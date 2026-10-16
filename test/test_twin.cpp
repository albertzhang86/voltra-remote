#include "minitest.h"
#include "protocol/twin_protocol.h"
#include "link/voltra_link.h"
#include "link/twin_transaction.h"
#include <cstring>
#include <initializer_list>
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

 for(int mode:{0,1,2,4,7}){s.mode=mode;CHECK(vlink::canChangeTwin(s,200,true));}
 s.mode=3;CHECK(!vlink::canChangeTwin(s,200,true));s.mode=0;
 s.loaded=true;CHECK(!vlink::canChangeTwin(s,200,true));s.loaded=false;
 s.armed=true;CHECK(!vlink::canChangeTwin(s,200,true));s.armed=false;
 s.fitnessMode=5;CHECK(!vlink::canChangeTwin(s,200,true));s.fitnessMode=4;
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

 // A trusted exact-peer TV handoff can perform the read-only peer handshake
 // while advertisements are paused. It never relaxes standalone discovery.
 s.link=vlink::LinkState::Ready;s.statusMs=s.endpointMs=20100;s.mode=1;s.fitnessMode=4;
 s.discoveryPausedForTv=true;s.trainers[1]={};s.nearby[0]={};
 vlink::TvTwinOffer offer;offer.epoch=7;offer.session=s.session;offer.deadline=21500;offer.ready=true;
 strcpy(offer.peer,"02:00:00:00:00:02");
 CHECK(!vlink::canChangeTwin(s,20200,true));
 CHECK(vlink::canHandoffTwin(s,offer,7,offer.peer,20200));
 // With A+B saved, a TV-owned C cannot silently replace B after pairing.
 // Exercise both controller slots, a free slot, and case-insensitive peer reuse.
 for(int controllerSlot:{0,1}){
   auto saved=s;auto slotOffer=offer;saved.selected=controllerSlot;
   saved.trainers[0]={};saved.trainers[1]={};
   strcpy(saved.trainers[controllerSlot].address,s.endpoint.local);
   strcpy(slotOffer.peer,"02:00:00:00:AB:CD");
   auto& peerSlot=saved.trainers[1-controllerSlot];
   CHECK(vlink::canHandoffTwin(saved,slotOffer,7,slotOffer.peer,20200));
   strcpy(peerSlot.address,"02:00:00:00:00:03");
   CHECK(!vlink::canHandoffTwin(saved,slotOffer,7,slotOffer.peer,20200));
   strcpy(saved.nearby[0].address,slotOffer.peer);saved.nearby[0].seen=true;saved.nearby[0].lastSeenMs=20200;
   CHECK(!vlink::canHandoffTwin(saved,slotOffer,7,slotOffer.peer,20200));
   strcpy(peerSlot.address,"02:00:00:00:ab:cd");
   CHECK(vlink::canHandoffTwin(saved,slotOffer,7,slotOffer.peer,20200));
 }
 CHECK(!vlink::canHandoffTwin(s,offer,8,offer.peer,20200));
 CHECK(!vlink::canHandoffTwin(s,offer,7,"02:00:00:00:00:03",20200));
 CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,21500));
 offer.ready=false;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));offer.ready=true;
 ++offer.session;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));--offer.session;
 s.loaded=true;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.loaded=false;
 s.armed=true;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.armed=false;
 s.twinPending=true;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.twinPending=false;
 s.parameterSelectionPending=true;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.parameterSelectionPending=false;
 s.cableActive=true;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.cableActive=false;
 s.controlBusyUntil=21000;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.controlBusyUntil=0;
 s.endpointMs=1;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.endpointMs=20100;
 s.fitnessMode=5;CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));s.fitnessMode=4;
 // The short offer TTL only admits the request. A three-second auth wait or
 // delayed TV notification must not invalidate the accepted 40-second operation.
 // Revocation, identity/session changes and the original operation deadline do.
 offer.revision=3;
 const uint32_t acceptedRevision=offer.revision,transactionDeadline=60200;
 const char* offeredPeer="02:00:00:00:00:02";
 auto accepted=[&](uint32_t now){
   return vlink::tvTwinTransactionAuthorized(offer,acceptedRevision,7,s.session,offeredPeer,transactionDeadline,now);
 };
 CHECK(accepted(20200));CHECK(accepted(25000));
 CHECK(!vlink::canHandoffTwin(s,offer,7,offeredPeer,25000)); // An expired offer cannot start another transaction.
 CHECK(accepted(transactionDeadline-1));CHECK(!accepted(transactionDeadline));
 offer.deadline=70000;CHECK(!accepted(transactionDeadline)); // A refresh cannot extend the accepted operation.
 CHECK(!vlink::tvTwinTransactionAuthorized(offer,0,7,s.session,offeredPeer,transactionDeadline,25000));
 CHECK(!vlink::tvTwinTransactionAuthorized(offer,acceptedRevision,0,s.session,offeredPeer,transactionDeadline,25000));
 CHECK(!vlink::tvTwinTransactionAuthorized(offer,acceptedRevision,8,s.session,offeredPeer,transactionDeadline,25000));
 ++offer.session;CHECK(!accepted(25000));--offer.session;
 offer.ready=false;CHECK(!accepted(25000));offer.ready=true;
 strcpy(offer.peer,"02:00:00:00:00:03");CHECK(!accepted(25000));strcpy(offer.peer,offeredPeer);
 ++offer.revision;CHECK(!accepted(25000)); // Revoking and re-offering the same peer cannot revive the old request.
 offer.revision=acceptedRevision;
 const uint32_t wrapStarted=UINT32_MAX-1000,wrapDeadline=wrapStarted+40000;
 offer.deadline=wrapStarted+500;
 CHECK(!vlink::tvTwinAuthorization(offer,7,s.session,offeredPeer,wrapStarted+3000));
 CHECK(vlink::tvTwinTransactionAuthorized(offer,acceptedRevision,7,s.session,offeredPeer,wrapDeadline,wrapStarted+3000));
 CHECK(!vlink::tvTwinTransactionAuthorized(offer,acceptedRevision,7,s.session,offeredPeer,wrapDeadline,wrapDeadline));
 strcpy(offer.peer,s.trainers[0].address);CHECK(!vlink::canHandoffTwin(s,offer,7,offer.peer,20200));
 // Connecting the second GATT client stops its advertisements. Fresh, verified
 // GATT state is sufficient for the transaction, even after discovery expires.
 vlink::TwinParticipant controller,peer;
 controller.endpoint.valid=true;controller.endpoint.status=0;
 strcpy(controller.endpoint.local,"02:00:00:00:00:01");strcpy(controller.endpoint.peer,"00:00:00:00:00:00");
 controller.endpointAt=controller.statusAt=21000;controller.fitness=4;
 peer=controller;strcpy(peer.endpoint.local,"02:00:00:00:00:02");
 const char* controllerAddress="02:00:00:00:00:01";
 const char* peerAddress="02:00:00:00:00:02";
 for(int mode:{0,1,2,4,7}){
   controller.mode=peer.mode=mode;
   CHECK(vlink::twinParticipantIndependent(controller,controllerAddress,21200));
   CHECK(vlink::twinParticipantIndependent(peer,peerAddress,21200));
   CHECK(vlink::twinParticipantIdle(peer,peerAddress,21200)==(mode==0));
 }
 peer.mode=0;
 for(int fitness:{1,2,3,5,6,21,-1}){
   peer.fitness=fitness;CHECK(!vlink::twinParticipantIndependent(peer,peerAddress,21200));
 }
 peer.fitness=4;
 CHECK(!vlink::twinParticipantIndependent(peer,controllerAddress,21200));
 CHECK(!vlink::twinParticipantIndependent(peer,peerAddress,25000));
 peer.statusAt=0;CHECK(!vlink::twinParticipantIndependent(peer,peerAddress,21200));peer.statusAt=21000;
 peer.endpoint.status=0x11;CHECK(!vlink::twinParticipantIndependent(peer,peerAddress,21200));peer.endpoint.status=0;
 peer.mode=6;CHECK(!vlink::twinParticipantIndependent(peer,peerAddress,21200));peer.mode=0;
 CHECK(vlink::twinParticipantIdle(peer,peerAddress,21200));

 // A request acknowledgment alone cannot prove native pairing. Require the
 // controller's new, fresh endpoint report identifying the requested peer.
 controller.mode=0;controller.endpoint.status=0x12;
 strcpy(controller.endpoint.peer,peerAddress);controller.endpointAt=21200;
 CHECK(vlink::twinCreateConfirmed(true,controller,controllerAddress,peerAddress,21000,21300));
 CHECK(!vlink::twinCreateConfirmed(false,controller,controllerAddress,peerAddress,21000,21300));
 CHECK(!vlink::twinCreateConfirmed(true,controller,controllerAddress,peerAddress,21200,21300));
 CHECK(!vlink::twinCreateConfirmed(true,controller,controllerAddress,"02:00:00:00:00:03",21000,21300));
 CHECK(!vlink::twinCreateConfirmed(true,controller,peerAddress,peerAddress,21000,21300));
 CHECK(!vlink::twinCreateConfirmed(true,controller,controllerAddress,peerAddress,21000,25000));
 CHECK(!vlink::twinExitConfirmed(true,controller,controllerAddress,21000,21300));
 // Mode restoration waits for readbacks received after temporary peer teardown.
 // Its cached paired endpoint or prior idle status cannot authorize the write.
 const uint32_t teardownAt=21250;
 controller.endpointAt=controller.statusAt=21200;
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));
 controller.endpointAt=21300;
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));
 controller.statusAt=21300;
 CHECK(vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,false,teardownAt,21400));
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,"02:00:00:00:00:03",true,teardownAt,21400));
 CHECK(!vlink::twinModeRestoreReady(controller,peerAddress,peerAddress,true,teardownAt,21400));
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,25000));
 controller.fitness=5;CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));controller.fitness=4;
 controller.mode=2;CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));controller.mode=0;
 controller.endpoint.status=0;strcpy(controller.endpoint.peer,"00:00:00:00:00:00");
 CHECK(vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,false,teardownAt,21400));
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,true,teardownAt,21400));
 controller.endpointAt=controller.statusAt=50;
 CHECK(vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,false,UINT32_MAX-100,100));
 CHECK(!vlink::twinModeRestoreReady(controller,controllerAddress,peerAddress,false,50,100));
 controller.endpointAt=21200;controller.statusAt=21000;
 CHECK(vlink::twinExitConfirmed(true,controller,controllerAddress,21000,21300));
 CHECK(!vlink::twinExitConfirmed(false,controller,controllerAddress,21000,21300));
 CHECK(!vlink::twinExitConfirmed(true,controller,controllerAddress,21200,21300));
 CHECK(!vlink::twinCreateConfirmed(true,controller,controllerAddress,peerAddress,21000,21300));
 std::printf("%d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
