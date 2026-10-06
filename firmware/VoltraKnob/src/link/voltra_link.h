#pragma once
#include <cstdint>
#include "../protocol/mode_parameters.h"
namespace vlink {
enum class LinkState:uint8_t {Scanning,Connecting,Handshaking,Ready};
enum class Parameter:uint8_t {Weight,Chains,Eccentric,InverseChains};
struct Trainer { char address[18]{}; char name[32]{}; uint8_t addressType=0; bool seen=false; uint32_t lastSeenMs=0; };
struct DeviceState {
  int modeValues[voltra::MODE_PARAMETER_COUNT]{};
  uint32_t modeValueMs[voltra::MODE_PARAMETER_COUNT]{};
  LinkState link=LinkState::Scanning;
  Trainer trainers[2];
  int selected=-1;
  uint32_t session=0;
  int weightLbs=-1, chainsLbs=-1, eccentricLbs=-1000;
  int inverseChainsEnabled=-1;
  uint32_t twinMs=0,twinRoleMs=0;
  int twinConnection=-1,twinLastRole=-1,twinMainForce=-1,twinSubForce=-1,twinTotalForce=-1;
  int cableScreenId=-1;
  int lengthUnitRaw=-1,cableCm=-1,cableOffsetCm=-1;
  uint32_t cableMs=0,cableSavedSeq=0;
  bool cableActive=false;
  bool parameterSelectionPending=false;
  int weightUnitRaw=-1;
  int mode=-1, fitnessMode=-1, battery=-1, reps=0;
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
// Connection state indicates a paired link, not which endpoint controls it.
inline bool twinActive(const DeviceState& s,uint32_t now){
 return fresh(s,now)&&s.twinConnection==2&&s.twinMs&&now-s.twinMs<3500;
}
// This firmware does not return a usable role field. Never infer roles from
// connection order or LAST_COWORKER_ROLE, and never lock out the other trainer.
inline bool twinControlReady(const DeviceState& s,uint32_t now){return s.twinConnection!=2||twinActive(s,now);}
inline bool selectedFollower(const DeviceState&,uint32_t){return false;}
inline bool pairedFollower(const DeviceState&,int,uint32_t){return false;}
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
  if(s.cableActive)return false;
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
 const auto& t=s.trainers[slot];return t.seen&&uint32_t(now-t.lastSeenMs)<10000;
}
class VoltraLink {
 public:
  void begin(); DeviceState state();
  bool selectTrainer(int slot);
  void setDiscoveryVisible(bool visible);
  bool requestSetting(Parameter parameter,int value);
  bool requestBandMaximum(int pounds);
  bool requestDamperLevel(int index);
  bool requestIsokinetic(int index,int value);
  bool requestParameter(Parameter parameter);
  bool requestCable(bool open);
  bool requestLoad(uint32_t expectedSession=UINT32_MAX); bool requestUnload(uint32_t expectedSession=UINT32_MAX); bool requestWeightMode(); bool requestMode(int mode);
};
extern VoltraLink voltraLink;
}
