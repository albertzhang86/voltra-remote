#include "tv/bridge_protocol.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace tvbridge;
 uint8_t bytes[24]={MAGIC,VERSION,3,0};put32(bytes+4,123);put32(bytes+8,7);put32(bytes+12,3000);
 put16(bytes+16,1);put16(bytes+18,0x3e86);put32(bytes+20,35);
 Command c;assert(decode(bytes,sizeof(bytes),c));assert(c.value==35&&c.field==0x3e86);
 assert(validRequest(c,123,7,0,1000));
 assert(!validRequest(c,124,7,0,1000));assert(!validRequest(c,123,8,0,1000));
 assert(!validRequest(c,123,7,1,1000));assert(!validRequest(c,123,7,0,3000));
 assert(!validRequest(c,123,7,0,999));
 assert(!decode(bytes,23,c));bytes[2]=5;assert(!decode(bytes,24,c));bytes[2]=3;
 c.deadline=1000;assert(validRequest(c,123,7,0,0xfffffff0));
 uint8_t reply[16];result(reply,c,1);assert(reply[2]==4&&reply[3]==1&&u16(reply+12)==1);
 std::cout<<"TV bridge protocol tests passed\n";
}
