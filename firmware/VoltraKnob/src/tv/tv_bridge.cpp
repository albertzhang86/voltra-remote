#include "tv_bridge.h"
#include "bridge_protocol.h"
#include "../link/voltra_link.h"
#include "../protocol/voltra_protocol.h"
#include "../board/board.h"
#include "../ui/ui.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <cstring>

namespace tvbridge {
namespace {
NimBLEServer* server=nullptr;
NimBLECharacteristic* updates=nullptr;
QueueHandle_t requests;
std::atomic<uint16_t> peer{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> epoch{0},pairUntil{0};
std::atomic<int> shownPasskey{-1};
std::atomic<bool> authenticated{false};
uint16_t lastId=0;
uint32_t lastEpoch=0,lastSnapshot=0,pendingAt=0,pendingStamp=0;
Command pendingCommand;
bool pending=false;
constexpr uint16_t fields[]={0x3e86,0x3e87,0x53b0,0x3e88,0x5362,0x5103,0x5350,0x5410,0x5411,0x5412,0x5413};

bool busy(const vlink::DeviceState& s,uint32_t now){return s.parameterSelectionPending||
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
 if(busy(s,now)||s.twinConnection==2||!s.twinMs||now-s.twinMs>=3500)return false;
 if(index==0)return vlink::canEditParameter(s,now,vlink::Parameter::Weight);
 if(index<4)return vlink::canEdit(s,now);
 if(index==4)return vlink::canEditBand(s,now);
 if(index==5)return vlink::canEditDamper(s,now);
 return vlink::canEditIsokinetic(s,now,voltra::modeParameterIndex(fields[index]));
}
bool sameMode(int mode,int index){return index<4?mode==1:index==4?mode==2:index==5?mode==4:mode==7;}
void sendResult(const Command& c,uint8_t code){uint8_t bytes[16];result(bytes,c,code);if(authenticated)updates->notify(bytes,sizeof(bytes),peer);}

class ServerCallbacks:public NimBLEServerCallbacks {
 void onConnect(NimBLEServer* s,NimBLEConnInfo& info) override {
   if(!pairing()||peer.load()!=BLE_HS_CONN_HANDLE_NONE){s->disconnect(info);return;}
   peer=info.getConnHandle();epoch=esp_random()|1u;shownPasskey=-1;
   if(!NimBLEDevice::startSecurity(info.getConnHandle()))s->disconnect(info);
 }
 uint32_t onPassKeyDisplay() override {const uint32_t pin=esp_random()%1000000;shownPasskey=pin;return pin;}
 void onAuthenticationComplete(NimBLEConnInfo& info) override {
   if(info.getConnHandle()!=peer||!info.isEncrypted()||!info.isAuthenticated()){server->disconnect(info);return;}
   authenticated=true;shownPasskey=-1;pairUntil=0;
   vlink::voltraLink.setTvSession(epoch);
 }
 void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int) override {
   if(info.getConnHandle()!=peer)return;
   authenticated=false;epoch=0;peer=BLE_HS_CONN_HANDLE_NONE;shownPasskey=-1;
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
   (s.twinMs&&now-s.twinMs<3500?8:0)|(s.twinConnection==2?16:0);
 put32(bytes+4,epoch);put32(bytes+8,s.session);put32(bytes+12,now);
 bytes[16]=s.selected<0?255:s.selected;
 const int battery=board::batteryPercent();bytes[17]=battery<0?255:battery;bytes[18]=s.battery<0?255:s.battery;
 bytes[19]=s.mode<0?255:s.mode;put16(bytes+20,s.fitnessMode<0?65535:s.fitnessMode);
 const bool independent=s.twinConnection!=2;
 put32(bytes+22,fresh&&independent&&s.forceMs&&now-s.forceMs<2000?s.activeWeightTenths:-1);
 put16(bytes+26,fresh&&independent&&s.repMs?s.reps:65535);
 bytes[30]=fresh&&!busy(s,now)&&independent&&s.twinMs&&now-s.twinMs<3500&&vlink::canSwitch(s,now)&&voltra::fitnessModeUnloaded(s.fitnessMode);
 if(s.selected>=0&&s.selected<2){
   memcpy(bytes+32,s.trainers[s.selected].name,31);memcpy(bytes+64,s.trainers[s.selected].address,17);
 }
 uint16_t mask=0;uint8_t count=0;
 for(int i=0;i<11;++i){
   int actual;uint32_t stamp;
   if(!sameMode(s.mode,i)||!value(s,fields[i],actual,stamp)||!stamp||now-stamp>=8000)continue;
   const int offset=82+count*6;put16(bytes+offset,fields[i]);put32(bytes+offset+2,actual);++count;
   if(editable(s,i,now))mask|=1u<<i;
 }
 put16(bytes+28,mask);bytes[31]=count;
 if(server->getPeerMTU(peer)>=185)updates->notify(bytes,82+count*6,peer);
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
}
void openPairing(){
 const auto s=vlink::voltraLink.state();
 if(connected()||!vlink::canSwitch(s,millis()))return;
 pairUntil=millis()+120000;shownPasskey=-1;server->startAdvertising(120000);
}
void disconnect(){
 pairUntil=0;authenticated=false;epoch=0;shownPasskey=-1;vlink::voltraLink.setTvSession(0);
 server->stopAdvertising();const auto handle=peer.load();if(handle!=BLE_HS_CONN_HANDLE_NONE)server->disconnect(handle);
}
bool connected(){return authenticated;}
bool pairing(){const uint32_t until=pairUntil;return until&&static_cast<int32_t>(until-millis())>0;}
int passkey(){return shownPasskey;}
void loop(){
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();const uint32_t currentEpoch=epoch;
 if(currentEpoch!=lastEpoch){lastEpoch=currentEpoch;lastId=0;pending=false;/* Old queued requests fail the epoch check. */}
 if(!authenticated){if(peer!=BLE_HS_CONN_HANDLE_NONE&&!pairing())disconnect();return;}
 Command c;
 if(xQueueReceive(requests,&c,0)==pdTRUE){
   if(pending||busy(s,now)||!validRequest(c,currentEpoch,s.session,lastId,now))sendResult(c,2);
   else {
     lastId=c.id;int actual=-1;uint32_t stamp=s.statusMs;
     if(c.type==3)value(s,c.field,actual,stamp);
     const bool accepted=vlink::voltraLink.requestTvSetting(c.epoch,c.session,c.deadline,c.type==2,c.field,c.value);
     if(!accepted)sendResult(c,2);
     else{pending=true;pendingCommand=c;pendingAt=now;pendingStamp=stamp;sendResult(c,0);}
   }
 }
 if(pending){
   int actual=s.mode;uint32_t stamp=s.statusMs;
   const bool known=pendingCommand.type==2||value(s,pendingCommand.field,actual,stamp);
   if(s.session!=pendingCommand.session){sendResult(pendingCommand,2);pending=false;}
   else if(known&&vlink::fresh(s,now)&&stamp!=pendingStamp&&actual==pendingCommand.value){sendResult(pendingCommand,1);pending=false;}
   else if(now-pendingAt>=3500){sendResult(pendingCommand,3);pending=false;}
 }
 if(now-lastSnapshot>=250){lastSnapshot=now;publish(s,now);}
}
}
