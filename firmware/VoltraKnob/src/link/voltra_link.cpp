#include "voltra_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include <functional>
#include "../protocol/voltra_protocol.h"
namespace vlink {
VoltraLink voltraLink;
namespace {
DeviceState current;
SemaphoreHandle_t mutex;
SemaphoreHandle_t discoveryMutex;
bool tvRadioActive=false;
struct Lock { Lock(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Lock(){xSemaphoreGive(mutex);} };
enum class CommandType:uint8_t {Setting,Load,Mode,Cable,SelectParameter,BandMaximum,DamperLevel,Isokinetic,Twin};
struct Command {CommandType type;Parameter parameter;int value;uint32_t session;uint32_t tvEpoch=0,deadline=0;};
QueueHandle_t queue;
// Bounded capture of one complete notification per family. BLE callbacks only
// enqueue bytes; the worker prints them when USB has room, never blocking BLE.
struct FrameDiagnostic { uint16_t size=0; bool valid=false; uint8_t bytes[244]{}; };
QueueHandle_t diagnosticQueue;
uint64_t diagnosticFamilies[24]{};
size_t diagnosticFamilyCount=0;
void captureDiagnostic(const uint8_t* bytes,size_t size,bool valid){
  if(!bytes||size<4||size>244||diagnosticFamilyCount>=24)return;
  uint64_t key=size|(uint64_t(bytes[2])<<8)|(uint64_t(valid)<<16);
  if(size>12)key|=(uint64_t(bytes[10])<<24)|(uint64_t(bytes[11])<<32)|(uint64_t(bytes[12])<<40);
  // A single-param read carries its register ID at 14, unlike stream packets.
  if(size>15&&bytes[10]==0x0f)key|=(uint64_t(bytes[14])<<48)|(uint64_t(bytes[15])<<56);
  for(size_t i=0;i<diagnosticFamilyCount;++i)if(diagnosticFamilies[i]==key)return;
  FrameDiagnostic frame;frame.size=size;frame.valid=valid;memcpy(frame.bytes,bytes,size);
  if(xQueueSend(diagnosticQueue,&frame,0)==pdTRUE)diagnosticFamilies[diagnosticFamilyCount++]=key;
}
void printDiagnostic(){
  FrameDiagnostic frame;
  if(xQueuePeek(diagnosticQueue,&frame,0)!=pdTRUE)return;
  const size_t capacity=frame.size*2+64;
  if(Serial.availableForWrite()<int(capacity))return;
  if(xQueueReceive(diagnosticQueue,&frame,0)!=pdTRUE)return;
  char line[560];int offset=snprintf(line,sizeof(line),"[voltra-rx] valid=%u size=%u hex=",frame.valid,frame.size);
  for(size_t i=0;i<frame.size;++i)offset+=snprintf(line+offset,sizeof(line)-offset,"%02x",frame.bytes[i]);
  line[offset++]='\n';Serial.write(reinterpret_cast<const uint8_t*>(line),offset);
}

std::atomic<bool> disconnected{false}, stopRequested{false};
std::atomic<bool> discoveryVisible{true};
std::atomic<uint32_t> tvEpoch{0};
NimBLEClient* client=nullptr;
NimBLERemoteCharacteristic* writer=nullptr;
uint32_t workerSession=0;
uint32_t workerTvEpoch=0,workerTvDeadline=0;
int workerSlot=-1;
bool settingsDirty=false;
// Do not immediately save a forgotten advertisement again. Explicit discovery
// starts a new search; this is not a permanent block list.
char ignoredTrainers[2][18]{};
int diagnosticReplies=0;
uint16_t twinSequence=0,expectedTwinSequence=0;
uint8_t expectedTwinOperation=0;
bool twinAcknowledged=false;
void message(const char* text){Lock lock;strlcpy(current.message,text,sizeof(current.message));}
// Serialize scan start/stop with the TV's transition into advertising. Setting a
// flag alone leaves a race where the worker restarts scanning just before pairing.
void updateDiscovery(bool requested){
  xSemaphoreTake(discoveryMutex,portMAX_DELAY);
  auto* scan=NimBLEDevice::getScan();
  if(requested&&!tvRadioActive){if(!scan->isScanning())scan->start(0,false,true);}
  else if(scan->isScanning())scan->stop();
  xSemaphoreGive(discoveryMutex);
}
bool sameSession(){Lock lock;return current.session==workerSession && current.selected==workerSlot;}
void resetReadings(){
  for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i){current.modeValues[i]=0;current.modeValueMs[i]=0;}
  current.endpoint={};current.endpointMs=0;current.twinPending=false;
  diagnosticReplies=0;current.twinMs=0;current.twinRoleMs=0;
  current.twinConnection=current.twinLastRole=current.twinMainForce=current.twinSubForce=current.twinTotalForce=-1;
  current.cableScreenId=-1;current.lengthUnitRaw=-1;current.cableCm=-1;current.cableOffsetCm=-1;current.cableMs=0;current.cableSavedSeq=0;current.cableActive=false;current.parameterSelectionPending=false;
  current.weightUnitRaw=-1;current.weightLbs=-1;current.chainsLbs=-1;current.inverseChainsEnabled=-1;current.eccentricLbs=-1000;
  current.controlBusyUntil=0;current.mode=-1;current.fitnessMode=-1;current.battery=-1;current.reps=0;
  current.loaded=false;current.armed=false;current.settingsMs=0;current.statusMs=0;current.confirmSeq=0;
  clearWeightTelemetry(current);current.repMs=0;
  diagnosticFamilyCount=0;xQueueReset(diagnosticQueue);
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
  for(const auto& ignored:ignoredTrainers)if(!strcasecmp(address.c_str(),ignored))return;
  const uint32_t now=millis();
  const int discovered=nearbySlot(current,address.c_str(),now);
  if(discovered>=0){
    auto& nearby=current.nearby[discovered];
    if(strcasecmp(nearby.address,address.c_str()))nearby={};
    strlcpy(nearby.address,address.c_str(),sizeof(nearby.address));
    nearby.addressType=dev->getAddress().getType();
    const auto name=dev->getName();if(!name.empty())strlcpy(nearby.name,name.c_str(),sizeof(nearby.name));
    nearby.seen=true;nearby.lastSeenMs=now;
  }
  // Discovery does not save a trainer. Refresh remembered entries independently.
  for(auto& trainer:current.trainers){
    if(strcasecmp(trainer.address,address.c_str()))continue;
    if(trainer.addressType!=dev->getAddress().getType()){trainer.addressType=dev->getAddress().getType();settingsDirty=true;}
    const auto name=dev->getName();
    if(!name.empty()&&strcmp(trainer.name,name.c_str())){strlcpy(trainer.name,name.c_str(),sizeof(trainer.name));settingsDirty=true;}
    trainer.seen=true;trainer.lastSeenMs=now;
  }
 }
} scanCallbacks;
class ClientCallbacks:public NimBLEClientCallbacks {
 void onDisconnect(NimBLEClient*,int) override {disconnected=true;}
} clientCallbacks;
void notify(NimBLERemoteCharacteristic*,uint8_t* bytes,size_t size,bool){
  const bool valid=voltra::validFrame(bytes,size);
  {Lock lock;if(current.session==workerSession)captureDiagnostic(bytes,size,valid);}
  if(!valid)return;
  const auto e=voltra::decode(bytes,size);
  if(size>=14&&bytes[10]==0x0f&&diagnosticReplies<16&&Serial.availableForWrite()>=240){
    if(e.type==voltra::EventType::Unknown||e.twinLastRole>=0||(size>=16&&bytes[14]==0x38&&bytes[15]==0x54)||bytes[11]!=0){
      ++diagnosticReplies;Serial.printf("[twin-reply] ");for(size_t i=0;i<size&&i<64;++i)Serial.printf("%02x",bytes[i]);Serial.println();
    }
  }
  Lock lock;
  if(current.session!=workerSession)return;
  const uint32_t now=millis();
  const auto endpoint=voltra::decodeEndpoint(bytes,size);
  if(endpoint.valid&&current.selected>=0&&!strcasecmp(endpoint.local,current.trainers[current.selected].address)){
    if((!current.endpoint.valid||current.endpoint.status!=endpoint.status||strcmp(current.endpoint.peer,endpoint.peer))&&Serial.availableForWrite()>=160)
      Serial.printf("[native-twin] status=%02x controller=%s peer=%s batteries=%d,%d\n",endpoint.status,endpoint.local,endpoint.peer,endpoint.battery,endpoint.peerBattery);
    if(current.endpoint.valid&&current.endpoint.status!=endpoint.status){
      clearWeightTelemetry(current);current.repMs=0;
    }
    current.endpoint=endpoint;current.endpointMs=now;
    current.twinConnection=endpoint.paired()?2:endpoint.independent()?0:-1;current.twinMs=now;
    if(endpoint.battery>=0)current.battery=endpoint.battery;
    if(endpoint.paired()){
      clearWeightTelemetry(current);current.repMs=0;
      auto& peer=current.trainers[1-current.selected];
      if(!peer.address[0]){strlcpy(peer.address,endpoint.peer,sizeof(peer.address));strlcpy(peer.name,"VOLTRA peer",sizeof(peer.name));settingsDirty=true;}
    }
  }
  if(expectedTwinOperation&&voltra::twinReply(bytes,size,expectedTwinSequence,expectedTwinOperation))twinAcknowledged=true;
  if(e.twinConnection>=0&& !current.endpoint.valid){current.twinConnection=e.twinConnection;current.twinMs=now;}
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
  if(e.mode>=0&&current.mode!=e.mode){
    current.mode=e.mode;for(auto& stamp:current.modeValueMs)stamp=0;
    clearWeightTelemetry(current);
  }
  if(e.modeParameter>=0&&voltra::MODE_PARAMETERS[e.modeParameter].mode==current.mode){
    current.modeValues[e.modeParameter]=e.modeParameterValue;current.modeValueMs[e.modeParameter]=now;
    if(Serial.availableForWrite()>=140)Serial.printf("[mode-config] mode=%d id=%04x width=%d value=%d\n",current.mode,voltra::MODE_PARAMETERS[e.modeParameter].id,e.modeParameterWidth,e.modeParameterValue);
  }
  if(e.fitnessMode>=0){
    if(current.fitnessMode!=e.fitnessMode){
      clearWeightTelemetry(current);
    }
    current.fitnessMode=e.fitnessMode;current.statusMs=now;
    current.loaded=voltra::fitnessModeLoaded(e.fitnessMode);
    current.armed=voltra::fitnessModeArmed(e.fitnessMode);
  }
  if(e.battery>=0 && e.battery<=100)current.battery=e.battery;
  if(e.repCount>=0&&!current.endpoint.paired()){current.reps=e.repCount;current.repMs=now;}
  updateWeightTelemetry(current,e,now);
}
bool send(voltra::Frame frame){
  if(!sameSession()||disconnected||!writer||!client->isConnected())return false;
  if(workerTvEpoch&&frame.len>10&&frame.data[10]==0x11&&
     (workerTvEpoch!=tvEpoch.load()||static_cast<int32_t>(workerTvDeadline-millis())<=0))return false;
  // Only the worker owns BLE writes. UI selection invalidates session before a new connection.
  if(!writer->writeValue(frame.data,frame.len,writer->canWrite())){disconnected=true;return false;}
  return true;
}
bool connect(){
  Trainer trainer;
  {Lock lock;if(current.selected<0||current.selected>=2)return false;workerSlot=current.selected;workerSession=current.session;trainer=current.trainers[workerSlot];current.link=LinkState::Connecting;resetReadings();}
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
  // Read setup independently: newer/older trainers may reject either register.
  // A failed readback must not authorize a guessed persistent write.
  send(voltra::readTelemetryRateFrame());send(voltra::readTelemetrySubscriptionFrame());send(voltra::readRuntimeWireWeightFrame());
  // Connection never changes training mode, weight, or load state.
  const uint32_t started=millis();uint32_t poll=0;
  while(sameSession()&&!disconnected && millis()-started<20000){
    if(millis()-poll>1000){poll=millis();send(voltra::readSettingsFrame());uint8_t bytes[13];send(voltra::endpointFrame(bytes));}
    {Lock lock;if(current.weightLbs>=5 && current.statusMs && current.mode>=0){current.link=LinkState::Ready;strlcpy(current.message,"Connected",sizeof(current.message));return true;}}
    printDiagnostic();vTaskDelay(pdMS_TO_TICKS(50));
  }
  return false;
}
// Pairing uses a short-lived second GATT client. The normal controller session
// remains authoritative and its verified endpoint report confirms the result.
struct PeerReadings {voltra::EndpointStatus endpoint;int mode=-1,fitness=-1;uint32_t endpointAt=0,statusAt=0;bool ack=false;};
PeerReadings peerReadings;
void peerNotify(NimBLERemoteCharacteristic*,uint8_t* bytes,size_t size,bool){
  if(!voltra::validFrame(bytes,size))return;
  const auto e=voltra::decode(bytes,size);const auto endpoint=voltra::decodeEndpoint(bytes,size);
  Lock lock;const uint32_t now=millis();
  if(endpoint.valid){peerReadings.endpoint=endpoint;peerReadings.endpointAt=now;}
  if(e.mode>=0)peerReadings.mode=e.mode;
  if(e.fitnessMode>=0){peerReadings.fitness=e.fitnessMode;peerReadings.statusAt=now;}
  if(voltra::twinReply(bytes,size,expectedTwinSequence,1))peerReadings.ack=true;
}
bool twinSessionValid(){
  return sameSession()&&!disconnected&&!stopRequested&&(!workerTvEpoch||workerTvEpoch==tvEpoch.load());
}
void pollTwinController(){uint8_t bytes[13];send(voltra::endpointFrame(bytes));send(voltra::readSettingsFrame());}
bool waitTwin(uint32_t timeout,const std::function<bool()>& complete){
  const uint32_t start=millis();uint32_t poll=0;
  while(twinSessionValid()&&millis()-start<timeout){
    if(!poll||millis()-poll>=500){poll=millis();pollTwinController();}
    if(complete())return true;
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  return false;
}
bool performTwin(bool create){
  auto s=voltraLink.state();s.twinPending=false;s.controlBusyUntil=0;
  if(!canChangeTwin(s,millis(),create))return false;
  const uint16_t sequence=++twinSequence;
  {Lock lock;expectedTwinSequence=sequence;expectedTwinOperation=create?1:2;twinAcknowledged=false;peerReadings={};}
  if(!create){
    uint8_t bytes[20];voltra::Frame frame;
    if(!voltra::twinFrame(2,s.endpoint.peer,sequence,bytes,frame)||!send(frame))return false;
    return waitTwin(15000,[&]{Lock lock;return twinAcknowledged&&current.endpointMs!=s.endpointMs&&current.endpoint.independent();});
  }
  const Trainer peer=s.trainers[1-s.selected];
  NimBLEDevice::getScan()->stop();
  auto* connection=NimBLEDevice::createClient();
  if(!connection)return false;
  connection->setConnectTimeout(5000);
  auto cleanup=[&]{if(connection->isConnected())connection->disconnect();NimBLEDevice::deleteClient(connection);};
  if(!connection->connect(NimBLEAddress(std::string(peer.address),peer.addressType))){cleanup();return false;}
  auto* service=connection->getService(voltra::SERVICE_UUID);
  auto* writes=service?service->getCharacteristic(voltra::WRITE_CHAR_UUID):nullptr;
  auto* events=service?service->getCharacteristic(voltra::NOTIFY_CHAR_UUID):nullptr;
  if(!writes||!events||!events->subscribe(true,peerNotify)){cleanup();return false;}
  auto writePeer=[&](voltra::Frame frame){return twinSessionValid()&&connection->isConnected()&&writes->writeValue(frame.data,frame.len,writes->canWrite());};
  if(!writePeer(voltra::authFrame())){cleanup();return false;}
  // Keep controller status fresh during the peer's read-only handshake.
  waitTwin(3000,[]{return false;});
  for(size_t i=0;i<voltra::INIT_COUNT;++i){if(!writePeer(voltra::initFrame(i))){cleanup();return false;}}
  uint32_t poll=0;
  const bool peerReady=waitTwin(12000,[&]{
    if(!poll||millis()-poll>=500){poll=millis();uint8_t bytes[13];writePeer(voltra::endpointFrame(bytes));writePeer(voltra::readSettingsFrame());}
    Lock lock;
    return peerReadings.endpoint.independent()&&!strcasecmp(peerReadings.endpoint.local,peer.address)&&
      peerReadings.endpointAt&&millis()-peerReadings.endpointAt<3500&&peerReadings.statusAt&&millis()-peerReadings.statusAt<3500&&
      peerReadings.mode==0&&voltra::fitnessModeUnloaded(peerReadings.fitness);
  });
  s=voltraLink.state();s.twinPending=false;s.controlBusyUntil=0;
  if(peerReady){s.trainers[1-s.selected].seen=true;s.trainers[1-s.selected].lastSeenMs=millis();}
  if(!peerReady||!canChangeTwin(s,millis(),true)){cleanup();return false;}
  uint8_t bytes[20];voltra::Frame frame;
  // Verified direction: future peer receives future controller's address.
  const bool sent=voltra::twinFrame(1,s.endpoint.local,sequence,bytes,frame)&&writePeer(frame);
  const bool paired=sent&&waitTwin(15000,[&]{Lock lock;return peerReadings.ack&&current.endpointMs!=s.endpointMs&&
    current.endpoint.paired()&&!strcasecmp(current.endpoint.peer,peer.address);});
  cleanup();return paired;
}

void pump(){
  uint32_t lastWeightPoll=0;
  uint32_t lastPoll=0;int configCursor=0;
  while(sameSession()&&!disconnected&&client->isConnected()){
    workerTvEpoch=0;workerTvDeadline=0;
    // A different saved trainer can be forgotten while this one stays connected.
    // Persist that removal now rather than waiting for this session to end.
    bool save;{Lock lock;save=settingsDirty;}if(save)saveTrainers();
    updateDiscovery(discoveryVisible.load());
    if(stopRequested.exchange(false)){
      xQueueReset(queue);{Lock lock;current.parameterSelectionPending=false;
        if(current.twinPending){current.twinPending=false;current.twinSucceeded=false;++current.twinCompleted;current.controlBusyUntil=0;}
      }
      if(!send(voltra::stopFrame()))break;
      {Lock lock;++current.stopFramesSent;current.lastStopFrameMs=millis();}
      message("Unload sent; checking");
      send(voltra::readSettingsFrame());
      continue;
    }
    Command command;
    if(xQueueReceive(queue,&command,0)==pdTRUE && command.session==workerSession){
      if(command.tvEpoch&&(command.tvEpoch!=tvEpoch.load()||static_cast<int32_t>(command.deadline-millis())<=0)){
        if(command.type==CommandType::Twin){Lock lock;current.twinPending=false;current.controlBusyUntil=0;current.twinSucceeded=false;++current.twinCompleted;}
        continue;
      }
      workerTvEpoch=command.tvEpoch;workerTvDeadline=command.deadline;
      DeviceState s;{Lock lock;s=current;current.controlBusyUntil=millis()+10000;}
      if(command.type==CommandType::Twin){
        message(command.value==1?"Checking second trainer":"Exiting Twin");
        if(Serial.availableForWrite()>=80)Serial.printf("[native-twin] request=%s\n",command.value==1?"create":"exit");
        const bool success=performTwin(command.value==1);
        if(Serial.availableForWrite()>=80)Serial.printf("[native-twin] confirmed=%d\n",success);
        {Lock lock;expectedTwinOperation=0;current.twinPending=false;current.controlBusyUntil=0;
          current.twinSucceeded=success;++current.twinCompleted;
          strlcpy(current.message,success?(command.value==1?"Twin connected":"Twin exited"):"Twin not confirmed",sizeof(current.message));}
        continue;
      }
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
    if(millis()-lastWeightPoll>=250){
      lastWeightPoll=millis();const auto snapshot=voltraLink.state();
      if(endpointFresh(snapshot,lastWeightPoll)&&snapshot.endpoint.independent()&&voltra::supportedMode(snapshot.mode))
        send(voltra::readRuntimeWireWeightFrame());
    }
    if(millis()-lastPoll>=1000){lastPoll=millis();send(voltra::readSettingsFrame());send(voltra::readWeightUnitFrame());send(voltra::readLengthUnitFrame());send(voltra::readCableFrame());uint8_t endpointBytes[13];send(voltra::endpointFrame(endpointBytes));
      const auto snapshot=voltraLink.state();
      for(int tried=0;tried<voltra::MODE_PARAMETER_COUNT;++tried){const int index=configCursor;configCursor=(configCursor+1)%voltra::MODE_PARAMETER_COUNT;
        if(voltra::MODE_PARAMETERS[index].mode==snapshot.mode){uint8_t storage[17];voltra::Frame f;if(voltra::readModeParameterFrame(index,storage,f))send(f);break;}
      }
    }
    printDiagnostic();vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void task(void*){
  auto* scan=NimBLEDevice::getScan();scan->setScanCallbacks(&scanCallbacks,true);scan->setMaxResults(0);scan->setActiveScan(true);scan->setInterval(100);scan->setWindow(70);
  uint32_t retryAt=0;
  while(true){
    bool save;int selected;{Lock lock;save=settingsDirty;selected=current.selected;}
    if(save)saveTrainers();
    if(selected<0 || static_cast<int32_t>(millis()-retryAt)<0){
      updateDiscovery(true);
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
 mutex=xSemaphoreCreateMutex();discoveryMutex=xSemaphoreCreateMutex();queue=xQueueCreate(8,sizeof(Command));diagnosticQueue=xQueueCreate(24,sizeof(FrameDiagnostic));
 Preferences p;p.begin("voltra-pair",true);
 for(int i=0;i<2;++i){String key="addr"+String(i);strlcpy(current.trainers[i].address,p.getString(key.c_str(),"").c_str(),18);key="type"+String(i);current.trainers[i].addressType=p.getUChar(key.c_str(),0);key="name"+String(i);strlcpy(current.trainers[i].name,p.getString(key.c_str(),"").c_str(),32);}p.end();
 NimBLEDevice::init("Voltra Remote");NimBLEDevice::setMTU(247);
}
void VoltraLink::start(){
 xTaskCreatePinnedToCore(task,"VOLTRA",8192,nullptr,2,nullptr,0);
}
DeviceState VoltraLink::state(){Lock lock;return current;}
void VoltraLink::setTvSession(uint32_t epoch){tvEpoch=epoch;}
bool VoltraLink::requestTwin(bool create,uint32_t epoch,uint32_t session,uint32_t deadline){
 Lock lock;const uint32_t now=millis();
 if(epoch&&(epoch!=tvEpoch.load()||session!=current.session||static_cast<int32_t>(deadline-now)<=0||static_cast<int32_t>(deadline-now)>2000))return false;
 if(!canChangeTwin(current,now,create)||uxQueueMessagesWaiting(queue))return false;
 if(create&&!current.trainers[1-current.selected].address[0]){
   const auto* candidate=nearbyTwinPeer(current,now);if(!candidate)return false;
   current.trainers[1-current.selected]=*candidate;settingsDirty=true;
 }
 Command c{CommandType::Twin,Parameter::Weight,create?1:2,current.session,epoch,deadline};
 if(xQueueSend(queue,&c,0)!=pdTRUE)return false;
 current.twinPending=true;current.controlBusyUntil=now+40000;return true;
}
bool VoltraLink::requestTvSetting(uint32_t epoch,uint32_t session,uint32_t deadline,bool mode,uint16_t field,int value){
 Lock lock;const uint32_t now=millis();
 if(!epoch||epoch!=tvEpoch.load()||session!=current.session||static_cast<int32_t>(deadline-now)<=0||static_cast<int32_t>(deadline-now)>2000)return false;
 // Endpoint identity and freshness authorize shared controls on the controller only.
 if(!fresh(current,now)||!twinControlReady(current,now)||current.parameterSelectionPending||current.cableActive)return false;
 if(current.controlBusyUntil&&static_cast<int32_t>(current.controlBusyUntil-now)>0)return false;
 Command c{CommandType::Setting,Parameter::Weight,value,current.session,epoch,deadline};
 if(mode){
   if((value!=1&&value!=2&&value!=4&&value!=7)||!canSwitch(current,now)||!voltra::fitnessModeUnloaded(current.fitnessMode))return false;
   c.type=CommandType::Mode;
 }else switch(field){
   case 0x3e86:case 0x3e87:case 0x3e88:{
     c.parameter=field==0x3e86?Parameter::Weight:field==0x3e88?Parameter::Eccentric:current.inverseChainsEnabled==1?Parameter::InverseChains:Parameter::Chains;
     if(!canEditParameter(current,now,c.parameter))return false;
     if((field==0x3e86&&(value<5||value>200||current.eccentricLbs<-195||value+current.eccentricLbs<5||value+current.eccentricLbs>200))||
        (field==0x3e87&&(value<0||value>100||current.inverseChainsEnabled<0))||
        (field==0x3e88&&(value<-195||value>195||current.weightLbs<5||current.weightLbs+value<5||current.weightLbs+value>200)))return false;
     break;
   }
   case 0x53b0:
     if(!canEdit(current,now)||(value!=0&&value!=1))return false;
     c.type=CommandType::SelectParameter;c.parameter=value?Parameter::InverseChains:Parameter::Chains;break;
   case 0x5362:if(!canEditBand(current,now)||value<15||value>200)return false;c.type=CommandType::BandMaximum;break;
   case 0x5103:if(!canEditDamper(current,now)||value<0||value>9)return false;c.type=CommandType::DamperLevel;break;
   default:{
     const int index=voltra::modeParameterIndex(field);
     if(index<7||index>11||!canEditIsokinetic(current,now,index)||!voltra::validIsokineticSetting(index,value))return false;
     c.type=CommandType::Isokinetic;c.parameter=static_cast<Parameter>(index);break;
   }
 }
 if(xQueueSend(queue,&c,0)!=pdTRUE)return false;
 if(c.type==CommandType::SelectParameter)current.parameterSelectionPending=true;
 current.controlBusyUntil=now+6000;return true;
}
void VoltraLink::setDiscoveryVisible(bool visible){discoveryVisible.store(visible);}
bool VoltraLink::setTvRadioActive(bool active){
 xSemaphoreTake(discoveryMutex,portMAX_DELAY);
 tvRadioActive=active;
 auto* scan=NimBLEDevice::getScan();
 const bool stopped=!active||!scan->isScanning()||scan->stop();
 {Lock lock;current.discoveryPausedForTv=active;}
 xSemaphoreGive(discoveryMutex);
 return stopped;
}
bool VoltraLink::selectTrainer(int slot){
 Lock lock;
 if(slot<0||slot>1||!current.trainers[slot].address[0])return false;
 if(!trainerAvailable(current,slot,millis()))return false;
 if(pairedFollower(current,slot,millis()))return false;
 if(current.selected==slot)return true;
 if(!canSwitch(current,millis())){strlcpy(current.message,"Unload before switching",sizeof(current.message));return false;}
 current.selected=slot;++current.session;current.link=LinkState::Scanning;resetReadings();xQueueReset(queue);stopRequested=false;return true;
}
bool VoltraLink::selectNearbyTrainer(const char* address){
 Lock lock;const uint32_t now=millis();
 if(!address||!canSwitch(current,now))return false;
 for(int i=0;i<2;++i)if(!strcasecmp(current.trainers[i].address,address))return false; // saved rows use selectTrainer
 const Trainer* candidate=nullptr;
 for(const auto& nearby:current.nearby)if(!strcasecmp(nearby.address,address)&&nearbyAdvertisement(nearby,now)){candidate=&nearby;break;}
 if(!candidate)return false;
 int slot=-1;for(int i=0;i<2;++i)if(!current.trainers[i].address[0]){slot=i;break;}
 if(slot<0){strlcpy(current.message,"Saved slots full: Manage devices",sizeof(current.message));return false;}
 current.trainers[slot]=*candidate;settingsDirty=true;
 current.selected=slot;++current.session;current.link=LinkState::Scanning;
 resetReadings();xQueueReset(queue);stopRequested=false;return true;
}
bool VoltraLink::forgetTrainer(int slot,const char* expectedAddress){
 Lock lock;
 if(!canForgetTrainer(current,slot,millis())||!expectedAddress||
    strcasecmp(current.trainers[slot].address,expectedAddress))return false;
 strlcpy(ignoredTrainers[slot],current.trainers[slot].address,sizeof(ignoredTrainers[slot]));
 for(auto& nearby:current.nearby)if(!strcasecmp(nearby.address,current.trainers[slot].address))nearby={};
 current.trainers[slot]={};settingsDirty=true;
 if(current.selected==slot){
   // Changing the session makes the worker close its connection. No trainer
   // writes, load changes, or native Twin changes are part of forgetting.
   current.selected=-1;++current.session;current.link=LinkState::Scanning;
   resetReadings();xQueueReset(queue);stopRequested=false;
 }
 strlcpy(current.message,"Trainer forgotten",sizeof(current.message));return true;
}
void VoltraLink::rediscoverTrainers(){Lock lock;memset(ignoredTrainers,0,sizeof(ignoredTrainers));discoveryVisible=true;}
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
bool VoltraLink::requestUnload(uint32_t expectedSession,UnloadSource source){Lock lock;if((expectedSession!=UINT32_MAX&&expectedSession!=current.session)||current.link!=LinkState::Ready)return false;
 if(source==UnloadSource::Touch)++current.unloadTouchRequests;else if(source==UnloadSource::Voice)++current.unloadVoiceRequests;else ++current.unloadOtherRequests;
 current.lastUnloadRequestMs=millis();current.lastUnloadRequestSession=current.session;
 stopRequested=true;return true;}
bool VoltraLink::requestWeightMode(){return requestMode(1);}
bool VoltraLink::requestMode(int mode){
 Lock lock;voltra::Frame frame;
 if(!voltra::supportedMode(mode)||!twinControlReady(current,millis())||!canSwitch(current,millis())||!fresh(current,millis())||!voltra::modeFrame(mode,frame))return false;
 Command c{CommandType::Mode,Parameter::Weight,mode,current.session};
 bool queued=xQueueSend(queue,&c,0)==pdTRUE;if(queued)current.controlBusyUntil=millis()+6000;return queued;
}
}
