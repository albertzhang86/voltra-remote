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
