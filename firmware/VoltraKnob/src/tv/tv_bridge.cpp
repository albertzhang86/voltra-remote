#include "tv_bridge.h"
#include "bridge_protocol.h"
#include "connection_policy.h"
#include "../link/voltra_link.h"
#include "../protocol/voltra_protocol.h"
#include "../board/board.h"
#include "../ui/ui.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <host/ble_store.h>
#include <atomic>
#include <cstring>
#include <vector>

namespace tvbridge {
namespace {
NimBLEServer* server=nullptr;
NimBLECharacteristic* updates=nullptr;
QueueHandle_t requests;
std::atomic<uint16_t> peer{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> epoch{0},pairUntil{0};
std::atomic<int> shownPasskey{-1};
std::atomic<bool> authenticated{false};
std::atomic<bool> accepting{false},trustedTv{false},admittedTrusted{false},rejectedPairing{false};
std::atomic<bool> forgettingTvs{false};
std::atomic<uint32_t> connectedAt{0};
uint32_t discoveryUntil=0,radioStoppedAt=0,nextAdvertiseAt=0;
uint16_t lastId=0;
uint32_t lastEpoch=0,lastSnapshot=0,pendingAt=0,pendingStamp=0;
Command pendingCommand;
bool pending=false;
uint32_t offerEpoch=0,offerSession=0,offerDeadline=0;
bool offerSeen=false,offerReady=false;
char offeredPeer[18]{};
uint16_t nextMixedRequest=1,mixedRequestId=0;
uint32_t mixedRequestEpoch=0,mixedRequestSession=0,mixedRequestAt=0,lastMixedNotify=0;
bool mixedRequestAccepted=false;
char requestedPeer[18]{},mixedMessage[64]{};
constexpr uint16_t fields[]={0x3e86,0x3e87,0x53b0,0x3e88,0x5362,0x5103,0x5350,0x5410,0x5411,0x5412,0x5413};

// The peripheral's authenticated Secure Connections bond is the trust record.
// Trainer links do not pair this way. Read only the security flags, never log keys.
bool trustedIdentity(const NimBLEAddress& address){
 const auto state=vlink::voltraLink.state();
 for(const auto& trainer:state.trainers)
   if(!strcasecmp(trainer.address,address.toString().c_str()))return false;
 ble_store_key_sec key{};key.peer_addr=*address.getBase();
 ble_store_value_sec security{};
 return ble_store_read_our_sec(&key,&security)==0&&security.ltk_present&&
   security.authenticated&&security.sc&&security.key_size==16;
}
bool hasTrustedBond(){
 for(int i=0;i<NimBLEDevice::getNumBonds();++i)
   if(trustedIdentity(NimBLEDevice::getBondedAddress(i)))return true;
 return false;
}
void stopAdvertising(){
 accepting=false;server->stopAdvertising();radioStoppedAt=millis();
}
bool advertise(){
 if(!vlink::voltraLink.setTvRadioActive(true))return false;
 accepting=true;
 if(server->startAdvertising())return true;
 accepting=false;radioStoppedAt=millis();
 Serial.println("[tv] advertising failed; will retry");return false;
}

bool busy(const vlink::DeviceState& s,uint32_t now){return s.twinPending||s.parameterSelectionPending||
 (s.controlBusyUntil&&static_cast<int32_t>(s.controlBusyUntil-now)>0)||!ui::tvCanEdit();}
bool value(const vlink::DeviceState& s,uint16_t field,int& out,uint32_t& stamp){
 stamp=s.settingsMs;
 switch(field){
 case 0x3e86:out=s.weightLbs;return out>=5&&out<=200;
 case 0x3e87:out=s.chainsLbs;return out>=0&&out<=100;
 case 0x53b0:out=s.inverseChainsEnabled;return out==0||out==1;
 case 0x3e88:out=s.eccentricLbs;return out>=-195&&out<=195;
 default:{const int index=voltra::modeParameterIndex(field);if(index<0)return false;
   out=s.modeValues[index];stamp=s.modeValueMs[index];return stamp!=0;}
 }
}
bool editable(const vlink::DeviceState& s,int index,uint32_t now){
 if(busy(s,now)||!vlink::twinControlReady(s,now))return false;
 if(index==0)return vlink::canEditParameter(s,now,vlink::Parameter::Weight);
 if(index<4)return vlink::canEdit(s,now);
 if(index==4)return vlink::canEditBand(s,now);
 if(index==5)return vlink::canEditDamper(s,now);
 return vlink::canEditIsokinetic(s,now,voltra::modeParameterIndex(fields[index]));
}
bool sameMode(int mode,int index){return index<4?mode==1:index==4?mode==2:index==5?mode==4:mode==7;}
void sendResult(const Command& c,uint8_t code){uint8_t bytes[16];result(bytes,c,code);if(authenticated)updates->notify(bytes,sizeof(bytes),peer);}
void clearMixedOffer(){offerDeadline=0;offeredPeer[0]=0;offerSeen=false;offerReady=false;
 vlink::voltraLink.updateTvTwinOffer(0,0,0,nullptr,false);}
void clearMixedRequest(){mixedRequestId=0;mixedRequestAccepted=false;requestedPeer[0]=0;}
void notifyMixedRequest(){
 uint8_t bytes[20];if(authenticated&&mixedRequestId&&twinRequest(bytes,mixedRequestEpoch,mixedRequestSession,mixedRequestId,requestedPeer)){
   updates->notify(bytes,sizeof(bytes),peer);lastMixedNotify=millis();
 }
}

class ServerCallbacks:public NimBLEServerCallbacks {
 void onConnect(NimBLEServer* s,NimBLEConnInfo& info) override {
   // ESP32 can restore bond encryption before its delayed CONNECT callback.
   // The authentication callback has already admitted this exact connection.
   if(info.getConnHandle()==peer&&authenticated)return;
   const bool trusted=trustedIdentity(info.getIdAddress());
   Serial.printf("[tv] connected handle=%u pairing=%d trusted=%d\n",info.getConnHandle(),pairing(),trusted);
   // Android may use a private address whose saved identity resolves only during
   // encryption. Permit that handshake, but expose no session/data until the
   // authenticated callback verifies the resolved bond. New PINs remain blocked.
   if(!allowConnection(accepting,pairing(),trustedTv,peer.load()!=BLE_HS_CONN_HANDLE_NONE)){s->disconnect(info);return;}
   accepting=false;admittedTrusted=trusted;rejectedPairing=false;connectedAt=millis();
   peer=info.getConnHandle();epoch=esp_random()|1u;shownPasskey=-1;
   if(!NimBLEDevice::startSecurity(info.getConnHandle()))s->disconnect(info);
 }
 bool onRepeatPairing(NimBLEConnInfo&) override {return pairing();}
 uint32_t onPassKeyDisplay() override {
   const uint32_t pin=esp_random()%1000000;
   if(!pairing()){
     rejectedPairing=true;server->disconnect(peer);
     Serial.println("[tv] new pairing rejected outside pairing window");return pin;
   }
   shownPasskey=pin;Serial.println("[tv] passkey displayed");return pin;
 }
 void onAuthenticationComplete(NimBLEConnInfo& info) override {
   if(forgettingTvs){server->disconnect(info);return;}
   const bool trusted=(info.getConnHandle()==peer&&admittedTrusted)||trustedIdentity(info.getIdAddress());
   const bool permitted=info.isSlave()&&allowAuthentication(info.isEncrypted(),info.isAuthenticated(),info.isBonded(),trusted,pairing(),rejectedPairing);
   if(peer==BLE_HS_CONN_HANDLE_NONE&&accepting&&permitted){
     // Authorization is complete even if remote-feature discovery has not yet
     // delivered CONNECT. Bind the session once; the late callback must not reset it.
     peer=info.getConnHandle();epoch=esp_random()|1u;connectedAt=millis();
     accepting=false;admittedTrusted=trusted;
     Serial.println("[tv] authenticated before connect callback");
   }
   Serial.printf("[tv] authentication handle=%u expected=%u encrypted=%d authenticated=%d bonded=%d trusted=%d rejected=%d\n",info.getConnHandle(),peer.load(),info.isEncrypted(),info.isAuthenticated(),info.isBonded(),trusted,rejectedPairing.load());
   if(info.getConnHandle()!=peer||!permitted){server->disconnect(info);return;}
   server->stopAdvertising();
   authenticated=true;trustedTv=true;shownPasskey=-1;pairUntil=0;
   vlink::voltraLink.setTvSession(epoch);
 }
 void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int reason) override {
   Serial.printf("[tv] disconnected handle=%u reason=%d\n",info.getConnHandle(),reason);
   if(info.getConnHandle()!=peer){
     if(peer==BLE_HS_CONN_HANDLE_NONE){accepting=false;admittedTrusted=false;rejectedPairing=false;}
     return;
   }
   authenticated=false;epoch=0;peer=BLE_HS_CONN_HANDLE_NONE;shownPasskey=-1;
   admittedTrusted=false;rejectedPairing=false;
   vlink::voltraLink.setTvSession(0);
 }
} serverCallbacks;
class CommandCallbacks:public NimBLECharacteristicCallbacks {
 void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& info) override {
   if(!authenticated||info.getConnHandle()!=peer||!info.isEncrypted()||!info.isAuthenticated())return;
   const auto bytes=characteristic->getValue();Command c;
   if(!decode(bytes.data(),bytes.size(),c))return;
   // Bounded callback work; commands are checked and dispatched on the UI loop.
   if(xQueueSend(requests,&c,0)!=pdTRUE)sendResult(c,2);
 }
} commandCallbacks;

void publish(const vlink::DeviceState& s,uint32_t now){
 uint8_t bytes[MAX_SNAPSHOT]{};bytes[0]=MAGIC;bytes[1]=VERSION;bytes[2]=1;
 const bool fresh=vlink::fresh(s,now);
 bytes[3]=(s.link==vlink::LinkState::Ready?1:0)|(fresh?2:0)|(busy(s,now)||pending?4:0)|
   (vlink::endpointFresh(s,now)?8:0)|(s.endpoint.paired()?16:0)|TWIN_IDENTITY_GUARD_FLAG|MIXED_TWIN_HANDOFF_FLAG;
 put32(bytes+4,epoch);put32(bytes+8,s.session);put32(bytes+12,now);
 bytes[16]=s.selected<0?255:s.selected;
 const int battery=board::batteryPercent();bytes[17]=battery<0?255:battery;bytes[18]=s.battery<0?255:s.battery;
 bytes[133]=board::usbPowerConnected()?USB_POWER_FLAG:0;
 bytes[19]=s.mode<0?255:s.mode;put16(bytes+20,s.fitnessMode<0?65535:s.fitnessMode);
 const auto activeWeight=vlink::activeWeightReading(s,now);
 const auto movingWeight=vlink::movingWeightReading(s,now);
 if(movingWeight.source==vlink::ActiveWeightSource::CableForce)writeMovingWeight(bytes,movingWeight.tenthsLb);
 else writeActiveWeight(bytes,activeWeight.tenthsLb);
 put16(bytes+26,encodedRepCount(vlink::repetitionReading(s,now)));
 bytes[30]=fresh&&!busy(s,now)&&vlink::twinControlReady(s,now)&&vlink::canSwitch(s,now)&&voltra::fitnessModeUnloaded(s.fitnessMode);
 if(s.selected>=0&&s.selected<2){
   memcpy(bytes+32,s.trainers[s.selected].name,31);memcpy(bytes+64,s.trainers[s.selected].address,17);
 }
 uint16_t mask=0;uint8_t count=0;
 for(int i=0;i<11;++i){
   int actual;uint32_t stamp;
   if(!sameMode(s.mode,i)||!value(s,fields[i],actual,stamp)||!stamp||now-stamp>=8000)continue;
   const int offset=SNAPSHOT_BASE+count*6;put16(bytes+offset,fields[i]);put32(bytes+offset+2,actual);++count;
   if(editable(s,i,now))mask|=1u<<i;
 }
 put16(bytes+28,mask);bytes[31]=count;
 if(s.selected>=0&&s.selected<2){
   const bool paired=s.endpoint.paired();
   const auto* candidate=paired?nullptr:vlink::nearbyTwinPeer(s,now);
   const auto offer=mixedTwinOffer();
   const bool handoff=!paired&&offer.available;
   if(handoff)bytes[3]|=HANDOFF_CANDIDATE_FLAG;
   const auto& other=candidate?*candidate:s.trainers[1-s.selected];
   const char* address=paired?s.endpoint.peer:handoff?offer.peerAddress:other.address;
   const bool nameMatches=!strcasecmp(other.address,address);
   memcpy(bytes+82,nameMatches?other.name:"VOLTRA peer",nameMatches?31:11);
   memcpy(bytes+114,address,17);
   bytes[132]=paired&&s.endpoint.peerBattery>=0?s.endpoint.peerBattery:255;
   bytes[133]|=((handoff||vlink::canChangeTwin(s,now,true))&&!pending&&ui::tvCanEdit()?1:0)|
     (vlink::canChangeTwin(s,now,false)&&!pending&&ui::tvCanEdit()?2:0)|weightUnitFlags(s.mode,s.weightUnitRaw);
 }
 if(server->getPeerMTU(peer)>=247)updates->notify(bytes,SNAPSHOT_BASE+count*6,peer);
}
}

void begin(){
 requests=xQueueCreate(4,sizeof(Command));
 server=NimBLEDevice::createServer();server->setCallbacks(&serverCallbacks,false);server->advertiseOnDisconnect(false);
 NimBLEDevice::setSecurityAuth(true,true,true);NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
 auto* service=server->createService(SERVICE);
 auto* command=service->createCharacteristic(COMMAND,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC|NIMBLE_PROPERTY::WRITE_AUTHEN,24);
 command->setCallbacks(&commandCallbacks);
 updates=service->createCharacteristic(UPDATES,NIMBLE_PROPERTY::NOTIFY);
 server->start();
 auto* advertisement=NimBLEDevice::getAdvertising();advertisement->addServiceUUID(SERVICE);
 advertisement->enableScanResponse(true);advertisement->setName("Voltra Remote");
 trustedTv=hasTrustedBond();
 Serial.printf("[tv] trusted reconnect available=%d\n",trustedTv.load());
}
void openPairing(){
 const auto s=vlink::voltraLink.state();
 if(peer!=BLE_HS_CONN_HANDLE_NONE||!vlink::canSwitch(s,millis()))return;
 if(!vlink::voltraLink.setTvRadioActive(true)){
   Serial.println("[tv] pairing deferred: discovery did not stop");
   vlink::voltraLink.setTvRadioActive(false);return;
 }
 stopAdvertising();discoveryUntil=0;pairUntil=millis()+120000;shownPasskey=-1;
 if(!advertise()){
   pairUntil=0;
   Serial.println("[tv] advertising failed");return;
 }
 Serial.println("[tv] pairing open; trainer discovery paused");
}
void disconnect(){
 pairUntil=0;authenticated=false;epoch=0;shownPasskey=-1;vlink::voltraLink.setTvSession(0);
 stopAdvertising();const auto handle=peer.load();if(handle!=BLE_HS_CONN_HANDLE_NONE)server->disconnect(handle);
}
void findTrainers(){
 if(peer!=BLE_HS_CONN_HANDLE_NONE||!vlink::canSwitch(vlink::voltraLink.state(),millis()))return;
 pairUntil=0;discoveryUntil=millis()+30000;shownPasskey=-1;stopAdvertising();
 vlink::voltraLink.rediscoverTrainers();
}
bool forgetTvs(){
 const auto s=vlink::voltraLink.state();
 if(!vlink::canSwitch(s,millis())||s.loaded||s.armed||s.parameterSelectionPending)return false;
 std::vector<NimBLEAddress> tvBonds;
 for(int i=0;i<NimBLEDevice::getNumBonds();++i){
   const auto address=NimBLEDevice::getBondedAddress(i);
   bool trainer=false;
   for(const auto& saved:s.trainers)if(!strcasecmp(saved.address,address.toString().c_str()))trainer=true;
   if(!trainer&&trustedIdentity(address))tvBonds.push_back(address);
 }
 // Revoke control before deleting keys. A late authentication callback cannot
 // revive the session while accepting is false and the pairing window is shut.
 forgettingTvs=true;disconnect();admittedTrusted=false;trustedTv=false;discoveryUntil=0;xQueueReset(requests);pending=false;
 bool removed=true;
 for(const auto& address:tvBonds)if(!NimBLEDevice::deleteBond(address))removed=false;
 trustedTv=hasTrustedBond();
 return removed&&!trustedTv;
}
bool connected(){return authenticated;}
bool trusted(){return trustedTv;}
bool findingTrainers(){return windowOpen(discoveryUntil,millis());}
bool pairing(){return windowOpen(pairUntil,millis());}
int passkey(){return shownPasskey;}
MixedTwinOffer mixedTwinOffer(){
 MixedTwinOffer offer;const auto s=vlink::voltraLink.state();const uint32_t now=millis();
 offer.pending=authenticated&&mixedRequestId&&mixedRequestEpoch==epoch&&mixedRequestSession==s.session;
 offer.owned=authenticated&&trustedTv&&offeredPeer[0]&&offerEpoch==epoch&&offerSession==s.session;
 offer.available=offer.owned&&offerReady&&static_cast<int32_t>(offerDeadline-now)>0&&vlink::twinSetupReady(s,now)&&s.endpoint.independent()&&
   s.selected>=0&&strcasecmp(offeredPeer,s.trainers[s.selected].address);
 strlcpy(offer.peerAddress,offer.pending?requestedPeer:offeredPeer,sizeof(offer.peerAddress));
 strlcpy(offer.peerName,"TV-connected VOLTRA",sizeof(offer.peerName));
 strlcpy(offer.message,mixedMessage,sizeof(offer.message));return offer;
}
bool canRequestMixedTwin(){const auto offer=mixedTwinOffer();return offer.available&&!offer.pending&&nextMixedRequest!=0;}
bool requestMixedTwin(){
 if(!canRequestMixedTwin())return false;
 const auto s=vlink::voltraLink.state();mixedRequestId=nextMixedRequest++;
 mixedRequestEpoch=epoch;mixedRequestSession=s.session;mixedRequestAt=millis();mixedRequestAccepted=false;
 strlcpy(requestedPeer,offeredPeer,sizeof(requestedPeer));strlcpy(mixedMessage,"Waiting for TV",sizeof(mixedMessage));
 notifyMixedRequest();return true;
}
void loop(){
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();const uint32_t currentEpoch=epoch;
 if(currentEpoch!=lastEpoch){lastEpoch=currentEpoch;lastId=0;pending=false;
   clearMixedOffer();clearMixedRequest();nextMixedRequest=1;mixedMessage[0]=0;/* Old queued requests fail the epoch check. */}
 if((offerSession!=s.session)||!authenticated)clearMixedOffer();
 if(mixedRequestId&&(mixedRequestSession!=s.session||!authenticated)){clearMixedRequest();mixedMessage[0]=0;}
 if(mixedRequestId&&now-mixedRequestAt>=(mixedRequestAccepted?90000u:8000u)){
   clearMixedRequest();strlcpy(mixedMessage,"TV did not finish Twin; retry on TV",sizeof(mixedMessage));
 }
 if(mixedRequestId&&!mixedRequestAccepted&&now-lastMixedNotify>=750)notifyMixedRequest();
 if(forgettingTvs){
   if(peer==BLE_HS_CONN_HANDLE_NONE&&server->getConnectedCount()==0&&now-radioStoppedAt>=250)forgettingTvs=false;
   else return;
 }
 if(!authenticated){
   if(peer!=BLE_HS_CONN_HANDLE_NONE){
     // Bond encryption can take a moment; do not expire a reconnect immediately.
     if(!pairing()&&static_cast<int32_t>(now-connectedAt.load())>=30000)disconnect();
     return;
   }
   const bool available=pairing()||(trustedTv&&!findingTrainers());
   if(available){
     if(!accepting&&!NimBLEDevice::getAdvertising()->isAdvertising()&&server->getConnectedCount()==0&&
        !windowOpen(nextAdvertiseAt,now)){
       if(!advertise())nextAdvertiseAt=now+2000;
     }
   }else{
     if(accepting||NimBLEDevice::getAdvertising()->isAdvertising())stopAdvertising();
     // Let a late connection/disconnect callback settle before resuming scanning.
     if(s.discoveryPausedForTv&&server->getConnectedCount()==0&&now-radioStoppedAt>=250)
       vlink::voltraLink.setTvRadioActive(false);
   }
   return;
 }
 Command c;
 if(xQueueReceive(requests,&c,0)==pdTRUE){
   if(c.type==7&&validOffer(c,currentEpoch,s.session,offerSeen,offerDeadline,now)){
     offerEpoch=currentEpoch;offerSession=s.session;offerDeadline=c.deadline;offerSeen=true;offerReady=c.offerReady;
     strlcpy(offeredPeer,c.expectedPeer,sizeof(offeredPeer));
     vlink::voltraLink.updateTvTwinOffer(currentEpoch,s.session,c.deadline,c.expectedPeer,c.offerReady);
   }else if(c.type==9&&validRequest(c,currentEpoch,s.session,lastId,now)){
     lastId=c.id;
     if(mixedRequestId==c.value&&mixedRequestEpoch==currentEpoch&&mixedRequestSession==s.session){
       if(c.field==1){mixedRequestAccepted=true;strlcpy(mixedMessage,"Creating Twin via TV",sizeof(mixedMessage));}
       else {clearMixedRequest();strlcpy(mixedMessage,c.field==2?"Twin connected":"Twin failed; check trainers and retry",sizeof(mixedMessage));}
     }
   }
   else if(c.type==7||c.type==9){/* Expired offer/status cannot revive an old request. */}
   else if(pending||busy(s,now)||!validRequest(c,currentEpoch,s.session,lastId,now))sendResult(c,2);
   else {
     lastId=c.id;int actual=-1;uint32_t stamp=s.statusMs;
     if(c.type==3)value(s,c.field,actual,stamp);
     const bool twin=c.type==5||c.type==6;
     if(twin)stamp=s.twinCompleted;
     const bool accepted=twin?vlink::voltraLink.requestTwin(c.type==5,c.epoch,c.session,c.deadline,c.expectedPeer,c.handoff):
       vlink::voltraLink.requestTvSetting(c.epoch,c.session,c.deadline,c.type==2,c.field,c.value);
     if(!accepted)sendResult(c,2);
     else{pending=true;pendingCommand=c;pendingAt=now;pendingStamp=stamp;sendResult(c,0);}
   }
 }
 if(pending){
   int actual=s.mode;uint32_t stamp=s.statusMs;
   const bool known=pendingCommand.type==2||value(s,pendingCommand.field,actual,stamp);
   if(s.session!=pendingCommand.session){sendResult(pendingCommand,2);pending=false;}
   else if((pendingCommand.type==5||pendingCommand.type==6)&&s.twinCompleted!=pendingStamp){sendResult(pendingCommand,s.twinSucceeded?1:3);pending=false;}
   else if(pendingCommand.type<=3&&known&&vlink::fresh(s,now)&&stamp!=pendingStamp&&actual==pendingCommand.value){sendResult(pendingCommand,1);pending=false;}
   else if(now-pendingAt>=((pendingCommand.type==5||pendingCommand.type==6)?40000u:3500u)){sendResult(pendingCommand,3);pending=false;}
 }
 if(now-lastSnapshot>=250){lastSnapshot=now;publish(s,now);}
}
}
