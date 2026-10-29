#pragma once
#include <cstdint>
#include <cstring>
#include "../protocol/mode_parameters.h"
#include "../protocol/twin_protocol.h"
namespace vlink {
enum class LinkState:uint8_t {Scanning,Connecting,Handshaking,Ready};
enum class UnloadSource:uint8_t {Unknown,Touch,Voice};
enum class Parameter:uint8_t {Weight,Chains,Eccentric,InverseChains};
enum class MotorFailure:uint16_t {None=0,State=1,CableNotZero=2,EndpointChanged=3,Revoked=4,WriteFailed=5,NotConfirmed=6,CableUnknown=7,ConfigurationChanged=8,ConfigurationUnknown=9};
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
  uint32_t weightSettingMs[4]{}; // Base, chains, inverse flag, eccentric.
  voltra::EndpointStatus endpoint;
  uint32_t endpointMs=0;
  bool twinPending=false;
  bool twinCreating=false;
  uint32_t twinCompleted=0;
  bool twinSucceeded=false;
  uint32_t twinMs=0,twinRoleMs=0;
  int twinConnection=-1,twinLastRole=-1,twinMainForce=-1,twinSubForce=-1,twinTotalForce=-1;
  int cableScreenId=-1;
  int lengthUnitRaw=-1,cableCm=-1,cableOffsetCm=-1;
  uint32_t cableMs=0,cableOffsetMs=0,cableSavedSeq=0;
  bool cableActive=false;
  bool parameterSelectionPending=false;
  int weightUnitRaw=-1;
  int maximumForceLbs=-1;
  int mode=-1, fitnessMode=-1, battery=-1, reps=0;
  // Mode selection and motor state are independent device reports. A motor
  // notification cannot confirm a mode request (including on Firmware 2.0).
  uint32_t modeMs=0,modeRevision=0;
  int activeWeightTenths=-1;
  uint32_t forceMs=0,repMs=0;
  int cableLengthMm=-1,cableVelocityMmPerSecond=0;
  uint32_t motionMs=0;
  int pairForceTenths=-1;
  uint32_t pairMotionMs=0;
  bool pairMoving=false;
  int targetForceTenths=-1;
  uint32_t targetForceMs=0;
  int runtimeWeightLbs=-1;
  uint32_t runtimeWeightMs=0;
  bool loaded=false, armed=false;
  uint32_t controlBusyUntil=0;
  uint32_t settingsMs=0, statusMs=0, confirmSeq=0;
  uint32_t motorCompleted=0;
  bool motorPending=false;
  bool motorSucceeded=false;
  MotorFailure motorFailure=MotorFailure::None;
  char message[48]="Choose a trainer";
};
inline int chainWeight(const DeviceState& s,bool inverse){
 if(s.inverseChainsEnabled<0||s.chainsLbs<0)return -1;
 return s.inverseChainsEnabled==(inverse?1:0)?s.chainsLbs:0;
}
inline bool fresh(const DeviceState& s,uint32_t now) {
  return s.link==LinkState::Ready && s.statusMs && now-s.statusMs<3500;
}
inline bool modeReportFresh(const DeviceState& s,uint32_t now) {
  return s.link==LinkState::Ready && s.modeMs && now-s.modeMs<3500;
}
inline bool modeConfirmed(const DeviceState& s,uint32_t now) {
  return modeReportFresh(s,now) && voltra::supportedMode(s.mode);
}
// Roles come from the verified endpoint identity, never connection order.
inline bool endpointFresh(const DeviceState& s,uint32_t now){return s.endpointMs&&now-s.endpointMs<3500&&s.endpoint.known();}
// Measured tension remains diagnostic data. The dashboard uses only device
// target resistance, or zero when the device explicitly reports unloaded.
enum class ActiveWeightSource:uint8_t {Unavailable,TargetForce,Unloaded,CableForce};
struct ActiveWeightReading {int tenthsLb=-1;ActiveWeightSource source=ActiveWeightSource::Unavailable;};
inline ActiveWeightReading activeWeightReading(const DeviceState& s,uint32_t now){
 if(!fresh(s,now)||!endpointFresh(s,now)||s.twinPending||!s.endpoint.independent()||!voltra::supportedMode(s.mode))return {};
 if(voltra::fitnessModeReady(s.fitnessMode)&&!s.loaded&&!s.armed)return {0,ActiveWeightSource::Unloaded};
 if(!s.loaded||s.armed||!voltra::fitnessModeLoaded(s.fitnessMode))return {};
 if(s.targetForceMs&&now-s.targetForceMs<2000&&s.targetForceTenths>=0&&s.targetForceTenths<=10000)
   return {s.targetForceTenths,ActiveWeightSource::TargetForce};
 return {};
}
// Display measured force only with contemporaneous movement. A stationary
// cable can retain tension, so neither force > 0 nor an extended cable proves
// movement. 10 mm/s rejects near-zero speed jitter; freshness is bounded to 0.5 s.
inline ActiveWeightReading movingWeightReading(const DeviceState& s,uint32_t now){
 if(!fresh(s,now)||!endpointFresh(s,now)||s.twinPending||!voltra::supportedMode(s.mode))return {};
 if(!s.loaded||s.armed||!voltra::fitnessModeLoaded(s.fitnessMode))return {};
 if(s.endpoint.paired()){
   if(!s.pairMotionMs||now-s.pairMotionMs>=500||!s.pairMoving||s.pairForceTenths<0)return {};
   return {s.pairForceTenths,ActiveWeightSource::CableForce};
 }
 if(!s.endpoint.independent())return {};
 if(!s.motionMs||s.motionMs!=s.forceMs||now-s.motionMs>=500)return {};
 if(s.cableVelocityMmPerSecond>-10&&s.cableVelocityMmPerSecond<10)return {};
 if(s.activeWeightTenths<0||s.activeWeightTenths>10000)return {};
 return {s.activeWeightTenths,ActiveWeightSource::CableForce};
}
inline void clearWeightTelemetry(DeviceState& s){
 s.activeWeightTenths=-1;s.forceMs=0;
 s.cableLengthMm=-1;s.cableVelocityMmPerSecond=0;s.motionMs=0;
 s.pairForceTenths=-1;s.pairMotionMs=0;s.pairMoving=false;
 s.targetForceTenths=-1;s.targetForceMs=0;
 s.runtimeWeightLbs=-1;s.runtimeWeightMs=0;
}
inline void updateWeightTelemetry(DeviceState& s,const voltra::Event& e,uint32_t now){
 if(s.twinPending)return;
 if(s.endpoint.paired()){
   if(e.twinTelemetry&&e.hasTwinPairMotion){
     const bool valid=e.activeWeightTenths>=0&&e.peerForceTenths>=0&&
       e.activeWeightTenths<=10000&&e.peerForceTenths<=10000&&
       e.activeWeightTenths+e.peerForceTenths<=10000;
     s.pairForceTenths=valid?e.activeWeightTenths+e.peerForceTenths:-1;
     s.pairMotionMs=valid?now:0;
     s.pairMoving=valid&&(e.cableVelocityMmPerSecond<=-10||e.cableVelocityMmPerSecond>=10||
       e.peerCableVelocityMmPerSecond<=-10||e.peerCableVelocityMmPerSecond>=10);
   }
   return;
 }
 if(e.twinTelemetry)return;
 if(e.type==voltra::EventType::Telemetry){
   const bool valid=e.activeWeightTenths>=0&&e.activeWeightTenths<=10000;
   s.activeWeightTenths=valid?e.activeWeightTenths:-1;s.forceMs=valid?now:0;
   // A force-only legacy frame cannot borrow movement from a previous B4.
   s.motionMs=valid&&e.hasMotionTelemetry?now:0;
   s.cableLengthMm=s.motionMs?e.cableLengthMm:-1;
   s.cableVelocityMmPerSecond=s.motionMs?e.cableVelocityMmPerSecond:0;
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
inline void updateRepetitionTelemetry(DeviceState& s,const voltra::Event& e,uint32_t now){
 if(e.repCount<0||e.repCount>=65535||s.twinPending||!fresh(s,now)||!endpointFresh(s,now))return;
 if(s.endpoint.paired()!=e.twinTelemetry)return;
 if(!s.endpoint.paired()&&!s.endpoint.independent())return;
 s.reps=e.repCount;s.repMs=now;
}
inline int repetitionReading(const DeviceState& s,uint32_t now){
 if(!fresh(s,now)||!endpointFresh(s,now)||s.twinPending||!s.repMs)return -1;
 return s.endpoint.paired()||s.endpoint.independent()?s.reps:-1;
}
inline bool twinActive(const DeviceState& s,uint32_t now){return fresh(s,now)&&endpointFresh(s,now)&&s.endpoint.paired();}
inline bool twinControlReady(const DeviceState& s,uint32_t now){return !s.twinPending&&endpointFresh(s,now);}
// A revoked TV request must release its admission latch. Its pending flag bars
// a second selection/Twin request; queued local work retains its busy deadline.
inline void discardTvCommand(DeviceState& s,uint32_t commandSession,bool selecting,bool twin,bool queueEmpty){
 if(s.session!=commandSession)return;
 if(selecting)s.parameterSelectionPending=false;
 if(twin){s.twinPending=false;s.twinSucceeded=false;++s.twinCompleted;}
 if(queueEmpty)s.controlBusyUntil=0;
}
inline bool selectedFollower(const DeviceState&,uint32_t){return false;}
inline bool pairedFollower(const DeviceState& s,int slot,uint32_t now){return twinActive(s,now)&&slot>=0&&slot<2&&!strcmp(s.trainers[slot].address,s.endpoint.peer);}
// A fresh connection reads its own ceiling. Unsupported firmware retains the
// conservative legacy envelope, never a guessed upgrade or enabled Overdrive.
inline int maximumWeight(const DeviceState& s){return s.maximumForceLbs>=5&&s.maximumForceLbs<=250?s.maximumForceLbs:200;}
inline bool validWeightSetting(const DeviceState& s,Parameter parameter,int value){
 const int maximum=maximumWeight(s);
 if(parameter==Parameter::Weight)return value>=5&&value<=maximum&&s.eccentricLbs>=5-maximum&&value+s.eccentricLbs>=5&&value+s.eccentricLbs<=maximum;
 if(parameter==Parameter::Eccentric)return value>=5-maximum&&value<=maximum-5&&s.weightLbs>=5&&s.weightLbs+value>=5&&s.weightLbs+value<=maximum;
 return value>=0&&value<=100;
}
inline bool canEdit(const DeviceState& s,uint32_t now) {
  return !s.motorPending&&twinControlReady(s,now)&&!s.cableActive && fresh(s,now) && s.mode==1 && voltra::fitnessModeUnloaded(s.fitnessMode) && !s.loaded && !s.armed;
}
inline bool canEditParameter(const DeviceState& s,uint32_t now,Parameter parameter) {
 if(s.motorPending||!twinControlReady(s,now)||(s.twinConnection==2&&!twinActive(s,now)))return false;
 if(canEdit(s,now))return true;
 return !s.cableActive && parameter==Parameter::Weight && fresh(s,now) && s.mode==1
   && (s.fitnessMode==1||s.fitnessMode==5) && s.loaded && !s.armed;
}
inline bool canEditBand(const DeviceState& s,uint32_t now){
 return !s.motorPending&&twinControlReady(s,now)&&fresh(s,now)&&s.mode==2&&voltra::fitnessModeUnloaded(s.fitnessMode)&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[0]&&now-s.modeValueMs[0]<8000&&(s.weightUnitRaw==0||s.weightUnitRaw==1);
}
inline bool canEditDamper(const DeviceState& s,uint32_t now){
 return !s.motorPending&&twinControlReady(s,now)&&fresh(s,now)&&s.mode==4&&voltra::fitnessModeUnloaded(s.fitnessMode)&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[5]&&now-s.modeValueMs[5]<5000&&s.modeValues[5]>=0&&s.modeValues[5]<=9;
}
inline bool canEditIsokinetic(const DeviceState& s,uint32_t now,int index){
 if(index<7||index>11)return false;
 return !s.motorPending&&twinControlReady(s,now)&&fresh(s,now)&&s.mode==7&&voltra::fitnessModeUnloaded(s.fitnessMode)&&!s.loaded&&!s.armed&&!s.cableActive
  &&s.modeValueMs[index]&&now-s.modeValueMs[index]<8000&&voltra::validIsokineticSetting(index,s.modeValues[index])
  &&(index<10||(s.weightUnitRaw==0||s.weightUnitRaw==1))
  &&(index<9||(s.modeValueMs[8]&&now-s.modeValueMs[8]<8000&&(s.modeValues[8]==0||s.modeValues[8]==1)))
  &&(index!=9||s.modeValues[8]==0)&&(index!=10||s.modeValues[8]==1);
}
inline bool canSwitch(const DeviceState& s,uint32_t now) {
  if(s.loaded||s.armed||s.cableActive||s.twinPending||s.motorPending)return false;
  if(s.controlBusyUntil && static_cast<int32_t>(s.controlBusyUntil-now)>0)return false;
  if(s.link==LinkState::Ready)return fresh(s,now)&&voltra::fitnessModeUnloaded(s.fitnessMode);
  return true;
}
inline bool canLoad(const DeviceState& s,uint32_t now) {
  if(!modeConfirmed(s,now)||!voltra::fitnessModeReady(s.fitnessMode))return false;
  if(!twinControlReady(s,now)||(s.twinConnection==2&&!twinActive(s,now))||s.parameterSelectionPending)return false;
  if(s.mode==2)return canEditBand(s,now)&&s.modeValues[0]>=15&&s.modeValues[0]<=maximumWeight(s);
  if(s.mode==4)return canEditDamper(s,now);
  if(s.mode==7)return canEditIsokinetic(s,now,7);
  return canEdit(s,now)&&s.weightLbs>=5&&s.chainsLbs>=0&&s.eccentricLbs>=5-maximumWeight(s)
    && s.settingsMs && now-s.settingsMs<3500;
}
inline MotorFailure cableLoadFailure(const DeviceState& s,uint32_t now){
 if(!s.cableMs||!s.cableOffsetMs||now-s.cableMs>=1500||now-s.cableOffsetMs>=1500||
    s.cableCm<0||s.cableCm>32767||s.cableOffsetCm<0||s.cableOffsetCm>32767)return MotorFailure::CableUnknown;
 // Verified Cortex cable-zero test: signed current position minus the device's
 // saved zero must be below 2 cm. We never write or invent a custom zero.
 return s.cableCm-s.cableOffsetCm<2?MotorFailure::None:MotorFailure::CableNotZero;
}
struct MotorConfiguration {
 int mode=-1;
 bool valid=false;
 int values[voltra::MODE_PARAMETER_COUNT]{};
 uint32_t stamps[voltra::MODE_PARAMETER_COUNT]{};
};
inline bool motorConfigurationField(int mode,int index){
 return mode==1?index<4:voltra::MODE_PARAMETERS[index].mode==mode;
}
inline MotorConfiguration motorConfiguration(const DeviceState& s,uint32_t now){
 MotorConfiguration config;config.mode=s.mode;
 if(!voltra::supportedMode(s.mode))return config;
 if(s.mode==1){
   if(s.weightLbs<5||s.weightLbs>maximumWeight(s)||s.chainsLbs<0||s.chainsLbs>100||
      s.inverseChainsEnabled<0||s.inverseChainsEnabled>1||s.eccentricLbs<5-maximumWeight(s)||s.eccentricLbs>maximumWeight(s)-5||
      s.weightLbs+s.eccentricLbs<5||s.weightLbs+s.eccentricLbs>maximumWeight(s))return config;
   config.values[0]=s.weightLbs;config.values[1]=s.chainsLbs;config.values[2]=s.inverseChainsEnabled;config.values[3]=s.eccentricLbs;
   for(int i=0;i<4;++i)config.stamps[i]=s.weightSettingMs[i];
 }else for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i){
   config.values[i]=s.modeValues[i];config.stamps[i]=s.modeValueMs[i];
 }
 for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(motorConfigurationField(s.mode,i)){
   if(!config.stamps[i]||now-config.stamps[i]>=(s.mode==1?3500u:8000u))return config;
   if(s.mode!=1&&config.values[i]<0)return config;
 }
 config.valid=true;return config;
}
inline MotorFailure motorConfigurationFailure(const DeviceState& s,const MotorConfiguration& expected,
                                              uint32_t now,uint32_t queriedAt=0){
 const auto actual=motorConfiguration(s,now);
 if(!expected.valid||!actual.valid)return MotorFailure::ConfigurationUnknown;
 if(actual.mode!=expected.mode)return MotorFailure::ConfigurationChanged;
 for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(motorConfigurationField(actual.mode,i)){
   if(actual.values[i]!=expected.values[i])return MotorFailure::ConfigurationChanged;
   if(queriedAt&&(actual.stamps[i]==expected.stamps[i]||static_cast<int32_t>(actual.stamps[i]-queriedAt)<0))
     return MotorFailure::ConfigurationUnknown;
 }
 return MotorFailure::None;
}
inline bool motorEndpointMatches(const DeviceState& s,uint32_t session,int mode,const char* peer,uint32_t now){
 if(s.session!=session||s.mode!=mode||!fresh(s,now)||!endpointFresh(s,now)||s.selected<0||s.selected>=2||
    strcasecmp(s.endpoint.local,s.trainers[s.selected].address))return false;
 return peer&&peer[0]?s.endpoint.paired()&&!strcasecmp(s.endpoint.peer,peer):s.endpoint.independent();
}
// Admission to the read-only phase of an authenticated load request. Cable
// data may be absent or stale after a Twin transition; SETUP must refresh it
// before the separate commit gate can authorize GO.
inline MotorFailure tvMotorPreparationFailure(const DeviceState& s,bool load,uint32_t now){
 if(!fresh(s,now)||!twinControlReady(s,now)||s.parameterSelectionPending||s.cableActive||
    !voltra::supportedMode(s.mode))return MotorFailure::State;
 if(!motorEndpointMatches(s,s.session,s.mode,s.endpoint.paired()?s.endpoint.peer:"",now))return MotorFailure::EndpointChanged;
 if(load){auto ownRequest=s;ownRequest.motorPending=false;
   if(!canLoad(ownRequest,now))return MotorFailure::State;
   if(!motorConfiguration(s,now).valid)return MotorFailure::ConfigurationUnknown;
   return MotorFailure::None;}
 return voltra::fitnessModeUnloaded(s.fitnessMode)||voltra::fitnessModeLoaded(s.fitnessMode)||voltra::fitnessModeArmed(s.fitnessMode)?MotorFailure::None:MotorFailure::State;
}
inline MotorFailure tvMotorFailure(const DeviceState& s,bool load,uint32_t now){
 if(const auto failure=tvMotorPreparationFailure(s,load,now);failure!=MotorFailure::None)return failure;
 return load?cableLoadFailure(s,now):MotorFailure::None;
}
inline bool motorConfirmed(const DeviceState& s,bool loaded,uint32_t previousStatus,uint32_t writtenAt,uint32_t now){
 if(!fresh(s,now)||s.statusMs==previousStatus||static_cast<int32_t>(s.statusMs-writtenAt)<0)return false;
 return loaded?voltra::fitnessModeLoaded(s.fitnessMode)&&s.loaded&&!s.armed:
   voltra::fitnessModeUnloaded(s.fitnessMode)&&!s.loaded&&!s.armed;
}
inline bool motorSetupReadback(const DeviceState& s,uint32_t priorCable,uint32_t priorOffset,uint32_t queriedAt){
 return s.cableMs!=priorCable&&s.cableOffsetMs!=priorOffset&&
   static_cast<int32_t>(s.cableMs-queriedAt)>=0&&static_cast<int32_t>(s.cableOffsetMs-queriedAt)>=0;
}
// This is the load worker's final gate after SETUP and fresh configuration
// reads. Neither bridge admission nor a cached zero position can replace it.
inline MotorFailure motorLoadCommitFailure(const DeviceState& s,uint32_t session,int mode,const char* peer,
                                          const MotorConfiguration& expected,uint32_t priorCable,
                                          uint32_t priorOffset,uint32_t queriedAt,uint32_t now){
 if(!motorEndpointMatches(s,session,mode,peer,now))return MotorFailure::EndpointChanged;
 if(!motorSetupReadback(s,priorCable,priorOffset,queriedAt))return MotorFailure::CableUnknown;
 if(const auto failure=motorConfigurationFailure(s,expected,now,queriedAt);failure!=MotorFailure::None)return failure;
 return tvMotorFailure(s,true,now);
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
// Discovery and command authorization are separate. A TV connection deliberately
// pauses scanning, so a saved peer's advertisement will expire. Its exact saved
// identity may start the read-only handshake; only fresh GATT readbacks authorize
// unloading/mode/pair commands. Never label this saved candidate as nearby.
inline const Trainer* twinRequestPeer(const DeviceState& s,uint32_t now){
 if(const auto* nearby=nearbyTwinPeer(s,now))return nearby;
 if(!s.discoveryPausedForTv||s.selected<0||s.selected>=2||!fresh(s,now))return nullptr;
 const auto& controller=s.trainers[s.selected];
 const auto& saved=s.trainers[1-s.selected];
 if(!controller.address[0]||!saved.address[0]||!strcasecmp(controller.address,saved.address)||
    !strcmp(saved.address,"00:00:00:00:00:00"))return nullptr;
 // Preserve the ambiguity/occupied-slot check when there is live discovery
 // evidence. A saved identity must not override a different visible candidate.
 for(const auto& nearby:s.nearby)
   if(nearbyAdvertisement(nearby,now)&&strcasecmp(nearby.address,controller.address))return nullptr;
 return &saved;
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
// Admission permits an unloaded training screen. The transaction confirms idle
// on both identified trainers before issuing the native pairing command.
// Once the peer has a live GATT connection, its identity/readbacks replace the
// advertisement that necessarily disappears during that connection.
inline bool twinSetupReady(const DeviceState& s,uint32_t now){
 if(s.selected<0||s.selected>=2||!s.trainers[s.selected].address[0])return false;
 if(s.twinPending||s.motorPending||s.parameterSelectionPending||s.cableActive||s.loaded||s.armed||!fresh(s,now)||!endpointFresh(s,now))return false;
 if(strcasecmp(s.trainers[s.selected].address,s.endpoint.local))return false;
 if((s.mode!=0&&!voltra::supportedMode(s.mode))||!voltra::fitnessModeUnloaded(s.fitnessMode))return false;
 if(s.controlBusyUntil&&static_cast<int32_t>(s.controlBusyUntil-now)>0)return false;
 return true;
}
inline bool canChangeTwin(const DeviceState& s,uint32_t now,bool create){
 if(!twinSetupReady(s,now))return false;
 if(!create)return s.endpoint.paired();
 return s.endpoint.independent()&&nearbyTwinPeer(s,now);
}
// An explicit local or TV Twin action may first unload a known active trainer.
// This authorizes preparation; native topology writes still require twinSetupReady.
inline bool twinRequestReady(const DeviceState& s,uint32_t now){
 if(twinSetupReady(s,now))return true;
 if(!voltra::supportedMode(s.mode)||
    !(voltra::fitnessModeLoaded(s.fitnessMode)||voltra::fitnessModeArmed(s.fitnessMode)))return false;
 auto unloaded=s;unloaded.loaded=false;unloaded.armed=false;unloaded.fitnessMode=4;
 return twinSetupReady(unloaded,now);
}
inline bool canRequestTwin(const DeviceState& s,uint32_t now,bool create){
 if(!twinRequestReady(s,now))return false;
 return create?s.endpoint.independent()&&twinRequestPeer(s,now):s.endpoint.paired();
}
// Trusted TV ownership and expiring readiness are separate. Saved identities
// remain protected while the TV is busy. A TV-owned peer requires an explicit
// handoff; the saved-address check must not compete with its existing TV link.
struct TvTwinOffer {
 uint32_t epoch=0,session=0,deadline=0,revision=0;
 bool ready=false;
 char peer[18]{};
};
inline bool tvOwnsTwinPeer(const TvTwinOffer& offer,uint32_t epoch,uint32_t session,const char* peer){
 return epoch&&offer.epoch==epoch&&offer.session==session&&offer.peer[0]&&peer&&peer[0]&&
   !strcasecmp(offer.peer,peer);
}
inline bool tvTwinOfferMatches(const TvTwinOffer& offer,uint32_t epoch,uint32_t session,const char* peer){
 return offer.ready&&tvOwnsTwinPeer(offer,epoch,session,peer);
}
inline bool tvTwinAuthorization(const TvTwinOffer& offer,uint32_t epoch,uint32_t session,const char* peer,uint32_t now){
 return tvTwinOfferMatches(offer,epoch,session,peer)&&static_cast<int32_t>(offer.deadline-now)>0;
}
// A fresh offer admits one bounded transaction. Delayed heartbeat delivery must
// not cancel its read-only handshake; explicit revocation/identity changes still
// invalidate the accepted revision, and a refresh never extends this deadline.
inline bool tvTwinTransactionAuthorized(const TvTwinOffer& offer,uint32_t revision,uint32_t epoch,uint32_t session,
                                       const char* peer,uint32_t transactionDeadline,uint32_t now){
 return revision&&offer.revision==revision&&tvTwinOfferMatches(offer,epoch,session,peer)&&
   static_cast<int32_t>(transactionDeadline-now)>0;
}
inline bool canHandoffTwin(const DeviceState& s,const TvTwinOffer& offer,uint32_t epoch,const char* peer,uint32_t now){
 if(!twinSetupReady(s,now)||!s.endpoint.independent()||!tvTwinAuthorization(offer,epoch,s.session,peer,now)||
    !strcasecmp(peer,s.trainers[s.selected].address))return false;
 // A handoff may fill a free slot or reuse this peer, never replace another
 // saved trainer. Check before any handshake/topology change is admitted.
 const auto& savedPeer=s.trainers[1-s.selected];
 return !savedPeer.address[0]||!strcasecmp(savedPeer.address,peer);
}
// Admission for an explicit TV handoff may prepare a known active controller.
// Its exact trusted peer/slot checks are unchanged; the worker still confirms
// both participants unloaded before any native idle or pairing command.
inline bool canRequestHandoffTwin(const DeviceState& s,const TvTwinOffer& offer,uint32_t epoch,const char* peer,uint32_t now){
 if(!twinRequestReady(s,now))return false;
 auto unloaded=s;unloaded.loaded=false;unloaded.armed=false;unloaded.fitnessMode=4;
 return canHandoffTwin(unloaded,offer,epoch,peer,now);
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
  // TV commands are session-bound and cannot select a trainer or bypass local gates.
  void setTvSession(uint32_t epoch);
  bool requestTwin(bool create,uint32_t epoch=0,uint32_t session=UINT32_MAX,uint32_t deadline=0,const char* expectedPeer=nullptr,bool handoff=false);
  void updateTvTwinOffer(uint32_t epoch,uint32_t session,uint32_t deadline,const char* peer,bool ready);
  bool requestTvSetting(uint32_t epoch,uint32_t session,uint32_t deadline,bool mode,uint16_t field,int value);
  bool requestBandMaximum(int pounds);
  bool requestDamperLevel(int index);
  bool requestIsokinetic(int index,int value);
  bool requestParameter(Parameter parameter);
  bool requestCable(bool open);
  bool requestLoad(uint32_t expectedSession=UINT32_MAX); bool requestUnload(uint32_t expectedSession=UINT32_MAX,UnloadSource source=UnloadSource::Unknown); bool requestWeightMode(); bool requestMode(int mode);
  bool requestTvMotor(uint32_t epoch,uint32_t session,uint32_t deadline,bool load);
};
extern VoltraLink voltraLink;
}
