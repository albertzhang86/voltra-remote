#pragma once
#include "voltra_protocol.h"
#include <cstdio>
#include <cstring>

// Native endpoint messages verified with an unloaded pair. Addresses are wire order,
// not NimBLE's internal little-endian address storage. No motor command is generated.
namespace voltra {
struct EndpointStatus {
  bool valid=false;
  uint8_t status=255;
  char local[18]{},peer[18]{};
  int battery=-1,peerBattery=-1;
  bool independent() const {return valid&&status==0&&!std::strcmp(peer,"00:00:00:00:00:00");}
  bool paired() const {return valid&&status==0x12&&std::strcmp(peer,"00:00:00:00:00:00")&&std::strcmp(local,peer);}
  bool known() const {return independent()||paired();}
};
inline void sealTwinFrame(uint8_t* p,size_t n){
  p[1]=uint8_t(n);
  uint8_t c8=0x77;for(int i=0;i<3;++i){c8^=p[i];for(int j=0;j<8;++j)c8=(c8>>1)^((c8&1)?0x8c:0);}p[3]=c8;
  uint16_t crc=0x3692;for(size_t i=0;i<n-2;++i){crc^=p[i];for(int j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0x8408:0);}
  p[n-2]=crc;p[n-1]=crc>>8;
}
inline Frame endpointFrame(uint8_t (&p)[13]){
  const uint8_t bytes[]={0x55,13,4,0,0xaa,0x10,0,0x20,0x20,0,0xa7,0,0};
  std::memcpy(p,bytes,sizeof(bytes));sealTwinFrame(p,sizeof(bytes));return {p,sizeof(bytes)};
}
inline bool twinFrame(uint8_t operation,const char* address,uint16_t sequence,uint8_t (&p)[20],Frame& out){
  if((operation!=1&&operation!=2)||!address||std::strlen(address)!=17)return false;
  unsigned parsed[6];int end=0;
  if(std::sscanf(address,"%2x:%2x:%2x:%2x:%2x:%2x%n",&parsed[0],&parsed[1],&parsed[2],&parsed[3],&parsed[4],&parsed[5],&end)!=6||end!=17)return false;
  uint8_t any=0;for(auto v:parsed){if(v>255)return false;any|=v;}if(!any)return false;
  const uint8_t header[]={0x55,20,4,0,0xaa,0x10,uint8_t(sequence),uint8_t(sequence>>8),0x20,0,0xa8,operation};
  std::memcpy(p,header,sizeof(header));for(int i=0;i<6;++i)p[12+i]=parsed[i];
  sealTwinFrame(p,sizeof(p));out={p,sizeof(p)};return true;
}
inline EndpointStatus decodeEndpoint(const uint8_t* p,size_t n){
  EndpointStatus e;
  if(!validFrame(p,n)||p[10]!=0xa7)return e;
  const int offset=(n==36&&p[2]==8&&p[11]==0)?12:(n==35&&p[2]==4)?11:0;
  if(!offset)return e;
  auto address=[&](char* out,int pos){std::snprintf(out,18,"%02x:%02x:%02x:%02x:%02x:%02x",p[pos],p[pos+1],p[pos+2],p[pos+3],p[pos+4],p[pos+5]);};
  address(e.local,offset+3);address(e.peer,offset+9);
  e.valid=std::strcmp(e.local,"00:00:00:00:00:00")!=0;e.status=p[offset+2];
  e.battery=p[offset]<=100?p[offset]:-1;e.peerBattery=p[offset+15]<=100?p[offset+15]:-1;
  return e;
}
inline bool twinReply(const uint8_t* p,size_t n,uint16_t sequence,uint8_t operation){
  return n==16&&validFrame(p,n)&&p[2]==8&&p[10]==0xa8&&p[11]==0&&
    (p[6]|uint16_t(p[7])<<8)==sequence&&p[12]==operation&&p[13]==0;
}
}
