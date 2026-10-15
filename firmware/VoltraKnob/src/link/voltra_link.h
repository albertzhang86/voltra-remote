#pragma once
#include <cstdint>
#include "../protocol/mode_parameters.h"
#include "../protocol/twin_protocol.h"
namespace vlink {
enum class LinkState:uint8_t {Scanning,Connecting,Handshaking,Ready};
enum class UnloadSource:uint8_t {Unknown,Touch,Voice};
enum class Parameter:uint8_t {Weight,Chains,Eccentric,InverseChains};
struct Trainer { char address[18]{}; char name[32]{}; uint8_t addressType=0; bool seen=false; uint32_t lastSeenMs=0; };
constexpr int NEARBY_TRAINER_LIMIT=8;
struct DeviceState {
  // RAM diagnostics survive connection changes, but reset on knob reboot.
  uint32_t unloadTouchRequests=0,unloadVoiceRequests=0,unloadOtherRequests=0;
  uint32_t lastUnloadRequestMs=0,lastUnloadRequestSession=0,stopFramesSent=0,lastStopFrameMs=0;
  int modeValues[voltra::MODE_PARAMETER_COUNT]{};
  uint32_t modeValueMs[voltra::MODE_PARAMETER_COUNT]{};
  LinkState link=LinkState::Scanning;
  Trainer trainers[2];
  Trainer nearby[NEARBY_TRAINER_LIMIT];
  int selected=-1;
  // TV pairing/connection pauses discovery to avoid a controller scheduler crash.
  bool discoveryPausedForTv=false;
  uint32_t session=0;
  int weightLbs=-1, chainsLbs=-1, eccentricLbs=-1000;
  int inverseChainsEnabled=-1;
  voltra::EndpointStatus endpoint;
  uint32_t endpointMs=0;
  bool twinPending=false;
  uint32_t twinCompleted=0;
  bool twinSucceeded=false;
  uint32_t twinMs=0,twinRoleMs=0;
  int twinConnection=-1,twinLastRole=-1,twinMainForce=-1,twinSubForce=-1,twinTotalForce=-1;
  int cableScreenId=-1;
  int lengthUnitRaw=-1,cableCm=-1,cableOffsetCm=-1;
  uint32_t cableMs=0,cableSavedSeq=0;
  bool cableActive=false;
  bool parameterSelectionPending=false;
  int weightUnitRaw=-1;
  int mode=-1, fitnessMode=-1, battery=-1, reps=0;
  int activeWeightTenths=-1;
  uint32_t forceMs=0,repMs=0;
  int targetForceTenths=-1;
  uint32_t targetForceMs=0;
  int runtimeWeightLbs=-1;
  uint32_t runtimeWeightMs=0;
  bool loaded=false, armed=false;
  uint32_t controlBusyUntil=0;
  uint32_t settingsMs=0, statusMs=0, confirmSeq=0;
  char message[48]="Choose a trainer";
};
inline int chainWeight(const DeviceState& s,bool inverse){
 if(s.inverseChainsEnabled<0||s.chainsLbs<0)return -1;
 return s.inverseChainsEnabled==(inverse?1:0)?s.chainsLbs:0;
}
inline bool fresh(const DeviceState& s,uint32_t now) {
  return s.link==LinkState::Ready && s.statusMs && now-s.statusMs<3500;
}
// Roles come from the verified endpoint identity, never connection order.
inline bool endpointFresh(const DeviceState& s,uint32_t now){return s.endpointMs&&now-s.endpointMs<3500&&s.endpoint.known();}
// Measured tension remains diagnostic data. The dashboard uses only device
// target resistance, or zero when the device explicitly reports unloaded.
enum class ActiveWeightSource:uint8_t {Unavailable,TargetForce,Unloaded};
struct ActiveWeightReading {int tenthsLb=-1;ActiveWeightSource source=ActiveWeightSource::Unavailable;};
inline ActiveWeightReading activeWeightReading(const DeviceState& s,uint32_t now){
 if(!fresh(s,now)||!endpointFresh(s,now)||s.twinPending||!s.endpoint.independent()||!voltra::supportedMode(s.mode))return {};
 if(s.fitnessMode==4&&!s.loaded&&!s.armed)return {0,ActiveWeightSource::Unloaded};
 if(!s.loaded||s.armed||!voltra::fitnessModeLoaded(s.fitnessMode))return {};
 if(s.targetForceMs&&now-s.targetForceMs<2000&&s.targetForceTenths>=0&&s.targetForceTenths<=10000)
   return {s.targetForceTenths,ActiveWeightSource::TargetForce};
 return {};
}
inline void clearWeightTelemetry(DeviceState& s){
 s.activeWeightTenths=-1;s.forceMs=0;
 s.targetForceTenths=-1;s.targetForceMs=0;
 s.runtimeWeightLbs=-1;s.runtimeWeightMs=0;
}
inline void updateWeightTelemetry(DeviceState& s,const voltra::Event& e,uint32_t now){
 if(s.endpoint.paired())return;
 if(e.type==voltra::EventType::Telemetry){
   const bool valid=e.activeWeightTenths>=0&&e.activeWeightTenths<=10000;
   s.activeWeightTenths=valid?e.activeWeightTenths:-1;s.forceMs=valid?now:0;
 }
 if(e.hasTargetForceReport){
   const bool valid=e.targetForceTenths>=0&&e.targetForceTenths<=10000;
   s.targetForceTenths=valid?e.targetForceTenths:-1;s.targetForceMs=valid?now:0;
 }
 if(e.hasRuntimeWeight){
   const bool valid=e.runtimeWeightLbs>=0&&e.runtimeWeightLbs<=1000;
   s.runtimeWeightLbs=valid?e.runtimeWeightLbs:-1;s.runtimeWeightMs=valid?now:0;
 }
}
inline bool twinActive(const DeviceState& s,uint32_t now){return fresh(s,now)&&endpointFresh(s,now)&&s.endpoint.paired();}
inline bool twinControlReady(const DeviceState& s,uint32_t now){return !s.twinPending&&endpointFresh(s,now);}
inline bool selectedFollower(const DeviceState&,uint32_t){return false;}
inline bool pairedFollower(const DeviceState& s,int slot,uint32_t now){return twinActive(s,now)&&slot>=0&&slot<2&&!strcmp(s.trainers[slot].address,s.endpoint.peer);}
inline bool canEdit(const DeviceState& s,uint32_t now) {
  return twinControlReady(s,now)&&!s.cableActive && fresh(s,now) && s.mode==1 && s.fitnessMode==4 && !s.loaded && !s.armed;
}
inline bool canEditParameter(const DeviceState& s,uint32_t now,Parameter parameter) {
 if(!twinControlReady(s,now)||(s.twinConnection==2&&!twinActive(s,now)))return false;
 if(canEdit(s,now))return true;
 return !s.cableActive && parameter==Parameter::Weight && fresh(s,now) && s.mode==1
   && s.fitnessMode==5 && s.loaded && !s.armed;
}
inline bool canEditBand(const DeviceState& s,uint32_t now){
 return twinControlReady(s,now)&&fresh(s,now)&&s.mode==2&&s.fitnessMode==4&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[0]&&now-s.modeValueMs[0]<8000&&(s.weightUnitRaw==0||s.weightUnitRaw==1);
}
inline bool canEditDamper(const DeviceState& s,uint32_t now){
 return twinControlReady(s,now)&&fresh(s,now)&&s.mode==4&&s.fitnessMode==4&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[5]&&now-s.modeValueMs[5]<5000&&s.modeValues[5]>=0&&s.modeValues[5]<=9;
}
inline bool canEditIsokinetic(const DeviceState& s,uint32_t now,int index){
 if(index<7||index>11)return false;
 return twinControlReady(s,now)&&fresh(s,now)&&s.mode==7&&s.fitnessMode==4&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[index]&&now-s.modeValueMs[index]<8000&&voltra::validIsokineticSetting(index,s.modeValues[index])
  &&(index<10||(s.weightUnitRaw==0||s.weightUnitRaw==1))
  &&(index<9||(s.modeValueMs[8]&&now-s.modeValueMs[8]<8000&&(s.modeValues[8]==0||s.modeValues[8]==1)))
  &&(index!=9||s.modeValues[8]==0)&&(index!=10||s.modeValues[8]==1);
}
inline bool canSwitch(const DeviceState& s,uint32_t now) {
  if(s.cableActive||s.twinPending)return false;
  if(s.controlBusyUntil && static_cast<int32_t>(s.controlBusyUntil-now)>0)return false;
  if(s.link==LinkState::Ready)return fresh(s,now)&&(s.fitnessMode==0||s.fitnessMode==4);
  return true;
}
inline bool canLoad(const DeviceState& s,uint32_t now) {
  if(!twinControlReady(s,now)||(s.twinConnection==2&&!twinActive(s,now))||s.parameterSelectionPending)return false;
  if(s.mode==2)return canEditBand(s,now)&&s.modeValues[0]>=15&&s.modeValues[0]<=200;
  if(s.mode==4)return canEditDamper(s,now);
  if(s.mode==7)return canEditIsokinetic(s,now,7);
  return canEdit(s,now)&&s.weightLbs>=5&&s.chainsLbs>=0&&s.eccentricLbs>=-195
    && s.settingsMs && now-s.settingsMs<3500;
}
inline bool trainerAvailable(const DeviceState& s,int slot,uint32_t now){
 if(slot<0||slot>=2||!s.trainers[slot].address[0])return false;
 if(s.selected==slot&&s.link==LinkState::Ready)return true;
 const auto& t=s.trainers[slot];
 // A saved identity remains selectable across reboot while scanning is paused.
 // Connection and fresh endpoint readback still verify availability before control.
 return s.discoveryPausedForTv||(t.seen&&uint32_t(now-t.lastSeenMs)<10000);
}
inline bool nearbyAdvertisement(const Trainer& trainer,uint32_t now){
 return trainer.address[0]&&trainer.seen&&uint32_t(now-trainer.lastSeenMs)<10000;
}
inline bool trainerNearby(const DeviceState& s,int slot,uint32_t now){
 if(slot<0||slot>=2||!s.trainers[slot].address[0])return false;
 return (s.selected==slot&&fresh(s,now))||nearbyAdvertisement(s.trainers[slot],now);
}
inline const Trainer* nearbyTwinPeer(const DeviceState& s,uint32_t now){
 if(s.selected<0||s.selected>=2||!fresh(s,now)||!s.trainers[s.selected].address[0])return nullptr;
 const auto& other=s.trainers[1-s.selected];const char* selected=s.trainers[s.selected].address;
 const Trainer* candidate=nullptr;bool ambiguous=false;
 auto consider=[&](const Trainer& trainer){
   if(!trainer.address[0]||!strcasecmp(trainer.address,selected))return;
   if(candidate&&strcasecmp(candidate->address,trainer.address))ambiguous=true;
   else if(!candidate)candidate=&trainer;
 };
 if(trainerNearby(s,1-s.selected,now))consider(other);
 for(const auto& nearby:s.nearby)if(nearbyAdvertisement(nearby,now))consider(nearby);
 if(ambiguous||!candidate)return nullptr;
 // Never replace a remembered device implicitly to create a new pair.
 if(other.address[0]&&strcasecmp(other.address,candidate->address))return nullptr;
 return candidate;
}
inline int nearbySlot(const DeviceState& s,const char* address,uint32_t now){
 int vacant=-1,oldest=-1;uint32_t oldestAge=0;
 for(int i=0;i<NEARBY_TRAINER_LIMIT;++i){
   const auto& t=s.nearby[i];
   if(!strcasecmp(t.address,address))return i;
   if(!t.address[0]&&vacant<0)vacant=i;
   const uint32_t age=now-t.lastSeenMs;
   if(!nearbyAdvertisement(t,now)&&(oldest<0||age>oldestAge)){oldest=i;oldestAge=age;}
 }
 return vacant>=0?vacant:oldest;
}
inline bool canForgetTrainer(const DeviceState& s,int slot,uint32_t now){
 if(slot<0||slot>=2||!s.trainers[slot].address[0])return false;
 return canSwitch(s,now)&&!s.loaded&&!s.armed&&!s.parameterSelectionPending;
}
inline bool canChangeTwin(const DeviceState& s,uint32_t now,bool create){
 if(s.twinPending||s.parameterSelectionPending||s.cableActive||!fresh(s,now)||!endpointFresh(s,now)||s.mode!=0||!voltra::fitnessModeUnloaded(s.fitnessMode))return false;
 if(s.controlBusyUntil&&static_cast<int32_t>(s.controlBusyUntil-now)>0)return false;
 if(!create)return s.endpoint.paired();
 return s.endpoint.independent()&&nearbyTwinPeer(s,now);
}
class VoltraLink {
 public:
  void begin();
  // Start scanning only after the local TV GATT service has been registered.
  void start();
  DeviceState state();
  bool selectTrainer(int slot);
  bool selectNearbyTrainer(const char* address);
  bool forgetTrainer(int slot,const char* expectedAddress);
  void rediscoverTrainers();
  void setDiscoveryVisible(bool visible);
  bool setTvRadioActive(bool active);
  bool requestSetting(Parameter parameter,int value);
  // TV commands cannot load/unload, select a trainer, or bypass local gates.
  void setTvSession(uint32_t epoch);
  bool requestTwin(bool create,uint32_t epoch=0,uint32_t session=UINT32_MAX,uint32_t deadline=0);
  bool requestTvSetting(uint32_t epoch,uint32_t session,uint32_t deadline,bool mode,uint16_t field,int value);
  bool requestBandMaximum(int pounds);
  bool requestDamperLevel(int index);
  bool requestIsokinetic(int index,int value);
  bool requestParameter(Parameter parameter);
  bool requestCable(bool open);
  bool requestLoad(uint32_t expectedSession=UINT32_MAX); bool requestUnload(uint32_t expectedSession=UINT32_MAX,UnloadSource source=UnloadSource::Unknown); bool requestWeightMode(); bool requestMode(int mode);
};
extern VoltraLink voltraLink;
}
