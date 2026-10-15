#include "tv/bridge_protocol.h"
#include "tv/connection_policy.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace tvbridge;
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
 assert(weightUnitFlags(1,0)==8);assert(weightUnitFlags(1,-1)==8);
 assert(weightUnitFlags(2,0)==4);assert(weightUnitFlags(2,1)==8);
 assert(weightUnitFlags(7,-1)==0);assert(weightUnitFlags(7,2)==0);
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
 assert(!decode(bytes,23,c));bytes[2]=7;assert(!decode(bytes,24,c));bytes[2]=3;
 c.deadline=1000;assert(validRequest(c,123,7,0,0xfffffff0));
 uint8_t reply[16];result(reply,c,1);assert(reply[2]==4&&reply[3]==1&&u16(reply+12)==1);
 bytes[2]=5;put16(bytes+18,0);put32(bytes+20,0);assert(decode(bytes,24,c));
 bytes[2]=6;assert(decode(bytes,24,c));put32(bytes+20,1);assert(!decode(bytes,24,c));
 std::cout<<"TV bridge protocol tests passed\n";
}
