#pragma once
#include <cstddef>
#include <cstdint>

// Super AI Gym bridge v1. This is our protocol, not a Beyond Power service.
namespace tvbridge {
constexpr const char* SERVICE="aed90001-4174-4ab6-a910-72457c592900";
constexpr const char* COMMAND="aed90002-4174-4ab6-a910-72457c592900";
constexpr const char* UPDATES="aed90003-4174-4ab6-a910-72457c592900";
constexpr uint8_t MAGIC=0x4b,VERSION=1;
constexpr size_t SNAPSHOT_BASE=82,MAX_SNAPSHOT=148;
inline uint16_t u16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
inline uint32_t u32(const uint8_t* p){return u16(p)|uint32_t(u16(p+2))<<16;}
inline void put16(uint8_t* p,uint16_t v){p[0]=v;p[1]=v>>8;}
inline void put32(uint8_t* p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
struct Command {uint8_t type=0;uint32_t epoch=0,session=0,deadline=0;uint16_t id=0,field=0;int32_t value=0;};
inline bool decode(const uint8_t* p,size_t size,Command& out){
 if(!p||size!=24||p[0]!=MAGIC||p[1]!=VERSION||(p[2]!=2&&p[2]!=3)||p[3]!=0)return false;
 out={p[2],u32(p+4),u32(p+8),u32(p+12),u16(p+16),u16(p+18),static_cast<int32_t>(u32(p+20))};
 return out.id!=0&&(out.type!=2||out.field==0);
}
inline bool validRequest(const Command& c,uint32_t epoch,uint32_t session,uint16_t lastId,uint32_t now){
 const int32_t remaining=static_cast<int32_t>(c.deadline-now);
 return epoch&&c.epoch==epoch&&c.session==session&&c.id>lastId&&remaining>0&&remaining<=2000;
}
inline void result(uint8_t* p,const Command& c,uint8_t code){
 for(int i=0;i<16;++i)p[i]=0;
 p[0]=MAGIC;p[1]=VERSION;p[2]=4;p[3]=code;put32(p+4,c.epoch);put32(p+8,c.session);put16(p+12,c.id);
}
}
