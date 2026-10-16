#pragma once
#include "voltra_protocol.h"
#include <cstring>

namespace voltra {
// A GATT notification is a byte chunk, not necessarily one native packet. Twin
// transitions can reduce chunks to 20 bytes even on a larger negotiated MTU.
// Own one assembler per connection and reset it when that connection/session
// changes. Call feed/reset under the same lock; emit must not reenter either.
class FrameAssembler {
 public:
  static constexpr size_t MAX_FRAME_SIZE=255;
  static constexpr uint32_t MAX_PARTIAL_AGE_MS=1000;

  void reset(){used_=0;startedAt_=0;}
  size_t bufferedSize() const{return used_;}

  // The emitted bytes remain valid only until emit returns. No allocation or
  // copy is needed to decode a complete, checksum-validated packet immediately.
  template<typename Emit>
  void feed(const uint8_t* bytes,size_t size,uint32_t now,Emit emit){
    if(used_&&uint32_t(now-startedAt_)>=MAX_PARTIAL_AGE_MS)reset();
    if(!bytes)return;
    for(size_t i=0;i<size;++i){
      if(used_==MAX_FRAME_SIZE)discard(1);
      if(!used_)startedAt_=now;
      buffer_[used_++]=bytes[i];
      drain(emit);
    }
  }

 private:
  uint8_t buffer_[MAX_FRAME_SIZE]{};
  size_t used_=0;
  uint32_t startedAt_=0;

  void discard(size_t size){
    used_-=size;
    if(used_)std::memmove(buffer_,buffer_+size,used_);
    else startedAt_=0;
  }
  bool headerValid() const{
    if(buffer_[0]!=0x55)return false;
    uint8_t crc=0x77;
    for(size_t i=0;i<3;++i){
      crc^=buffer_[i];
      for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0x8c:0);
    }
    return crc==buffer_[3];
  }
  template<typename Emit>
  void drain(Emit& emit){
    while(used_>=4){
      if(!headerValid()){discard(1);continue;}
      const size_t size=buffer_[1]|size_t(buffer_[2]&3)<<8;
      if(size<13||size>MAX_FRAME_SIZE){discard(1);continue;}
      if(used_<size)return;
      if(!validFrame(buffer_,size)){discard(1);continue;}
      emit(buffer_,size);
      discard(size);
    }
  }
};
}
