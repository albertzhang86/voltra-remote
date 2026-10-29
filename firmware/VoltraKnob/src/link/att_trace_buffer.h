#pragma once
#include <cstddef>
#include <cstdint>

namespace vlink {
// Fixed storage only. Platform code serializes access; callbacks never print.
struct AttTraceEvent {
  uint32_t at=0;
  int32_t result=0;
  uint16_t connection=0,cid=0,detail=0;
  uint8_t kind=0;
};
class AttTraceBuffer {
 public:
  static constexpr size_t CAPACITY=64;
  void watch(uint16_t connection) {
    if(frozen_||connection==connection_)return;
    connection_=connection;next_=count_=0;notifications_=lastNotification_=0;
  }
  void record(AttTraceEvent event) {
    if(frozen_||event.connection!=connection_)return;
    // High-rate notifications must not evict the pending request/response trail.
    if(event.kind==5&&(event.detail==0x1b||event.detail==0x1d)){
      ++notifications_;lastNotification_=event.at;return;
    }
    events_[next_]=event;next_=(next_+1)%CAPACITY;
    if(count_<CAPACITY)++count_;
    if(event.kind==8)freeze();
  }
  void freeze(){if(count_)frozen_=true;}
  bool pop(AttTraceEvent& event){
    if(!frozen_||!count_)return false;
    event=events_[(next_+CAPACITY-count_)%CAPACITY];--count_;
    if(!count_)frozen_=false;
    return true;
  }
  uint32_t notifications()const{return notifications_;}
  uint32_t lastNotification()const{return lastNotification_;}
 private:
  AttTraceEvent events_[CAPACITY]{};
  uint16_t connection_=0xffff;
  size_t next_=0,count_=0;
  bool frozen_=false;
  uint32_t notifications_=0,lastNotification_=0;
};
}
