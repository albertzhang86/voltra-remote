#pragma once
#include "../protocol/twin_protocol.h"
#include "../protocol/mode_parameters.h"
#include <strings.h>

namespace vlink {
// Pairing is authorized by live, identity-checked GATT state. Advertisements only
// choose the peer before connecting; the peer may stop advertising once connected.
struct TwinParticipant {
  voltra::EndpointStatus endpoint;
  int mode=-1,fitness=-1;
  uint32_t endpointAt=0,statusAt=0;
};
inline bool twinParticipantFresh(const TwinParticipant& device,const char* address,uint32_t now){
  return address&&address[0]&&device.endpoint.known()&&
    !strcasecmp(device.endpoint.local,address)&&device.endpointAt&&device.statusAt&&
    uint32_t(now-device.endpointAt)<3500&&uint32_t(now-device.statusAt)<3500;
}
inline bool twinParticipantCanPrepare(const TwinParticipant& device,const char* address,uint32_t now){
 return twinParticipantFresh(device,address,now)&&device.endpoint.independent()&&
   (device.mode==0||voltra::supportedMode(device.mode))&&
   (voltra::fitnessModeUnloaded(device.fitness)||
    (voltra::supportedMode(device.mode)&&
     (voltra::fitnessModeLoaded(device.fitness)||voltra::fitnessModeArmed(device.fitness))));
}
inline bool twinParticipantUnloaded(const TwinParticipant& device,const char* address,uint32_t now){
  return twinParticipantFresh(device,address,now)&&
    (device.mode==0||voltra::supportedMode(device.mode))&&voltra::fitnessModeUnloaded(device.fitness);
}
inline bool twinParticipantIndependent(const TwinParticipant& device,const char* address,uint32_t now){
  return twinParticipantUnloaded(device,address,now)&&device.endpoint.independent();
}
inline bool twinParticipantIdle(const TwinParticipant& device,const char* address,uint32_t now){
  return twinParticipantUnloaded(device,address,now)&&device.mode==0;
}
enum class TwinUnloadResult {Ready,State,Cancelled,WriteFailed,NotConfirmed};
inline bool twinParticipantSameEndpoint(const TwinParticipant& current,const TwinParticipant& before){
 return current.mode==before.mode&&current.endpoint.status==before.endpoint.status&&
   !strcasecmp(current.endpoint.local,before.endpoint.local)&&
   !strcasecmp(current.endpoint.peer,before.endpoint.peer);
}
// Shared by controller/peer preparation and host fault injection. One STOP is
// followed by a new native unloaded readback; no ACK, timeout or cached state
// can authorize the subsequent idle/pair write. There is never a GO here.
template<class Read,class Authorized,class Stop,class Wait,class Clock>
TwinUnloadResult unloadTwinParticipant(const char* address,Read read,Authorized authorized,
                                      Stop stop,Wait wait,Clock now){
 const auto before=read();
 if(!authorized())return TwinUnloadResult::Cancelled;
 if(twinParticipantUnloaded(before,address,now()))return TwinUnloadResult::Ready;
 if(!twinParticipantFresh(before,address,now())||!voltra::supportedMode(before.mode)||
    !(voltra::fitnessModeLoaded(before.fitness)||voltra::fitnessModeArmed(before.fitness)))
   return TwinUnloadResult::State;
 const uint32_t writtenAt=now();
 if(!authorized())return TwinUnloadResult::Cancelled;
 if(!stop())return TwinUnloadResult::WriteFailed;
 const auto confirmed=[&]{
   const auto latest=read();
   return authorized()&&twinParticipantSameEndpoint(latest,before)&&
     latest.statusAt!=before.statusAt&&static_cast<int32_t>(latest.statusAt-writtenAt)>=0&&
     twinParticipantUnloaded(latest,address,now());
 };
 if(wait(2500,confirmed)&&confirmed())return TwinUnloadResult::Ready;
 return authorized()?TwinUnloadResult::NotConfirmed:TwinUnloadResult::Cancelled;
}
// After the peer link closes, require new controller readbacks before restoring
// its previous mode. A paired endpoint captured before teardown is insufficient.
inline bool twinModeRestoreReady(const TwinParticipant& controller,const char* controllerAddress,
                                 const char* peerAddress,bool paired,uint32_t after,uint32_t now){
  if(!twinParticipantIdle(controller,controllerAddress,now)||
     static_cast<int32_t>(controller.endpointAt-after)<=0||static_cast<int32_t>(controller.statusAt-after)<=0)return false;
  return paired?controller.endpoint.paired()&&peerAddress&&peerAddress[0]&&
    !strcasecmp(controller.endpoint.peer,peerAddress):controller.endpoint.independent();
}
inline bool twinCreateConfirmed(bool acknowledged,const TwinParticipant& controller,
                                const char* controllerAddress,const char* peerAddress,
                                uint32_t previousEndpointAt,uint32_t now){
  return acknowledged&&twinParticipantFresh(controller,controllerAddress,now)&&
    controller.endpointAt!=previousEndpointAt&&controller.endpoint.paired()&&
    peerAddress&&!strcasecmp(controller.endpoint.peer,peerAddress);
}
inline bool twinExitConfirmed(bool acknowledged,const TwinParticipant& controller,
                              const char* controllerAddress,uint32_t previousEndpointAt,uint32_t now){
  return acknowledged&&twinParticipantFresh(controller,controllerAddress,now)&&
    controller.endpointAt!=previousEndpointAt&&controller.endpoint.independent();
}
}
