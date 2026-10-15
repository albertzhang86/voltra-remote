#include "minitest.h"
#include "protocol/twin_protocol.h"
#include "link/voltra_link.h"
#include <cstring>
int main(){
 using namespace voltra;
 uint8_t query[13];auto frame=endpointFrame(query);CHECK(validFrame(frame.data,frame.len));CHECK_EQ(query[10],0xa7);
 uint8_t command[20];Frame output;
 CHECK(twinFrame(1,"02:00:00:00:00:01",0x1234,command,output));CHECK(validFrame(command,20));
 CHECK_EQ(command[6],0x34);CHECK_EQ(command[7],0x12);CHECK_EQ(command[11],1);CHECK_EQ(command[17],1);
 CHECK(!twinFrame(3,"02:00:00:00:00:01",1,command,output));CHECK(!twinFrame(1,"00:00:00:00:00:00",1,command,output));
 CHECK(!twinFrame(1,"garbage",1,command,output));
 for(int response=0;response<2;++response){
   const size_t size=response?36:35;const int p=response?12:11;
   uint8_t bytes[36]{};bytes[0]=0x55;bytes[2]=response?8:4;bytes[10]=0xa7;
   bytes[p]=82;bytes[p+2]=0x12;bytes[p+3]=2;bytes[p+8]=1;bytes[p+9]=2;bytes[p+14]=2;bytes[p+15]=71;
   sealTwinFrame(bytes,size);auto e=decodeEndpoint(bytes,size);
   CHECK(e.paired());CHECK(!e.independent());CHECK_EQ(e.battery,82);CHECK_EQ(e.peerBattery,71);
   CHECK(!std::strcmp(e.local,"02:00:00:00:00:01"));CHECK(!std::strcmp(e.peer,"02:00:00:00:00:02"));
   bytes[p+2]=0x11;sealTwinFrame(bytes,size);CHECK(!decodeEndpoint(bytes,size).known());
   bytes[p+2]=0;std::memset(bytes+p+9,0,6);sealTwinFrame(bytes,size);CHECK(decodeEndpoint(bytes,size).independent());
   bytes[3]^=1;CHECK(!decodeEndpoint(bytes,size).valid);bytes[3]^=1;
   if(response){bytes[11]=1;sealTwinFrame(bytes,size);CHECK(!decodeEndpoint(bytes,size).valid);}
 }
 uint8_t ack[16]={0x55,16,8,0,0,0,0x34,0x12,0,0,0xa8,0,1,0,0,0};sealTwinFrame(ack,16);
 CHECK(twinReply(ack,16,0x1234,1));CHECK(!twinReply(ack,16,0x1235,1));CHECK(!twinReply(ack,16,0x1234,2));
 ack[13]=1;sealTwinFrame(ack,16);CHECK(!twinReply(ack,16,0x1234,1));
 vlink::DeviceState s;s.link=vlink::LinkState::Ready;s.statusMs=100;s.endpointMs=100;s.endpoint.valid=true;s.endpoint.status=0;
 strcpy(s.endpoint.local,"02:00:00:00:00:01");strcpy(s.endpoint.peer,"00:00:00:00:00:00");s.mode=0;s.fitnessMode=4;s.selected=0;
 strcpy(s.trainers[0].address,"02:00:00:00:00:01");
 strcpy(s.trainers[1].address,"02:00:00:00:00:02");s.trainers[1].seen=true;s.trainers[1].lastSeenMs=100;
 CHECK(vlink::canChangeTwin(s,200,true));CHECK(!vlink::canChangeTwin(s,200,false));
 s.nearby[0]=s.trainers[1];CHECK(vlink::canChangeTwin(s,200,true)); // deduplicated saved advertisement
 s.nearby[1]=s.trainers[1];strcpy(s.nearby[1].address,"02:00:00:00:00:03");CHECK(!vlink::canChangeTwin(s,200,true));
 s.nearby[0]={};s.nearby[1]={};

 s.mode=1;CHECK(!vlink::canChangeTwin(s,200,true));s.mode=0;s.fitnessMode=5;CHECK(!vlink::canChangeTwin(s,200,true));s.fitnessMode=4;
 s.twinPending=true;CHECK(!vlink::canChangeTwin(s,200,true));s.twinPending=false;
 CHECK(!vlink::canChangeTwin(s,4000,true));s.endpoint.status=0x12;strcpy(s.endpoint.peer,s.trainers[1].address);
 CHECK(vlink::canChangeTwin(s,200,false));CHECK(!vlink::canChangeTwin(s,200,true));
 // Discovery pauses during TV pairing/connection. Saved candidates remain
 // selectable, but never authorize settings without a live verified session.
 s.endpoint.status=0;strcpy(s.endpoint.peer,"00:00:00:00:00:00");
 s.statusMs=s.endpointMs=20100;
 CHECK(!vlink::trainerAvailable(s,1,20200));CHECK(!vlink::canChangeTwin(s,20200,true));
 s.discoveryPausedForTv=true;
 CHECK(vlink::trainerAvailable(s,1,20200));CHECK(!vlink::canChangeTwin(s,20200,true));
 s.trainers[1].seen=false;CHECK(vlink::trainerAvailable(s,1,20200));
 CHECK(!vlink::canChangeTwin(s,20200,true)); // Remembered identity is not proximity evidence.
 s.trainers[1].seen=true;s.trainers[1].lastSeenMs=20100;
 CHECK(vlink::canChangeTwin(s,20200,true));
 strcpy(s.trainers[1].address,s.trainers[0].address);CHECK(!vlink::canChangeTwin(s,20200,true));
 s.trainers[1]={};s.nearby[0].seen=true;s.nearby[0].lastSeenMs=20100;strcpy(s.nearby[0].address,"02:00:00:00:00:02");
 CHECK(vlink::canChangeTwin(s,20200,true)); // one unambiguous new peer, free saved slot
 s.nearby[1]=s.nearby[0];strcpy(s.nearby[1].address,"02:00:00:00:00:03");CHECK(!vlink::canChangeTwin(s,20200,true));
 s.nearby[1]={};s.nearby[0].lastSeenMs=100;CHECK(!vlink::canChangeTwin(s,20200,true));
 s.link=vlink::LinkState::Connecting;CHECK(!vlink::canChangeTwin(s,20200,true));
 s.link=vlink::LinkState::Ready;s.fitnessMode=5;CHECK(!vlink::canChangeTwin(s,20200,true));
 s.discoveryPausedForTv=false;CHECK(!vlink::trainerAvailable(s,1,20200));
 std::printf("%d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
