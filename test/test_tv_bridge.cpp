#include "tv/bridge_protocol.h"
#include "tv/connection_policy.h"
#include "tv/setup_activity.h"
#include <cassert>
#include <iostream>
#include <cstring>
int main(){
 using namespace tvbridge;
 {
  using A=remote::SetupActivity;
  SetupActivityLease lease;
  Command activity;activity.type=11;activity.epoch=7;activity.session=3;
  activity.deadline=2500;activity.field=2;
  assert(lease.accept(activity,7,3,1000));assert(lease.current(1000)==A::CreateTwin);
  assert(!lease.accept(activity,7,3,1100)); // Replay cannot extend the display.
  assert(!lease.accept(activity,8,3,1100));assert(!lease.accept(activity,7,4,1100));
  assert(lease.current(8499)==A::CreateTwin);assert(lease.current(8500)==A::None);
  activity.deadline=10000;activity.session=4;activity.field=3;
  assert(lease.accept(activity,7,4,8500));assert(lease.current(8500)==A::ExitTwin);
  activity.deadline=10001;activity.field=0;
  assert(lease.accept(activity,7,4,8501));assert(lease.current(8501)==A::None);
  activity.field=2;activity.deadline=10000;
  assert(!lease.accept(activity,7,4,8501));assert(lease.current(8501)==A::None);
  activity.deadline=11000;activity.field=6;assert(!lease.accept(activity,7,4,9500));
  activity.field=1;activity.id=1;assert(!lease.accept(activity,7,4,9500));
  activity.id=0;activity.value=1;assert(!lease.accept(activity,7,4,9500));
  activity.value=0;assert(!lease.accept(activity,7,4,11000));
  assert(!lease.accept(activity,7,4,8999));
  activity.epoch=8;activity.deadline=500;
  assert(lease.accept(activity,8,4,UINT32_MAX-999));assert(lease.current(0)==A::Connecting);
  assert(lease.current(6500)==A::None);
  uint8_t packet[24]={MAGIC,VERSION,11,0};put32(packet+4,8);put32(packet+8,4);
  put32(packet+12,500);put16(packet+18,3);Command decoded;
  assert(decode(packet,sizeof(packet),decoded));assert(decoded.type==11&&decoded.field==3);
  assert(!validRequest(decoded,8,4,0,0)); // No control command ID.
 }
 // A saved bond enables identity-resolution handshakes with pairing closed.
 assert(allowConnection(true,false,true,false));
 assert(!allowConnection(true,false,false,false));
 assert(allowConnection(true,true,false,false));
 assert(!allowConnection(false,true,true,false));
 assert(!allowConnection(true,true,true,true));
 assert(allowAuthentication(true,true,true,true,false,false));
 assert(allowAuthentication(true,true,true,false,true,false));
 assert(!allowAuthentication(true,true,true,false,false,false));
 // Even an admitted handshake cannot expose data for an unresolved/untrusted TV.
 assert(!allowAuthentication(true,true,true,false,false,true));
 assert(!allowAuthentication(true,true,true,true,false,true));
 assert(!allowAuthentication(false,true,true,true,false,false));
 assert(!allowAuthentication(true,false,true,true,false,false));
 assert(!allowAuthentication(true,true,false,true,false,false));
 assert(!windowOpen(0,1));assert(windowOpen(100,99));assert(!windowOpen(100,100));
 assert(windowOpen(100,UINT32_MAX-10));
 assert(weightUnitFlags(0)==4);assert(weightUnitFlags(-1)==0);
 assert(weightUnitFlags(0)==4);assert(weightUnitFlags(1)==8);
 assert(weightUnitFlags(-1)==0);assert(weightUnitFlags(2)==0);
 assert(encodedRepCount(-1)==65535);assert(encodedRepCount(0)==0);
 assert(encodedRepCount(1)==1);assert(encodedRepCount(65534)==65534);
 assert(encodedRepCount(65535)==65535);assert(encodedRepCount(65536)==65535);
 uint8_t repBytes[2];put16(repBytes,encodedRepCount(65536));assert(u16(repBytes)==65535);
 uint8_t snapshot[SNAPSHOT_BASE]{};snapshot[133]=USB_POWER_FLAG|RUNTIME_WEIGHT_FLAG|8;
 writeActiveWeight(snapshot,60);assert(u32(snapshot+22)==60);
 assert(snapshot[133]==(USB_POWER_FLAG|TARGET_FORCE_FLAG|8));
 writeActiveWeight(snapshot,0);assert(u32(snapshot+22)==0);assert(snapshot[133]&TARGET_FORCE_FLAG);
 writeActiveWeight(snapshot,-1);assert(u32(snapshot+22)==0xffffffff);
 assert(snapshot[133]==(USB_POWER_FLAG|8)); // missing target has no fallback marker
 writeMovingWeight(snapshot,83);assert(u32(snapshot+22)==83);
 assert(snapshot[133]==(USB_POWER_FLAG|MOVING_FORCE_FLAG|8));
 writeActiveWeight(snapshot,60);assert(snapshot[133]==(USB_POWER_FLAG|TARGET_FORCE_FLAG|8));
 writeMovingWeight(snapshot,0);assert(u32(snapshot+22)==0);assert(snapshot[133]&MOVING_FORCE_FLAG);
 writeMovingWeight(snapshot,-1);assert(u32(snapshot+22)==0xffffffff);
 assert(snapshot[133]==(USB_POWER_FLAG|8));
 uint8_t bytes[24]={MAGIC,VERSION,3,0};put32(bytes+4,123);put32(bytes+8,7);put32(bytes+12,3000);
 put16(bytes+16,1);put16(bytes+18,0x3e86);put32(bytes+20,35);
 Command c;assert(decode(bytes,sizeof(bytes),c));assert(c.value==35&&c.field==0x3e86);
 assert(validRequest(c,123,7,0,1000));
 assert(!validRequest(c,124,7,0,1000));assert(!validRequest(c,123,8,0,1000));
 assert(!validRequest(c,123,7,1,1000));assert(!validRequest(c,123,7,0,3000));
 assert(!validRequest(c,123,7,0,999));
 // Reopening the app can retain its physical encrypted BLE connection. Its
 // explicit CCCD 0 -> 1 renews the control epoch without weakening replay checks.
 assert(subscriptionChange(true,true,true,0)==SubscriptionChange::Revoke);
 assert(!logicalSessionReady(true,false,0));
 assert(!validRequest(c,0,7,0,1000)); // Notifications off never authorizes writes.
 assert(subscriptionChange(true,true,false,1)==SubscriptionChange::Renew);
 const uint32_t newEpoch=renewedEpoch(123,123);assert(newEpoch!=0&&newEpoch!=123);
 assert(logicalSessionReady(true,true,newEpoch));
 assert(!validRequest(c,newEpoch,7,0,1000)); // Old queued command is revoked.
 Command renewed=c;renewed.epoch=newEpoch;
 assert(validRequest(renewed,newEpoch,7,0,1000)); // New app starts with command 1.
 assert(!validRequest(renewed,newEpoch,7,1,1000)); // Same-epoch replay still rejected.
 assert(!std::strcmp(requestRejection(renewed,newEpoch,7,1,1000),"replay"));
 assert(!std::strcmp(requestRejection(c,newEpoch,7,0,1000),"epoch"));
 assert(!std::strcmp(requestRejection(c,123,7,0,3000),"expired"));
 assert(!std::strcmp(requestRejection(c,123,7,0,999),"future-deadline"));
 assert(!std::strcmp(requestRejection(c,123,8,0,1000),"trainer-session"));
 assert(subscriptionChange(true,true,true,1)==SubscriptionChange::Ignore); // Duplicate enable.
 assert(subscriptionChange(true,true,true,3)==SubscriptionChange::Ignore);
 assert(subscriptionChange(true,true,true,2)==SubscriptionChange::Revoke); // Notification bit off.
 for(uint16_t setting:{0,1,2,3}){
   assert(subscriptionChange(false,true,true,setting)==SubscriptionChange::Ignore);
   assert(subscriptionChange(true,false,true,setting)==SubscriptionChange::Ignore);
   assert(subscriptionChange(true,false,false,setting)==SubscriptionChange::Ignore);
 }
 assert(subscriptionChange(true,true,true,4)==SubscriptionChange::Ignore);
 assert(!logicalSessionReady(false,true,newEpoch));assert(!logicalSessionReady(true,false,newEpoch));
 assert(renewedEpoch(0,0)==1);assert(renewedEpoch(UINT32_MAX,UINT32_MAX)==1);
 assert(!decode(bytes,23,c));bytes[2]=11;assert(!decode(bytes,24,c));bytes[2]=3;
 c.deadline=1000;assert(validRequest(c,123,7,0,0xfffffff0));
 uint8_t reply[16];result(reply,c,1);assert(reply[2]==4&&reply[3]==1&&u16(reply+12)==1);
 // Explicit motor command is capability-gated by the sender. Its envelope
 // keeps the normal replay/deadline/session rules, with no arbitrary registers.
 bytes[2]=10;put16(bytes+18,0);put32(bytes+20,1);assert(decode(bytes,24,c));
 assert(c.type==10&&c.value==1&&c.field==0);
 assert(validRequest(c,123,7,0,1000));assert(!validRequest(c,123,7,1,1000));
 put32(bytes+20,0);assert(decode(bytes,24,c));
 put32(bytes+20,2);assert(!decode(bytes,24,c));put32(bytes+20,0xffffffff);assert(!decode(bytes,24,c));
 put32(bytes+20,1);put16(bytes+18,0x3e89);assert(!decode(bytes,24,c));put16(bytes+18,0);
 bytes[3]=1;assert(!decode(bytes,24,c));bytes[3]=0;put16(bytes+16,0);assert(!decode(bytes,24,c));put16(bytes+16,1);
 assert(decode(bytes,24,c));result(reply,c,3,2);assert(reply[3]==3&&u16(reply+14)==2);
 assert((MOTOR_CONTROL_FLAG|LOAD_READY_FLAG|UNLOAD_READY_FLAG)==0xe000);
 assert(((MOTOR_CONTROL_FLAG|LOAD_READY_FLAG|UNLOAD_READY_FLAG)&0x07ff)==0);
 assert(LOAD_PREPARE_FLAG==0x1000);
 assert((LOAD_PREPARE_FLAG&(MOTOR_CONTROL_FLAG|LOAD_READY_FLAG|UNLOAD_READY_FLAG|0x07ff))==0);
 bytes[2]=5;put16(bytes+18,0);put32(bytes+20,0);assert(decode(bytes,24,c));
 assert(c.expectedPeer[0]==0); // Old clients retain unguarded native behavior.
 const uint8_t peer[6]={1,2,3,4,5,7};std::memcpy(bytes+18,peer,6);
 assert(decode(bytes,24,c));assert(!std::strcmp(c.expectedPeer,"01:02:03:04:05:07"));
 const Command queued=c; // Queue copies own the peer guard; a later command cannot replace it.
 bytes[23]=8;assert(decode(bytes,24,c));
 assert(!std::strcmp(queued.expectedPeer,"01:02:03:04:05:07"));
 assert(!std::strcmp(c.expectedPeer,"01:02:03:04:05:08"));
 std::memset(bytes+18,0,6);
 bytes[2]=6;assert(decode(bytes,24,c));put32(bytes+20,1);assert(!decode(bytes,24,c));
 bytes[2]=7;put16(bytes+16,0);bytes[3]=1;std::memcpy(bytes+18,peer,6);assert(decode(bytes,24,c));
 assert(c.offerReady);
 assert(!std::strcmp(c.expectedPeer,"01:02:03:04:05:07"));
 std::memset(bytes+18,0,6);assert(!decode(bytes,24,c));bytes[3]=0;assert(decode(bytes,24,c)&&!c.expectedPeer[0]);
 bytes[2]=9;put16(bytes+16,1);put16(bytes+18,1);put32(bytes+20,42);assert(decode(bytes,24,c));
 put16(bytes+18,4);assert(!decode(bytes,24,c));put16(bytes+18,3);
 put32(bytes+20,0);assert(!decode(bytes,24,c));put32(bytes+20,65536);assert(!decode(bytes,24,c));
 // Authenticated handoff is explicitly marked, never inferred from a remembered peer.
 bytes[2]=5;bytes[3]=1;std::memcpy(bytes+18,peer,6);assert(decode(bytes,24,c)&&c.handoff);
 std::memset(bytes+18,0,6);assert(!decode(bytes,24,c));
 bytes[2]=6;assert(!decode(bytes,24,c));bytes[2]=3;assert(!decode(bytes,24,c));
 // Offer IDs never consume the normal control sequence; only newer, short-lived
 // deadlines in the current authenticated epoch and trainer session are accepted.
 bytes[2]=7;bytes[3]=1;put16(bytes+16,0);std::memcpy(bytes+18,peer,6);
 put32(bytes+12,2500);assert(decode(bytes,24,c));
 assert(validOffer(c,123,7,false,0,1000));
 assert(validOffer(c,123,7,true,2499,1000));
 assert(!validOffer(c,123,7,true,2500,1000));
 assert(!validOffer(c,123,7,true,2501,1000));
 assert(!validOffer(c,124,7,false,0,1000));assert(!validOffer(c,123,8,false,0,1000));
 assert(!validOffer(c,123,7,false,0,2500));assert(!validOffer(c,123,7,false,0,499));
 c.id=1;assert(!validOffer(c,123,7,false,0,1000));c.id=0;
 c.deadline=500;assert(validOffer(c,123,7,true,0xfffffff0,0xffffff00));
 assert(!validOffer(c,123,7,true,500,0xffffff00));
 c.deadline=0xfffffff0;assert(!validOffer(c,123,7,true,500,0xffffff00));
 c.deadline=0;assert(validOffer(c,123,7,true,0xfffffff0,0xffffff00));
 uint8_t request[20];assert(twinRequest(request,123,7,42,"01:02:03:04:05:07"));
 assert(request[2]==8&&u32(request+4)==123&&u32(request+8)==7&&u16(request+12)==42);
 assert(!std::memcmp(request+14,peer,6));assert(!twinRequest(request,123,7,0,"01:02:03:04:05:07"));
 assert(!twinRequest(request,123,7,1,"bad address"));
 std::cout<<"TV bridge protocol tests passed\n";
}
