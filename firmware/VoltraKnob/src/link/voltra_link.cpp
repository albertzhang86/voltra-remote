#include "voltra_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include "../protocol/voltra_protocol.h"
namespace vlink {
VoltraLink voltraLink;
namespace {
DeviceState current;
SemaphoreHandle_t mutex;
struct Lock { Lock(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Lock(){xSemaphoreGive(mutex);} };
enum class CommandType:uint8_t {Setting,Load,Mode,Cable,SelectParameter,BandMaximum,DamperLevel,Isokinetic};
struct Command {CommandType type;Parameter parameter;int value;uint32_t session;};
QueueHandle_t queue;
std::atomic<bool> disconnected{false}, stopRequested{false};
std::atomic<bool> discoveryVisible{true};
NimBLEClient* client=nullptr;
NimBLERemoteCharacteristic* writer=nullptr;
uint32_t workerSession=0;
int workerSlot=-1;
bool settingsDirty=false;
int diagnosticReplies=0;
void message(const char* text){Lock lock;strlcpy(current.message,text,sizeof(current.message));}
bool sameSession(){Lock lock;return current.session==workerSession && current.selected==workerSlot;}
void resetReadings(){
  for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i){current.modeValues[i]=0;current.modeValueMs[i]=0;}
  diagnosticReplies=0;current.twinMs=0;current.twinRoleMs=0;
  current.twinConnection=current.twinLastRole=current.twinMainForce=current.twinSubForce=current.twinTotalForce=-1;
  current.cableScreenId=-1;current.lengthUnitRaw=-1;current.cableCm=-1;current.cableOffsetCm=-1;current.cableMs=0;current.cableSavedSeq=0;current.cableActive=false;current.parameterSelectionPending=false;
  current.weightUnitRaw=-1;current.weightLbs=-1;current.chainsLbs=-1;current.inverseChainsEnabled=-1;current.eccentricLbs=-1000;
  current.controlBusyUntil=0;current.mode=-1;current.fitnessMode=-1;current.battery=-1;current.reps=0;
  current.loaded=false;current.armed=false;current.settingsMs=0;current.statusMs=0;current.confirmSeq=0;
}
void saveTrainers(){
  DeviceState snapshot;{Lock lock;snapshot=current;settingsDirty=false;}
  Preferences prefs;prefs.begin("voltra-pair",false);
  for(int i=0;i<2;++i){String key="addr"+String(i);prefs.putString(key.c_str(),snapshot.trainers[i].address);key="type"+String(i);prefs.putUChar(key.c_str(),snapshot.trainers[i].addressType);key="name"+String(i);prefs.putString(key.c_str(),snapshot.trainers[i].name);}
  prefs.end();
}
class ScanCallbacks:public NimBLEScanCallbacks {
 void onResult(const NimBLEAdvertisedDevice* dev) override {
  if(!dev->isAdvertisingService(NimBLEUUID(voltra::SERVICE_UUID)) && dev->getName().rfind(voltra::DEVICE_NAME_PREFIX,0)!=0)return;
  const std::string address=dev->getAddress().toString();
  Lock lock;
  int slot=-1;
  for(int i=0;i<2;++i)if(address==current.trainers[i].address)slot=i;
  if(slot<0)for(int i=0;i<2;++i)if(!current.trainers[i].address[0]){slot=i;break;}
  if(slot<0)return;
  auto& t=current.trainers[slot];
  if(!t.address[0]){strlcpy(t.address,address.c_str(),sizeof(t.address));t.addressType=dev->getAddress().getType();settingsDirty=true;}
  if(t.addressType!=dev->getAddress().getType()){t.addressType=dev->getAddress().getType();settingsDirty=true;}
  // Refresh saved names on discovery; nameless packets must not erase a known name.
  char advertisedName[sizeof(t.name)]{};
  strlcpy(advertisedName,dev->getName().c_str(),sizeof(advertisedName));
  if(advertisedName[0] && strcmp(t.name,advertisedName)!=0){strlcpy(t.name,advertisedName,sizeof(t.name));settingsDirty=true;}
  if(!t.seen && Serial.availableForWrite()>=100)Serial.printf("[discovered] %s name=%s\n",t.address,t.name);
  t.seen=true;t.lastSeenMs=millis();
 }
} scanCallbacks;
class ClientCallbacks:public NimBLEClientCallbacks {
 void onDisconnect(NimBLEClient*,int) override {disconnected=true;}
} clientCallbacks;
void notify(NimBLERemoteCharacteristic*,uint8_t* bytes,size_t size,bool){
  if(!voltra::validFrame(bytes,size))return;
  const auto e=voltra::decode(bytes,size);
  if(size>=14&&bytes[10]==0x0f&&diagnosticReplies<16&&Serial.availableForWrite()>=240){
    if(e.type==voltra::EventType::Unknown||e.twinLastRole>=0||(size>=16&&bytes[14]==0x38&&bytes[15]==0x54)||bytes[11]!=0){
      ++diagnosticReplies;Serial.printf("[twin-reply] ");for(size_t i=0;i<size&&i<64;++i)Serial.printf("%02x",bytes[i]);Serial.println();
    }
  }
  Lock lock;
  if(current.session!=workerSession)return;
  const uint32_t now=millis();
  if(e.twinConnection>=0){current.twinConnection=e.twinConnection;current.twinMs=now;}
  if(e.twinLastRole>=0){current.twinLastRole=e.twinLastRole;current.twinRoleMs=now;}
  if(e.twinMainForce>=0)current.twinMainForce=e.twinMainForce;
  if(e.twinSubForce>=0)current.twinSubForce=e.twinSubForce;
  if(e.twinTotalForce>=0)current.twinTotalForce=e.twinTotalForce;
  if(e.cableScreenId>=0)current.cableScreenId=e.cableScreenId;
  if(e.lengthUnitRaw>=0)current.lengthUnitRaw=e.lengthUnitRaw;
  if(e.cableCm>=0){current.cableCm=e.cableCm;current.cableMs=now;}
  if(e.cableOffsetCm>=0){current.cableOffsetCm=e.cableOffsetCm;if(e.cableSavedNotification)++current.cableSavedSeq;}
  if(e.weightUnitRaw>=0)current.weightUnitRaw=e.weightUnitRaw;
  if(e.weightLbs>=5 && e.weightLbs<=200){if(e.weightLbs!=current.weightLbs&&Serial.availableForWrite()>=120)Serial.printf("[weight-rx] previous=%d value=%d command=%02x type=%02x now=%lu\n",current.weightLbs,e.weightLbs,bytes[10],bytes[11],(unsigned long)now);
    current.weightLbs=e.weightLbs;current.settingsMs=now;++current.confirmSeq;}
  if(e.inverseChainsEnabled>=0 && e.inverseChainsEnabled<=1){current.inverseChainsEnabled=e.inverseChainsEnabled;++current.confirmSeq;}
  if(e.chainsLbs>=0 && e.chainsLbs<=100){current.chainsLbs=e.chainsLbs;++current.confirmSeq;}
  if(e.eccentricLbs>=-195 && e.eccentricLbs<=195){current.eccentricLbs=e.eccentricLbs;++current.confirmSeq;}
  if(e.mode>=0&&current.mode!=e.mode){current.mode=e.mode;for(auto& stamp:current.modeValueMs)stamp=0;}
  if(e.modeParameter>=0&&voltra::MODE_PARAMETERS[e.modeParameter].mode==current.mode){
    current.modeValues[e.modeParameter]=e.modeParameterValue;current.modeValueMs[e.modeParameter]=now;
    if(Serial.availableForWrite()>=140)Serial.printf("[mode-config] mode=%d id=%04x width=%d value=%d\n",current.mode,voltra::MODE_PARAMETERS[e.modeParameter].id,e.modeParameterWidth,e.modeParameterValue);
  }
  if(e.fitnessMode>=0){
    current.fitnessMode=e.fitnessMode;current.statusMs=now;
    current.loaded=voltra::fitnessModeLoaded(e.fitnessMode);
    current.armed=voltra::fitnessModeArmed(e.fitnessMode);
  }
  if(e.battery>=0 && e.battery<=100)current.battery=e.battery;
  if(e.repCount>=0)current.reps=e.repCount;
}
bool send(voltra::Frame frame){
  if(!sameSession()||disconnected||!writer||!client->isConnected())return false;
  // Only the worker owns BLE writes. UI selection invalidates session before a new connection.
  if(!writer->writeValue(frame.data,frame.len,writer->canWrite())){disconnected=true;return false;}
  return true;
}
bool connect(){
  Trainer trainer;
  {Lock lock;workerSlot=current.selected;workerSession=current.session;trainer=current.trainers[workerSlot];current.link=LinkState::Connecting;resetReadings();}
  disconnected=false;
  NimBLEDevice::getScan()->stop();
  if(!client){client=NimBLEDevice::createClient();client->setClientCallbacks(&clientCallbacks,false);client->setConnectTimeout(5000);}
  if(Serial.availableForWrite()>=120)Serial.printf("[connect] address=%s name=%s type=%u\n",trainer.address,trainer.name,trainer.addressType);
  if(!client->connect(NimBLEAddress(std::string(trainer.address),trainer.addressType))){if(Serial.availableForWrite()>=100)Serial.printf("[connect-failed] address=%s error=%d\n",trainer.address,client->getLastError());return false;}
  if(!sameSession())return false;
  auto* service=client->getService(voltra::SERVICE_UUID);if(!service)return false;
  writer=service->getCharacteristic(voltra::WRITE_CHAR_UUID);
  auto* updates=service->getCharacteristic(voltra::NOTIFY_CHAR_UUID);
  if(!writer||!updates||!updates->subscribe(true,notify))return false;
  {Lock lock;current.link=LinkState::Handshaking;}
  message("Accept on Voltra screen");
  if(!send(voltra::authFrame()))return false;
  for(int i=0;i<30 && sameSession()&&!disconnected;++i)vTaskDelay(pdMS_TO_TICKS(100));
  for(size_t i=0;i<voltra::INIT_COUNT;++i){if(!send(voltra::initFrame(i)))return false;vTaskDelay(pdMS_TO_TICKS(25));}
  if(!send(voltra::telemetrySubscribeAllFrame()))return false;
  // Connection never changes training mode, weight, or load state.
  const uint32_t started=millis();uint32_t poll=0;
  while(sameSession()&&!disconnected && millis()-started<20000){
    if(millis()-poll>1000){poll=millis();send(voltra::readSettingsFrame());}
    {Lock lock;if(current.weightLbs>=5 && current.statusMs && current.mode>=0){current.link=LinkState::Ready;strlcpy(current.message,"Connected",sizeof(current.message));return true;}}
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return false;
}
void pump(){
  uint32_t lastPoll=0;int configCursor=0;
  while(sameSession()&&!disconnected&&client->isConnected()){
    auto* scan=NimBLEDevice::getScan();
    if(discoveryVisible.load()){if(!scan->isScanning())scan->start(0,false,true);}
    else if(scan->isScanning())scan->stop();
    if(stopRequested.exchange(false)){
      xQueueReset(queue);{Lock lock;current.parameterSelectionPending=false;}
      if(!send(voltra::stopFrame()))break;
      message("Unload sent; checking");
      send(voltra::readSettingsFrame());
      continue;
    }
    Command command;
    if(xQueueReceive(queue,&command,0)==pdTRUE && command.session==workerSession){
      DeviceState s;{Lock lock;s=current;current.controlBusyUntil=millis()+10000;}
      if(command.type==CommandType::BandMaximum){
        uint8_t storage[19];voltra::Frame f;
        const uint32_t previous=s.modeValueMs[0];
        if(canEditBand(s,millis())&&!stopRequested&&sameSession()&&voltra::bandMaximumFrame(command.value,storage,f)&&send(f)){
          const uint32_t started=millis();uint32_t lastRead=0;
          while(sameSession()&&!disconnected&&!stopRequested&&millis()-started<2500){
            const auto latest=voltraLink.state();
            if(latest.mode!=2)break;
            if(latest.modeValueMs[0]!=previous&&latest.modeValues[0]==command.value)break;
            if(!lastRead||millis()-lastRead>=250){lastRead=millis();uint8_t read[17];voltra::Frame query;if(voltra::readModeParameterFrame(0,read,query))send(query);send(voltra::readSettingsFrame());}
            vTaskDelay(pdMS_TO_TICKS(20));
          }
        }
        {Lock lock;if(current.session==command.session&&uxQueueMessagesWaiting(queue)==0)current.controlBusyUntil=0;}
        continue;
      }
      if(command.type==CommandType::Isokinetic){
        const int index=static_cast<int>(command.parameter);uint8_t storage[21];voltra::Frame f;
        const uint32_t previous=index>=7&&index<=11?s.modeValueMs[index]:0;
        if(canEditIsokinetic(s,millis(),index)&&!stopRequested&&sameSession()&&voltra::isokineticFrame(index,command.value,storage,f)&&send(f)){
          const uint32_t started=millis();uint32_t lastRead=0;
          while(sameSession()&&!disconnected&&!stopRequested&&millis()-started<2500){
            const auto latest=voltraLink.state();if(latest.mode!=7||latest.loaded||latest.armed)break;
            if(latest.modeValueMs[index]!=previous&&latest.modeValues[index]==command.value)break;
            if(!lastRead||millis()-lastRead>=250){lastRead=millis();uint8_t read[17];voltra::Frame query;if(voltra::readModeParameterFrame(index,read,query))send(query);send(voltra::readSettingsFrame());}
            vTaskDelay(pdMS_TO_TICKS(20));
          }
        }
        {Lock lock;if(current.session==command.session&&uxQueueMessagesWaiting(queue)==0)current.controlBusyUntil=0;}
        continue;
      }
      if(command.type==CommandType::DamperLevel){
        uint8_t storage[19];voltra::Frame f;
        const uint32_t previous=s.modeValueMs[5];
        if(canEditDamper(s,millis())&&!stopRequested&&sameSession()&&voltra::damperLevelFrame(command.value,storage,f)&&send(f)){
          const uint32_t started=millis();uint32_t lastRead=0;
          while(sameSession()&&!disconnected&&!stopRequested&&millis()-started<2500){
            const auto latest=voltraLink.state();
            if(latest.mode!=4)break;
            if(latest.modeValueMs[5]!=previous&&latest.modeValues[5]==command.value)break;
            if(!lastRead||millis()-lastRead>=250){lastRead=millis();uint8_t read[17];voltra::Frame query;if(voltra::readModeParameterFrame(5,read,query))send(query);send(voltra::readSettingsFrame());}
            vTaskDelay(pdMS_TO_TICKS(20));
          }
        }
        {Lock lock;if(current.session==command.session&&uxQueueMessagesWaiting(queue)==0)current.controlBusyUntil=0;}
        continue;
      }
      if(command.type==CommandType::SelectParameter){
        const uint32_t started=millis();bool ready=fresh(s,started)&&!stopRequested;
        const bool chain=command.parameter==Parameter::Chains||command.parameter==Parameter::InverseChains;
        const int inverse=command.parameter==Parameter::InverseChains?1:0;
        if(chain){
          voltra::Frame direction;
          ready=ready&&canEdit(s,started)&&voltra::inverseChainsFrame(inverse,direction)&&send(direction);
        }
        bool confirmed=false;uint32_t lastRead=0;
        while(ready&&sameSession()&&!disconnected&&!stopRequested&&millis()-started<2500){
          if(!lastRead||millis()-lastRead>=250){lastRead=millis();if(!send(voltra::readSettingsFrame()))break;}
          const auto state=voltraLink.state();
          if(state.settingsMs>started&&(!chain||state.inverseChainsEnabled==inverse)){confirmed=true;break;}
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        {Lock lock;if(current.session==command.session){current.parameterSelectionPending=false;current.controlBusyUntil=0;strlcpy(current.message,confirmed?"Settings read from Voltra":"Setting selection not confirmed",sizeof(current.message));}}
        continue;
      }
      if(command.type==CommandType::Cable){
        if(!command.value||(twinControlReady(s,millis())&&fresh(s,millis())&&!s.loaded&&!s.armed&&s.mode>0)){
          if(send(voltra::cableFrame(command.value!=0))){Lock lock;current.cableActive=command.value!=0;}
        }
        send(voltra::readCableFrame());
        {Lock lock;current.controlBusyUntil=0;}
        continue;
      }
      if(command.type==CommandType::Setting && canEditParameter(s,millis(),command.parameter)){
        voltra::Frame f;bool valid=false;
        switch(command.parameter){
          case Parameter::Weight:valid=voltra::weightFrame(command.value,f);break;
          case Parameter::InverseChains:valid=voltra::chainsFrame(command.value,f);break;
          case Parameter::Chains:valid=voltra::chainsFrame(command.value,f);break;
          case Parameter::Eccentric:valid=voltra::eccentricFrame(command.value,f);break;
        }
        if(valid&&!stopRequested){
          bool ready=true;
          if(command.parameter==Parameter::Chains||command.parameter==Parameter::InverseChains){
            const int inverse=command.parameter==Parameter::InverseChains?1:0;
            voltra::Frame direction;
            ready=voltra::inverseChainsFrame(inverse,direction)&&send(direction);
          }
          const auto latest=voltraLink.state();
          if(ready&&!stopRequested&&sameSession()&&canEditParameter(latest,millis(),command.parameter))send(f);
          send(voltra::readSettingsFrame());
        }
      } else if(command.type==CommandType::Load && s.mode==command.value && canLoad(s,millis()) && !stopRequested){
        if(send(voltra::setupFrame())){
          for(int i=0;i<15&&!stopRequested&&sameSession();++i)vTaskDelay(pdMS_TO_TICKS(20));
          s=voltraLink.state();
          if(sameSession()&&s.mode==command.value&&canLoad(s,millis())&&!stopRequested)send(voltra::goFrame());
          // Do not claim LOADED until a validated notification confirms it.
          send(voltra::readSettingsFrame());
        }
      } else if(command.type==CommandType::Mode && twinControlReady(s,millis()) && fresh(s,millis()) && voltra::fitnessModeUnloaded(s.fitnessMode)){
        voltra::Frame modeFrame;if(voltra::modeFrame(command.value,modeFrame))send(modeFrame);send(voltra::readSettingsFrame());
      }
      const uint32_t waitStart=millis();
      while(sameSession()&&!disconnected&&!stopRequested&&millis()-waitStart<2500){
        auto confirmed=voltraLink.state();
        bool matched=command.type==CommandType::Load ? confirmed.loaded :
          command.type==CommandType::Mode ? confirmed.mode==command.value :
          command.parameter==Parameter::Weight ? confirmed.weightLbs==command.value :
          command.parameter==Parameter::InverseChains ? confirmed.inverseChainsEnabled==1&&confirmed.chainsLbs==command.value :
          command.parameter==Parameter::Chains ? confirmed.inverseChainsEnabled==0&&confirmed.chainsLbs==command.value : confirmed.eccentricLbs==command.value;
        if(matched){Lock lock;if(uxQueueMessagesWaiting(queue)==0)current.controlBusyUntil=0;break;}
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
    if(millis()-lastPoll>=1000){lastPoll=millis();send(voltra::readSettingsFrame());send(voltra::readWeightUnitFrame());send(voltra::readLengthUnitFrame());send(voltra::readCableFrame());send(voltra::readTwinProbeFrame(0));
      const auto snapshot=voltraLink.state();
      for(int tried=0;tried<voltra::MODE_PARAMETER_COUNT;++tried){const int index=configCursor;configCursor=(configCursor+1)%voltra::MODE_PARAMETER_COUNT;
        if(voltra::MODE_PARAMETERS[index].mode==snapshot.mode){uint8_t storage[17];voltra::Frame f;if(voltra::readModeParameterFrame(index,storage,f))send(f);break;}
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void task(void*){
  auto* scan=NimBLEDevice::getScan();scan->setScanCallbacks(&scanCallbacks,true);scan->setMaxResults(0);scan->setActiveScan(true);scan->setInterval(100);scan->setWindow(70);
  uint32_t retryAt=0;
  while(true){
    bool save;int selected;{Lock lock;save=settingsDirty;selected=current.selected;}
    if(save)saveTrainers();
    if(selected<0 || static_cast<int32_t>(millis()-retryAt)<0){
      if(!scan->isScanning())scan->start(0,false,true);
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    message("Connecting...");
    if(connect())pump();
    writer=nullptr;
    if(client&&client->isConnected())client->disconnect();
    {Lock lock;current.link=LinkState::Scanning;resetReadings();++current.session;strlcpy(current.message,"Reconnecting / close Beyond+",sizeof(current.message));}
    xQueueReset(queue);stopRequested=false;
    retryAt=millis()+2000;
  }
}
}
void VoltraLink::begin(){
 mutex=xSemaphoreCreateMutex();queue=xQueueCreate(8,sizeof(Command));
 Preferences p;p.begin("voltra-pair",true);
 for(int i=0;i<2;++i){String key="addr"+String(i);strlcpy(current.trainers[i].address,p.getString(key.c_str(),"").c_str(),18);key="type"+String(i);current.trainers[i].addressType=p.getUChar(key.c_str(),0);key="name"+String(i);strlcpy(current.trainers[i].name,p.getString(key.c_str(),"").c_str(),32);}p.end();
 NimBLEDevice::init("Voltra Remote");NimBLEDevice::setMTU(247);
 xTaskCreatePinnedToCore(task,"VOLTRA",8192,nullptr,2,nullptr,0);
}
DeviceState VoltraLink::state(){Lock lock;return current;}
void VoltraLink::setDiscoveryVisible(bool visible){discoveryVisible.store(visible);}
bool VoltraLink::selectTrainer(int slot){
 Lock lock;
 if(slot<0||slot>1||!current.trainers[slot].address[0])return false;
 if(!trainerAvailable(current,slot,millis()))return false;
 if(pairedFollower(current,slot,millis()))return false;
 if(current.selected==slot)return true;
 if(!canSwitch(current,millis())){strlcpy(current.message,"Unload before switching",sizeof(current.message));return false;}
 current.selected=slot;++current.session;current.link=LinkState::Scanning;resetReadings();xQueueReset(queue);stopRequested=false;return true;
}
bool VoltraLink::requestParameter(Parameter parameter){
 Lock lock;
 if(!twinControlReady(current,millis())||!fresh(current,millis())||current.mode!=1||current.cableActive||current.parameterSelectionPending)return false;
 if(current.controlBusyUntil&&static_cast<int32_t>(current.controlBusyUntil-millis())>0)return false;
 if((parameter==Parameter::Chains||parameter==Parameter::InverseChains)&&!canEdit(current,millis()))return false;
 Command c{CommandType::SelectParameter,parameter,0,current.session};
 if(xQueueSend(queue,&c,0)!=pdTRUE)return false;
 current.parameterSelectionPending=true;current.controlBusyUntil=millis()+4000;return true;
}
bool VoltraLink::requestSetting(Parameter parameter,int value){
 Lock lock;if(current.parameterSelectionPending||!canEditParameter(current,millis(),parameter))return false;
 voltra::Frame f;bool valid=parameter==Parameter::Weight?voltra::weightFrame(value,f):parameter==Parameter::Chains?voltra::chainsFrame(value,f):parameter==Parameter::InverseChains?voltra::chainsFrame(value,f):parameter==Parameter::Eccentric?voltra::eccentricFrame(value,f):false;
 if(!valid)return false;
 Command c{CommandType::Setting,parameter,value,current.session};bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
bool VoltraLink::requestBandMaximum(int pounds){
 Lock lock;if(!canEditBand(current,millis())||pounds<15||pounds>200)return false;
 Command c{CommandType::BandMaximum,Parameter::Weight,pounds,current.session};
 const bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
bool VoltraLink::requestDamperLevel(int index){
 Lock lock;if(!canEditDamper(current,millis())||index<0||index>9)return false;
 Command c{CommandType::DamperLevel,Parameter::Weight,index,current.session};
 const bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
bool VoltraLink::requestIsokinetic(int index,int value){
 Lock lock;if(!canEditIsokinetic(current,millis(),index)||!voltra::validIsokineticSetting(index,value))return false;
 Command c{CommandType::Isokinetic,static_cast<Parameter>(index),value,current.session};
 const bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
bool VoltraLink::requestCable(bool open){
 Lock lock;if(current.link!=LinkState::Ready)return false;
 if(open&&(!twinControlReady(current,millis())||!fresh(current,millis())||current.loaded||current.armed||current.mode<=0||current.cableActive||!canSwitch(current,millis())))return false;
 Command c{CommandType::Cable,Parameter::Weight,open?1:0,current.session};
 if(xQueueSend(queue,&c,0)!=pdTRUE)return false;
 current.controlBusyUntil=millis()+6000;return true;
}
bool VoltraLink::requestLoad(uint32_t expectedSession){Lock lock;if((expectedSession!=UINT32_MAX&&expectedSession!=current.session)||!canLoad(current,millis()))return false;Command c{CommandType::Load,Parameter::Weight,current.mode,current.session};bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;}
bool VoltraLink::requestUnload(uint32_t expectedSession){Lock lock;if((expectedSession!=UINT32_MAX&&expectedSession!=current.session)||current.link!=LinkState::Ready)return false;stopRequested=true;return true;}
bool VoltraLink::requestWeightMode(){return requestMode(1);}
bool VoltraLink::requestMode(int mode){
 Lock lock;voltra::Frame frame;
 if(!voltra::supportedMode(mode)||!twinControlReady(current,millis())||!canSwitch(current,millis())||!fresh(current,millis())||!voltra::modeFrame(mode,frame))return false;
 Command c{CommandType::Mode,Parameter::Weight,mode,current.session};
 bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
}
