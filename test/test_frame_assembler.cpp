#include "minitest.h"
#include "protocol/frame_assembler.h"
#include "protocol/twin_protocol.h"
#include <vector>
#include <limits>

using Bytes=std::vector<uint8_t>;
Bytes hex(const char* text){
  Bytes bytes;
  while(*text){unsigned value=0;std::sscanf(text,"%2x",&value);bytes.push_back(value);text+=2;}
  return bytes;
}
void append(Bytes& target,const Bytes& source){target.insert(target.end(),source.begin(),source.end());}

int main(){
 using namespace voltra;
 // Recorded successful native unpair: the 52-byte telemetry packet arrived as
 // 20/20/12, followed by the independent endpoint as 20/15. Neither chunk alone
 // is a native packet; the 16-byte operation acknowledgment remained intact.
 // Endpoint addresses are replaced with synthetic identities and CRC resealed.
 const auto telemetry=hex("553404ac10aa711e2000aa80250200000000000000008f000000500000005800000005030200000078050029000000000000daab");
 const auto independent=hex("5523042e10aa721e2000a73701000200000000010000000000000001000000180010c5");
 const auto endpoint=hex("552408e310aa00202000a70064010002000000000100000000000000010000001d006262");
 const auto settings=hex("5526087210aa002020000f000600863e0a00873e0200883e0200893e0400b04f01b053013b89");
 const auto ack=hex("551008f510aa03602000a80002006d0a");
 CHECK_EQ(telemetry.size(),size_t(52));CHECK_EQ(independent.size(),size_t(35));CHECK_EQ(endpoint.size(),size_t(36));CHECK_EQ(settings.size(),size_t(38));
 for(const auto* packet:{&telemetry,&independent,&endpoint,&settings,&ack})CHECK(validFrame(packet->data(),packet->size()));

 FrameAssembler assembler;
 std::vector<Bytes> emitted;
 auto receive=[&](const uint8_t* bytes,size_t size){CHECK(validFrame(bytes,size));emitted.emplace_back(bytes,bytes+size);};
 assembler.feed(telemetry.data(),20,100,receive);CHECK(emitted.empty());CHECK_EQ(assembler.bufferedSize(),size_t(20));
 assembler.feed(telemetry.data()+20,20,130,receive);CHECK(emitted.empty());
 assembler.feed(telemetry.data()+40,12,160,receive);CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted.back()==telemetry);
 assembler.feed(independent.data(),20,200,receive);CHECK_EQ(emitted.size(),size_t(1));
 assembler.feed(independent.data()+20,15,230,receive);CHECK_EQ(emitted.size(),size_t(2));
 CHECK(decodeEndpoint(emitted.back().data(),emitted.back().size()).independent());
 CHECK_EQ(assembler.bufferedSize(),size_t(0));

 for(const auto* packet:{&endpoint,&settings}){
   emitted.clear();assembler.reset();
   assembler.feed(packet->data(),20,300,receive);CHECK(emitted.empty());
   assembler.feed(packet->data()+20,packet->size()-20,330,receive);
   CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==*packet);
 }
 CHECK_EQ(decode(settings.data(),settings.size()).mode,1);
 CHECK_EQ(decode(settings.data(),settings.size()).fitnessMode,4);

 // Both coalesced packets and every possible single-byte boundary are valid.
 Bytes coalesced=ack;append(coalesced,endpoint);append(coalesced,settings);append(coalesced,telemetry);
 emitted.clear();assembler.reset();assembler.feed(coalesced.data(),coalesced.size(),400,receive);
 CHECK_EQ(emitted.size(),size_t(4));CHECK(emitted[0]==ack);CHECK(emitted[3]==telemetry);
 emitted.clear();assembler.reset();
 for(size_t i=0;i<coalesced.size();++i)assembler.feed(coalesced.data()+i,1,500+i,receive);
 CHECK_EQ(emitted.size(),size_t(4));CHECK(emitted[1]==endpoint);CHECK_EQ(assembler.bufferedSize(),size_t(0));

 // Bad header/length/checksum bytes do not authorize a packet. Resynchronize
 // to the next complete valid frame without carrying an unbounded junk buffer.
 Bytes damaged=settings;damaged[18]^=0x80;append(damaged,ack);
 emitted.clear();assembler.reset();assembler.feed(damaged.data(),damaged.size(),700,receive);
 CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==ack);
 damaged=endpoint;damaged[3]^=1;append(damaged,settings);
 emitted.clear();assembler.reset();assembler.feed(damaged.data(),damaged.size(),800,receive);
 CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==settings);
 Bytes oversized(256,0);oversized[0]=0x55;oversized[2]=5;sealTwinFrame(oversized.data(),oversized.size());append(oversized,ack);
 emitted.clear();assembler.reset();assembler.feed(oversized.data(),oversized.size(),900,receive);
 CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==ack);
 Bytes noise(4096,0x55);append(noise,endpoint);
 emitted.clear();assembler.reset();assembler.feed(noise.data(),noise.size(),1000,receive);
 CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==endpoint);CHECK_EQ(assembler.bufferedSize(),size_t(0));
 Bytes maximum(255,0);maximum[0]=0x55;maximum[2]=4;sealTwinFrame(maximum.data(),maximum.size());
 emitted.clear();assembler.reset();
 for(size_t offset=0;offset<maximum.size();offset+=20){
   const size_t remaining=maximum.size()-offset;
   assembler.feed(maximum.data()+offset,remaining<20?remaining:20,1100,receive);
   CHECK(assembler.bufferedSize()<=FrameAssembler::MAX_FRAME_SIZE);
 }
 CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==maximum);

 // Reset/cancellation drops a partial frame. A different connection cannot
 // complete the first connection's packet or inherit its receipt timestamp.
 FrameAssembler peer;
 emitted.clear();assembler.reset();assembler.feed(endpoint.data(),20,1200,receive);assembler.reset();
 assembler.feed(endpoint.data()+20,16,1210,receive);CHECK(emitted.empty());
 assembler.feed(ack.data(),ack.size(),1220,receive);CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==ack);
 emitted.clear();assembler.reset();peer.reset();
 assembler.feed(endpoint.data(),20,1300,receive);
 peer.feed(endpoint.data()+20,16,1310,receive);CHECK(emitted.empty());
 peer.feed(ack.data(),ack.size(),1320,receive);CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==ack);
 assembler.feed(endpoint.data()+20,16,1330,receive);CHECK_EQ(emitted.size(),size_t(2));CHECK(emitted[1]==endpoint);

 // A trickle of fragments cannot renew an incomplete frame indefinitely.
 emitted.clear();assembler.reset();
 assembler.feed(telemetry.data(),20,2000,receive);
 assembler.feed(telemetry.data()+20,20,2900,receive);
 assembler.feed(telemetry.data()+40,12,3000,receive);CHECK(emitted.empty());
 assembler.feed(endpoint.data(),endpoint.size(),3010,receive);CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==endpoint);
 emitted.clear();assembler.reset();
 const uint32_t nearWrap=std::numeric_limits<uint32_t>::max()-500;
 assembler.feed(endpoint.data(),20,nearWrap,receive);
 assembler.feed(endpoint.data()+20,16,uint32_t(nearWrap+999),receive);CHECK_EQ(emitted.size(),size_t(1));
 emitted.clear();assembler.reset();
 assembler.feed(endpoint.data(),20,nearWrap,receive);
 assembler.feed(endpoint.data()+20,16,uint32_t(nearWrap+1000),receive);CHECK(emitted.empty());
 assembler.feed(ack.data(),ack.size(),uint32_t(nearWrap+1010),receive);CHECK_EQ(emitted.size(),size_t(1));CHECK(emitted[0]==ack);
 assembler.reset();assembler.feed(nullptr,0,5000,receive);CHECK_EQ(assembler.bufferedSize(),size_t(0));
 std::printf("%d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
