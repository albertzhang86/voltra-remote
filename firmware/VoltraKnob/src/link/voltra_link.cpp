#include "voltra_link.h"
#include "twin_transaction.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include <functional>
#include "../protocol/voltra_protocol.h"
#include "../protocol/frame_assembler.h"
namespace vlink {
VoltraLink voltraLink;
namespace {
DeviceState current;
SemaphoreHandle_t mutex;
SemaphoreHandle_t discoveryMutex;
bool tvRadioActive=false;
struct Lock { Lock(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Lock(){xSemaphoreGive(mutex);} };
enum class CommandType:uint8_t {Setting,Load,Mode,Cable,SelectParameter,BandMaximum,DamperLevel,Isokinetic,Twin};
struct Command {CommandType type;Parameter parameter;int value;uint32_t session;uint32_t tvEpoch=0,deadline=0;char expectedPeer[18]{};bool handoff=false;uint32_t handoffRevision=0;Trainer handoffPeer{};bool addressTypeKnown=false;};
QueueHandle_t queue;
TvTwinOffer tvTwinOffer;
uint32_t workerHandoffRevision=0;
char workerHandoffPeer[18]{};
// Bounded capture of one complete notification per family. BLE callbacks only
// enqueue bytes; the worker prints them when USB has room, never blocking BLE.
struct FrameDiagnostic { uint16_t size=0; bool valid=false,twin=false; uint32_t uptime=0; uint8_t bytes[244]{}; };
QueueHandle_t diagnosticQueue;
uint64_t diagnosticFamilies[24]{};
size_t diagnosticFamilyCount=0;
// Separate Twin sampling budget: startup parameter replies and changing payload
// values must not exhaust the useful capture. At most 120 samples per family
// during a loaded interval, 4 while unloaded, with no callback-side printing.
uint16_t twinDiagnosticCounts[12]{};
uint32_t twinDiagnosticLastMs[12]{};
int twinDiagnosticState=-1;
void captureTwinDiagnostic(const uint8_t* bytes,size_t size,bool valid){
  if(!valid||size<14||size>244)return;
  int family=-1;
  if(bytes[10]==0xb4&&bytes[9]==0){
    if(bytes[8]<=2)family=bytes[8];
    else if(bytes[8]>=0x10&&bytes[8]<=0x12)family=bytes[8]-0x10+3;
  }
  if(bytes[10]==0xaa){
    if(bytes[11]==0x81)family=6;
    else if(bytes[11]==0xa1)family=7;
    else if(bytes[11]==0xa2)family=8;
    else if(bytes[11]==0x82)family=9;
    else if(bytes[11]==0xa0)family=10;
    else if(bytes[11]==0x80)family=11;
  }
  if(family<0||!current.endpoint.paired())return;
  const int state=current.loaded?1:0;
  if(twinDiagnosticState!=state){
    memset(twinDiagnosticCounts,0,sizeof(twinDiagnosticCounts));
    memset(twinDiagnosticLastMs,0,sizeof(twinDiagnosticLastMs));twinDiagnosticState=state;
  }
  const uint32_t now=millis();
  if(twinDiagnosticCounts[family]>=unsigned(state?120:4)||
     (twinDiagnosticLastMs[family]&&now-twinDiagnosticLastMs[family]<200))return;
  FrameDiagnostic frame;frame.size=size;frame.valid=true;frame.twin=true;frame.uptime=now;memcpy(frame.bytes,bytes,size);
  if(xQueueSend(diagnosticQueue,&frame,0)==pdTRUE){++twinDiagnosticCounts[family];twinDiagnosticLastMs[family]=now;}
}
void captureDiagnostic(const uint8_t* bytes,size_t size,bool valid){
  captureTwinDiagnostic(bytes,size,valid);
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
  char line[560];int offset=frame.twin?
    snprintf(line,sizeof(line),"[twin-rx] uptime=%lu valid=%u size=%u hex=",(unsigned long)frame.uptime,frame.valid,frame.size):
    snprintf(line,sizeof(line),"[voltra-rx] valid=%u size=%u hex=",frame.valid,frame.size);
  for(size_t i=0;i<frame.size;++i)offset+=snprintf(line+offset,sizeof(line)-offset,"%02x",frame.bytes[i]);
  line[offset++]='\n';Serial.write(reinterpret_cast<const uint8_t*>(line),offset);
}

std::atomic<bool> disconnected{false}, stopRequested{false};
std::atomic<bool> discoveryVisible{true};
std::atomic<uint32_t> tvEpoch{0};
NimBLEClient* client=nullptr;
NimBLERemoteCharacteristic* writer=nullptr;
// Notifications can be split into 20-byte chunks after a native Twin change.
// These buffers and subscription identities are protected by the state mutex.
voltra::FrameAssembler controllerFrames,peerFrames;
NimBLERemoteCharacteristic* controllerUpdates=nullptr;
NimBLERemoteCharacteristic* peerUpdates=nullptr;
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
uint32_t twinDeadline=0;
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
  controllerUpdates=nullptr;controllerFrames.reset();
  peerUpdates=nullptr;peerFrames.reset();
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
  twinDiagnosticState=-1;
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
 void onDisconnect(NimBLEClient*,int) override {
  Lock lock;disconnected=true;controllerUpdates=nullptr;controllerFrames.reset();
  peerUpdates=nullptr;peerFrames.reset();
 }
} clientCallbacks;
// Called only by the assembler while the state mutex is held. A partial packet
// cannot update freshness, acknowledge a command, or publish trainer readings.
void applyControllerFrame(const uint8_t* bytes,size_t size){
  captureDiagnostic(bytes,size,true);
  const auto e=voltra::decode(bytes,size);
  if(size>=14&&bytes[10]==0x0f&&diagnosticReplies<16&&Serial.availableForWrite()>=240){
    if(e.type==voltra::EventType::Unknown||e.twinLastRole>=0||(size>=16&&bytes[14]==0x38&&bytes[15]==0x54)||bytes[11]!=0){
      ++diagnosticReplies;Serial.printf("[twin-reply] ");for(size_t i=0;i<size&&i<64;++i)Serial.printf("%02x",bytes[i]);Serial.println();
    }
  }
  const uint32_t now=millis();
  const auto endpoint=voltra::decodeEndpoint(bytes,size);
  if(endpoint.valid&&current.selected>=0&&!strcasecmp(endpoint.local,current.trainers[current.selected].address)){
    if((!current.endpoint.valid||current.endpoint.status!=endpoint.status||strcmp(current.endpoint.peer,endpoint.peer))&&Serial.availableForWrite()>=160)
      Serial.printf("[native-twin] status=%02x controller=%s peer=%s batteries=%d,%d\n",endpoint.status,endpoint.local,endpoint.peer,endpoint.battery,endpoint.peerBattery);
    if(!current.endpoint.valid||current.endpoint.status!=endpoint.status||strcasecmp(current.endpoint.peer,endpoint.peer)){
      clearWeightTelemetry(current);current.repMs=0;
    }
    current.endpoint=endpoint;current.endpointMs=now;
    current.twinConnection=endpoint.paired()?2:endpoint.independent()?0:-1;current.twinMs=now;
    if(endpoint.battery>=0)current.battery=endpoint.battery;
    if(endpoint.paired()){
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
    clearWeightTelemetry(current);current.reps=0;current.repMs=0;
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
  updateRepetitionTelemetry(current,e,now);
  updateWeightTelemetry(current,e,now);
}
void notify(uint32_t session,NimBLERemoteCharacteristic* source,uint8_t* bytes,size_t size){
  Lock lock;
  if(source!=controllerUpdates||!controllerUpdates||disconnected||
     current.session!=session||workerSession!=session||current.selected!=workerSlot)return;
  controllerFrames.feed(bytes,size,millis(),applyControllerFrame);
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
  {Lock lock;controllerFrames.reset();controllerUpdates=updates;}
  const uint32_t session=workerSession;
  if(!writer||!updates||!updates->subscribe(true,[session](NimBLERemoteCharacteristic* source,uint8_t* bytes,size_t size,bool){
    notify(session,source,bytes,size);
  }))return false;
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
struct PeerReadings: TwinParticipant {bool ack=false;};
PeerReadings peerReadings;
TwinParticipant controllerReadings(){
  Lock lock;return {current.endpoint,current.mode,current.fitnessMode,current.endpointMs,current.statusMs};
}
PeerReadings readPeer(){Lock lock;return peerReadings;}
// Peer fragments must never be joined with controller fragments or survive the
// temporary peer connection. The callback caller owns the state mutex.
void applyPeerFrame(const uint8_t* bytes,size_t size){
  const auto e=voltra::decode(bytes,size);const auto endpoint=voltra::decodeEndpoint(bytes,size);
  const uint32_t now=millis();
  if(endpoint.valid){peerReadings.endpoint=endpoint;peerReadings.endpointAt=now;}
  if(e.mode>=0)peerReadings.mode=e.mode;
  if(e.fitnessMode>=0){peerReadings.fitness=e.fitnessMode;peerReadings.statusAt=now;}
  if(voltra::twinReply(bytes,size,expectedTwinSequence,1))peerReadings.ack=true;
}
void peerNotify(uint32_t session,uint16_t sequence,NimBLERemoteCharacteristic* source,uint8_t* bytes,size_t size){
  Lock lock;
  if(source!=peerUpdates||!peerUpdates||disconnected||expectedTwinOperation!=1||
     expectedTwinSequence!=sequence||current.session!=session||workerSession!=session||
     current.selected!=workerSlot)return;
  peerFrames.feed(bytes,size,millis(),applyPeerFrame);
}
const char* twinSessionIssue(){
  if(!sameSession())return "trainer-session-changed";
  if(disconnected)return "controller-disconnected";
  if(stopRequested)return "unload-requested";
  if(workerTvEpoch&&workerTvEpoch!=tvEpoch.load())return "tv-session-changed";
  if(twinDeadline&&static_cast<int32_t>(twinDeadline-millis())<=0)return "transaction-expired";
  if(workerHandoffRevision){
    Lock lock;
    if(!tvTwinTransactionAuthorized(tvTwinOffer,workerHandoffRevision,workerTvEpoch,workerSession,
                                   workerHandoffPeer,twinDeadline,millis()))return "handoff-revoked";
  }
  return nullptr;
}
bool twinSessionValid(){return !twinSessionIssue();}
void pollTwinController(){uint8_t bytes[13];send(voltra::endpointFrame(bytes));send(voltra::readSettingsFrame());}
bool waitTwin(uint32_t timeout,const std::function<bool()>& complete){
  const uint32_t start=millis();uint32_t poll=0;
  while(twinSessionValid()&&millis()-start<timeout){
    if(!poll||millis()-poll>=500){poll=millis();pollTwinController();}
    // BLE writes can block. A readback received during that wait cannot revive
    // a cancelled or expired transaction, even if it would satisfy the predicate.
    if(!twinSessionValid()||millis()-start>=timeout)return false;
    if(complete())return twinSessionValid()&&millis()-start<timeout;
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  return false;
}
bool performTwin(bool create,const char* expectedPeer,const Command& command){
  auto s=voltraLink.state();s.twinPending=false;s.controlBusyUntil=0;
  auto fail=[&](const char* reason){if(sameSession())message(reason);return false;};
  if(command.handoff){
    TvTwinOffer authorization;{Lock lock;authorization=tvTwinOffer;}
    if(!create||authorization.revision!=command.handoffRevision||
       !canHandoffTwin(s,authorization,workerTvEpoch,expectedPeer,millis()))return fail("TV Twin authorization expired");
    workerHandoffRevision=command.handoffRevision;
    strlcpy(workerHandoffPeer,expectedPeer,sizeof(workerHandoffPeer));
  }else if(!canChangeTwin(s,millis(),create))return fail("Twin unavailable; check trainer state");
  if(create&&!command.handoff&&expectedPeer&&expectedPeer[0]&&strcasecmp(expectedPeer,s.trainers[1-s.selected].address))
    return fail("Twin peer changed; try again");
  // The TV's two-second deadline applies to dispatch, not the authenticated
  // second-device handshake. The accepted operation remains bounded and is
  // cancelled immediately when its trainer session or TV epoch changes.
  twinDeadline=millis()+40000;
  if(workerTvEpoch)workerTvDeadline=twinDeadline;
  const int previousMode=s.mode;
  const Trainer controller=s.trainers[s.selected];
  Trainer peer=command.handoff?command.handoffPeer:s.trainers[1-s.selected];
  const uint32_t transactionStarted=millis();
  const uint16_t sequence=++twinSequence;
  {Lock lock;expectedTwinSequence=sequence;expectedTwinOperation=create?1:2;twinAcknowledged=false;
    peerUpdates=nullptr;peerFrames.reset();peerReadings={};}
  auto controllerReady=[&]{
    auto latest=voltraLink.state();latest.twinPending=false;latest.controlBusyUntil=0;
    return twinSessionValid()&&twinSetupReady(latest,millis())&&
      !strcasecmp(latest.endpoint.local,controller.address);
  };
  auto controllerIdle=[&](const std::function<bool()>& peerStillReady){
    const auto before=controllerReadings();
    if(!controllerReady()||!twinParticipantUnloaded(before,controller.address,millis()))return false;
    if(create?!before.endpoint.independent():
       (!before.endpoint.paired()||strcasecmp(before.endpoint.peer,s.endpoint.peer)))return false;
    if(before.mode==0)return true;
    if(!twinSessionValid()||!send(voltra::modeIdleFrame()))return false;
    return waitTwin(3000,[&]{
      if(peerStillReady&&!peerStillReady())return false;
      if(!twinSessionValid())return false;
      const auto latest=controllerReadings();
      return latest.statusAt!=before.statusAt&&twinParticipantIdle(latest,controller.address,millis());
    });
  };
  auto finish=[&](const char* success,const char* needsMode){
    if(!sameSession())return false;
    if(previousMode==0){message(success);return true;}
    const uint32_t afterTeardown=millis();
    const bool ready=waitTwin(3000,[&]{
      return controllerReady()&&twinModeRestoreReady(controllerReadings(),controller.address,peer.address,
                                                   create,afterTeardown,millis());
    });
    voltra::Frame frame;
    const auto before=controllerReadings();
    const bool sent=ready&&controllerReady()&&
      twinModeRestoreReady(before,controller.address,peer.address,create,afterTeardown,millis())&&
      voltra::modeFrame(previousMode,frame)&&send(frame);
    const char* restoreIssue=twinSessionIssue();
    if(Serial.availableForWrite()>=160)Serial.printf("[native-twin] restore mode=%d ready=%d sent=%d guard=%s elapsedMs=%lu\n",
      previousMode,ready,sent,restoreIssue?restoreIssue:"valid",static_cast<unsigned long>(millis()-transactionStarted));
    if(!sent){
      if(sameSession())message(needsMode);return true;
    }
    const bool restored=waitTwin(3000,[&]{
      const auto latest=controllerReadings();
      return latest.statusAt!=before.statusAt&&latest.mode==previousMode&&
        twinParticipantUnloaded(latest,controller.address,millis());
    });
    if(Serial.availableForWrite()>=120)Serial.printf("[native-twin] restore confirmed=%d mode=%d elapsedMs=%lu\n",
      restored,controllerReadings().mode,static_cast<unsigned long>(millis()-transactionStarted));
    if(sameSession())message(restored?success:needsMode);return true;
  };
  if(!create){
    if(!controllerIdle({}))return fail("Finish training before exiting Twin");
    const auto before=controllerReadings();
    if(!controllerReady()||!twinParticipantIdle(before,controller.address,millis())||
       !before.endpoint.paired()||strcasecmp(before.endpoint.peer,s.endpoint.peer))
      return fail("Twin connection changed; try again");
    uint8_t bytes[20];voltra::Frame frame;
    if(!twinSessionValid()||!voltra::twinFrame(2,s.endpoint.peer,sequence,bytes,frame)||!send(frame))
      return fail("Exit Twin could not be sent");
    const bool exited=waitTwin(15000,[&]{
      bool ack;{Lock lock;ack=twinAcknowledged;}
      return twinExitConfirmed(ack,controllerReadings(),controller.address,before.endpointAt,millis());
    });
    if(!exited)return fail("Exit Twin not confirmed; check trainers");
    return finish("Twin exited","Twin exited; choose training mode");
  }
  NimBLEDevice::getScan()->stop();
  auto* connection=NimBLEDevice::createClient();
  if(!connection)return fail("No Bluetooth slot for second trainer");
  connection->setConnectTimeout(5000);
  auto cleanup=[&]{
    {Lock lock;peerUpdates=nullptr;peerFrames.reset();}
    // disconnect() and deleteClient() only schedule termination in NimBLE.
    // Let NimBLE delete its own client after the disconnect callback. Waiting on
    // the captured host handle avoids dereferencing a client being torn down.
    const uint16_t peerHandle=connection->getConnHandle();
    const bool deletionRequested=NimBLEDevice::deleteClient(connection);
    connection=nullptr;
    auto peerConnected=[&]{
      ble_gap_conn_desc description{};
      return peerHandle!=BLE_HS_CONN_HANDLE_NONE&&ble_gap_conn_find(peerHandle,&description)==0;
    };
    const uint32_t started=millis();uint32_t poll=0;
    while(peerConnected()&&millis()-started<3000){
      if(twinSessionValid()&&(!poll||millis()-poll>=500)){poll=millis();pollTwinController();}
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    const bool released=deletionRequested&&!peerConnected();
    if(Serial.availableForWrite()>=120)Serial.printf("[native-twin] peer-release confirmed=%d elapsedMs=%lu\n",
      released,static_cast<unsigned long>(millis()-started));
    return released;
  };
  bool connected=twinSessionValid()&&connection->connect(NimBLEAddress(std::string(peer.address),peer.addressType));
  // An offered peer may never have advertised to this knob. Try both BLE address
  // types for the exact MAC only; auth/init/query frames below are read-only.
  // No mode or pair write is allowed until live identity and unloaded readbacks.
  if(!connected&&command.handoff&&!command.addressTypeKnown&&twinSessionValid()){
    peer.addressType=peer.addressType?0:1;
    connected=connection->connect(NimBLEAddress(std::string(peer.address),peer.addressType));
  }
  if(!connected){cleanup();return fail("Second trainer unavailable; close other app");}
  if(Serial.availableForWrite()>=120)Serial.printf("[native-twin] peer-connected mtu=%u elapsedMs=%lu handoff=%d\n",
    connection->getMTU(),static_cast<unsigned long>(millis()-transactionStarted),command.handoff);
  auto* service=connection->getService(voltra::SERVICE_UUID);
  auto* writes=service?service->getCharacteristic(voltra::WRITE_CHAR_UUID):nullptr;
  auto* events=service?service->getCharacteristic(voltra::NOTIFY_CHAR_UUID):nullptr;
  {Lock lock;peerFrames.reset();peerUpdates=events;}
  const uint32_t session=workerSession;
  if(!writes||!events||!events->subscribe(true,[session,sequence](NimBLERemoteCharacteristic* source,uint8_t* bytes,size_t size,bool){
    peerNotify(session,sequence,source,bytes,size);
  })){cleanup();return fail("Second trainer service unavailable");}
  auto writePeer=[&](voltra::Frame frame,const char* stage){
    const char* reason=twinSessionIssue();
    if(!reason&&!connection->isConnected())reason="peer-disconnected";
    if(!reason&&writes->writeValue(frame.data,frame.len,writes->canWrite()))return true;
    if(!reason)reason="gatt-write-failed";
    TvTwinOffer authorization;{Lock lock;authorization=tvTwinOffer;}
    if(Serial.availableForWrite()>=256)Serial.printf(
      "[native-twin] peer-write stage=%s reason=%s error=%d mtu=%u elapsedMs=%lu leaseMs=%ld revision=%lu/%lu\n",
      stage,reason,connection->getLastError(),connection->getMTU(),static_cast<unsigned long>(millis()-transactionStarted),
      static_cast<long>(static_cast<int32_t>(authorization.deadline-millis())),
      static_cast<unsigned long>(workerHandoffRevision),static_cast<unsigned long>(authorization.revision));
    return false;
  };
  if(!writePeer(voltra::authFrame(),"auth")){cleanup();return fail("Second trainer handshake failed");}
  message("Accept on second VOLTRA if prompted");
  // Keep controller status fresh during the peer's read-only handshake.
  waitTwin(3000,[]{return false;});
  for(size_t i=0;i<voltra::INIT_COUNT;++i){
    char stage[16];snprintf(stage,sizeof(stage),"init-%u",static_cast<unsigned>(i));
    if(!writePeer(voltra::initFrame(i),stage)){cleanup();return fail("Second trainer handshake failed");}
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  uint32_t poll=0;
  auto pollPeer=[&]{
    if(!poll||millis()-poll>=500){poll=millis();uint8_t bytes[13];writePeer(voltra::endpointFrame(bytes),"endpoint");writePeer(voltra::readSettingsFrame(),"settings");}
  };
  const bool peerReady=waitTwin(8000,[&]{
    pollPeer();const auto latest=readPeer();
    return latest.mode>=0&&twinParticipantFresh(latest,peer.address,millis());
  });
  auto peerState=readPeer();
  if(!peerReady){cleanup();return fail("Second trainer state unavailable");}
  if(!peerState.endpoint.independent()){cleanup();return fail("Second trainer already in another Twin");}
  if(peerState.mode!=0&&!voltra::supportedMode(peerState.mode)){
    cleanup();return fail("Select a supported mode on second trainer");
  }
  if(!twinParticipantIndependent(peerState,peer.address,millis())){cleanup();return fail("Unload second trainer before Twin");}
  // Both live identities and unloaded states are now known. Entering idle is
  // necessary for native pairing, but must never be used as an implicit unload.
  if(!controllerReady()||!twinParticipantIndependent(controllerReadings(),controller.address,millis())){
    cleanup();return fail("Unload connected trainer before Twin");
  }
  message("Preparing both trainers for Twin");
  if(!controllerIdle([&]{
    pollPeer();
    return twinSessionValid()&&twinParticipantIndependent(readPeer(),peer.address,millis());
  })){cleanup();return fail("Trainer state changed while entering idle");}
  pollPeer();peerState=readPeer();
  if(!twinParticipantIndependent(peerState,peer.address,millis())){cleanup();return fail("Second trainer state changed; try again");}
  if(peerState.mode!=0){
    const uint32_t previousStatus=peerState.statusAt;
    if(!writePeer(voltra::modeIdleFrame(),"idle")){cleanup();return fail("Second trainer did not enter idle");}
    const bool idle=waitTwin(3000,[&]{
      pollPeer();const auto latest=readPeer();
      return latest.statusAt!=previousStatus&&twinParticipantIndependent(latest,peer.address,millis())&&latest.mode==0;
    });
    if(!idle){cleanup();return fail("Second trainer did not enter idle");}
  }
  const auto before=controllerReadings();peerState=readPeer();
  if(!controllerReady()||!twinParticipantIndependent(before,controller.address,millis())||before.mode!=0||
     !twinParticipantIndependent(peerState,peer.address,millis())||peerState.mode!=0){
    cleanup();return fail("Trainer state changed; Twin cancelled");
  }
  uint8_t bytes[20];voltra::Frame frame;
  // Verified direction: future peer receives future controller's address.
  message("Creating Twin");
  const bool sent=voltra::twinFrame(1,controller.address,sequence,bytes,frame)&&writePeer(frame,"create");
  const bool paired=sent&&waitTwin(15000,[&]{
    return twinCreateConfirmed(readPeer().ack,controllerReadings(),controller.address,peer.address,before.endpointAt,millis());
  });
  const bool peerReleased=cleanup();
  if(!paired)return fail("Twin not confirmed; check both trainers");
  if(command.handoff&&sameSession()){
    Lock lock;current.trainers[1-current.selected]=peer;settingsDirty=true;
  }
  if(!peerReleased){message("Twin connected; choose training mode");return true;}
  return finish("Twin connected","Twin connected; choose training mode");
}

void pump(){
  uint32_t lastWeightPoll=0;
  uint32_t lastPoll=0;int configCursor=0;
  while(sameSession()&&!disconnected&&client->isConnected()){
    workerTvEpoch=0;workerTvDeadline=0;workerHandoffRevision=0;workerHandoffPeer[0]=0;
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
        const bool success=performTwin(command.value==1,command.expectedPeer,command);
        const auto completed=voltraLink.state();
        if(Serial.availableForWrite()>=140)Serial.printf("[native-twin] confirmed=%d mode=%d message=%s\n",success,completed.mode,completed.message);
        {Lock lock;expectedTwinOperation=0;twinDeadline=0;
          if(current.session==command.session){current.twinPending=false;current.controlBusyUntil=0;
            current.twinSucceeded=success;++current.twinCompleted;}}
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
void VoltraLink::updateTvTwinOffer(uint32_t epoch,uint32_t session,uint32_t deadline,const char* peer,bool ready){
 Lock lock;const char* address=peer?peer:"";
 if(tvTwinOffer.epoch!=epoch||tvTwinOffer.session!=session||tvTwinOffer.ready!=ready||strcasecmp(tvTwinOffer.peer,address)){
   ++tvTwinOffer.revision;if(!tvTwinOffer.revision)++tvTwinOffer.revision;
 }
 tvTwinOffer.epoch=epoch;tvTwinOffer.session=session;tvTwinOffer.deadline=deadline;tvTwinOffer.ready=ready;
 strlcpy(tvTwinOffer.peer,address,sizeof(tvTwinOffer.peer));
}
bool VoltraLink::requestTwin(bool create,uint32_t epoch,uint32_t session,uint32_t deadline,const char* expectedPeer,bool handoff){
 Lock lock;const uint32_t now=millis();
 if(epoch&&(epoch!=tvEpoch.load()||session!=current.session||static_cast<int32_t>(deadline-now)<=0||static_cast<int32_t>(deadline-now)>2000))return false;
 if(uxQueueMessagesWaiting(queue))return false;
 if(handoff){
   if(!create||!canHandoffTwin(current,tvTwinOffer,epoch,expectedPeer,now))return false;
 }else{
   if(!canChangeTwin(current,now,create))return false;
   if(create&&expectedPeer&&expectedPeer[0]){
     const auto* candidate=nearbyTwinPeer(current,now);
     if(!candidate||strcasecmp(expectedPeer,candidate->address))return false;
   }
   if(create&&!current.trainers[1-current.selected].address[0]){
     const auto* candidate=nearbyTwinPeer(current,now);if(!candidate)return false;
     current.trainers[1-current.selected]=*candidate;settingsDirty=true;
   }
 }
 Command c{CommandType::Twin,Parameter::Weight,create?1:2,current.session,epoch,deadline};
 if(create&&expectedPeer)strlcpy(c.expectedPeer,expectedPeer,sizeof(c.expectedPeer));
 if(handoff){
   c.handoff=true;c.handoffRevision=tvTwinOffer.revision;
   for(const auto& saved:current.trainers)if(!strcasecmp(saved.address,expectedPeer)){c.handoffPeer=saved;c.addressTypeKnown=true;}
   for(const auto& nearby:current.nearby)if(!strcasecmp(nearby.address,expectedPeer)){c.handoffPeer=nearby;c.addressTypeKnown=true;}
   if(!c.addressTypeKnown){strlcpy(c.handoffPeer.address,expectedPeer,sizeof(c.handoffPeer.address));strlcpy(c.handoffPeer.name,"VOLTRA peer",sizeof(c.handoffPeer.name));}
 }
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
