#pragma once
#include <cstddef>
#include <cstdint>

// Super AI Gym bridge v2. This is our protocol, not a Beyond Power service.
namespace tvbridge {
constexpr const char* SERVICE="aed90001-4174-4ab6-a910-72457c592900";
constexpr const char* COMMAND="aed90002-4174-4ab6-a910-72457c592900";
constexpr const char* UPDATES="aed90003-4174-4ab6-a910-72457c592900";
constexpr uint8_t MAGIC=0x4b,VERSION=2;
constexpr size_t SNAPSHOT_BASE=134,MAX_SNAPSHOT=200;
// Optional v2 flags at byte 133, bits 2..3: 0 unknown, 1 lb, 2 kg.
// Legacy bit 4 marked runtime wire weight; older unmarked values were cable force.
// New snapshots never use those measurements for active resistance.
constexpr uint8_t RUNTIME_WEIGHT_FLAG=0x10;
// Byte 133 bit 5: a USB host is supplying power to the knob. This does not
// assert that a battery is installed, actively charging, or already full.
constexpr uint8_t USB_POWER_FLAG=0x20;
// Byte 133 bit 6: byte 22 is the device target resistance in tenths of lb,
// or explicit zero when fitness state 4 reports unloaded.
constexpr uint8_t TARGET_FORCE_FLAG=0x40;
// Byte 133 bit 7: byte 22 is measured force from a fresh, loaded, moving B4
// sample. Mutually exclusive with the runtime/target flags; length stays v2.
constexpr uint8_t MOVING_FORCE_FLAG=0x80;
// Weight Training on this knob uses the verified half-kg display scale.
inline uint8_t weightUnitFlags(int mode,int rawUnit){
 const int unit=mode==1?1:rawUnit;
 return unit==0?4:unit==1?8:0;
}
inline uint16_t u16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
inline uint32_t u32(const uint8_t* p){return u16(p)|uint32_t(u16(p+2))<<16;}
inline void put16(uint8_t* p,uint16_t v){p[0]=v;p[1]=v>>8;}
inline void put32(uint8_t* p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
// 0xffff means unavailable in our bridge, not in the trainer's rep-index schema.
inline uint16_t encodedRepCount(int count){return count>=0&&count<65535?static_cast<uint16_t>(count):65535;}
inline void writeActiveWeight(uint8_t* snapshot,int32_t targetTenths){
 put32(snapshot+22,targetTenths);
 snapshot[133]&=~(RUNTIME_WEIGHT_FLAG|TARGET_FORCE_FLAG|MOVING_FORCE_FLAG);
 if(targetTenths>=0)snapshot[133]|=TARGET_FORCE_FLAG;
}
inline void writeMovingWeight(uint8_t* snapshot,int32_t forceTenths){
 put32(snapshot+22,forceTenths);
 snapshot[133]&=~(RUNTIME_WEIGHT_FLAG|TARGET_FORCE_FLAG|MOVING_FORCE_FLAG);
 if(forceTenths>=0)snapshot[133]|=MOVING_FORCE_FLAG;
}
struct Command {uint8_t type=0;uint32_t epoch=0,session=0,deadline=0;uint16_t id=0,field=0;int32_t value=0;};
inline bool decode(const uint8_t* p,size_t size,Command& out){
 if(!p||size!=24||p[0]!=MAGIC||p[1]!=VERSION||(p[2]!=2&&p[2]!=3&&p[2]!=5&&p[2]!=6)||p[3]!=0)return false;
 out={p[2],u32(p+4),u32(p+8),u32(p+12),u16(p+16),u16(p+18),static_cast<int32_t>(u32(p+20))};
 return out.id!=0&&((out.type!=2&&out.type!=5&&out.type!=6)||out.field==0)&&((out.type!=5&&out.type!=6)||out.value==0);
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
