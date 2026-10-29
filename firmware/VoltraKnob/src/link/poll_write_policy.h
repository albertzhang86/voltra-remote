#pragma once
#include "../protocol/voltra_protocol.h"

namespace vlink {
// Only these read-only requests may be deferred after ATT backpressure. Never
// retry or defer an auth, subscription, parameter, load, unload or Twin write.
inline bool trainerReadRequest(voltra::Frame frame){
  if(!voltra::validFrame(frame.data,frame.len)||frame.data[2]!=4||
     frame.data[4]!=0xaa||frame.data[5]!=0x10||
     frame.data[6]!=0||frame.data[7]!=0x20||frame.data[8]!=0x20||frame.data[9]!=0)return false;
  // The sequenced setup query belongs to the load transaction, not polling.
  if(frame.data[10]==0xa7)return frame.len==13;
  if(frame.data[10]!=0x0f||frame.len<17)return false;
  const unsigned count=frame.data[11]|unsigned(frame.data[12])<<8;
  return count>0&&frame.len==15+2*count;
}
struct PollWritePolicy {
  static constexpr unsigned FAILURE_LIMIT=5;
  unsigned failures=0;
  uint32_t retryAt=0;
  bool mayAttempt(bool readOnly,uint32_t now) const {
    return !readOnly||!failures||static_cast<int32_t>(now-retryAt)>=0;
  }
  // True asks the worker to reconnect. A failed mutation is returned as failure
  // and retains the previous fail-closed teardown behavior, without replay.
  bool completed(bool readOnly,bool succeeded,bool connected,uint32_t now){
    if(!connected)return true;
    if(succeeded){failures=0;retryAt=0;return false;}
    if(!readOnly)return true;
    if(failures<FAILURE_LIMIT)++failures;
    retryAt=now+(failures==1?250:failures==2?500:1000);
    return failures>=FAILURE_LIMIT;
  }
};
// Poll replies arrive on the notify characteristic. Avoid creating an ATT
// response transaction for those polls when the peer explicitly supports it.
// Oversized writes would silently become long writes in NimBLE, so retain
// response behavior unless the entire request fits one negotiated ATT packet.
inline bool trainerWriteResponse(voltra::Frame frame,bool canWrite,bool canWriteNoResponse,uint16_t mtu){
 return canWrite&&!(trainerReadRequest(frame)&&canWriteNoResponse&&mtu>=3&&frame.len<=mtu-3);
}
inline bool trainerReadHostQueued(voltra::Frame frame,bool response,uint16_t mtu){
 return response&&trainerReadRequest(frame)&&mtu>=3&&frame.len<=mtu-3;
}
struct TrainerWriteResult {
  bool readOnly=false,attempted=false,accepted=false,connected=false,reconnect=false,response=false;
  uint32_t elapsedMs=0;
};
// Shared by the actual GATT sender and host fault injection. There is exactly
// one transport attempt; a later poll is scheduled normally, never replayed here.
template<class Clock,class Connected,class Write>
TrainerWriteResult writeTrainerRequest(PollWritePolicy& policy,voltra::Frame frame,
                                      bool canWrite,bool canWriteNoResponse,uint16_t mtu,
                                      Clock now,Connected connected,Write write){
  TrainerWriteResult result;result.readOnly=trainerReadRequest(frame);result.connected=connected();
  if(!result.connected){result.reconnect=true;return result;}
  if(!policy.mayAttempt(result.readOnly,now()))return result;
  result.response=trainerWriteResponse(frame,canWrite,canWriteNoResponse,mtu);
  result.attempted=true;const uint32_t started=now();const bool sent=write(result.response);
  result.elapsedMs=now()-started;result.connected=connected();
  result.reconnect=policy.completed(result.readOnly,sent,result.connected,now());
  result.accepted=sent&&result.connected;return result;
}
}
