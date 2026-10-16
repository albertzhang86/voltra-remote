#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Super AI Gym bridge v2. This is our protocol, not a Beyond Power service.
namespace tvbridge {
constexpr const char* SERVICE="aed90001-4174-4ab6-a910-72457c592900";
constexpr const char* COMMAND="aed90002-4174-4ab6-a910-72457c592900";
constexpr const char* UPDATES="aed90003-4174-4ab6-a910-72457c592900";
constexpr uint8_t MAGIC=0x4b,VERSION=2;
constexpr size_t SNAPSHOT_BASE=134,MAX_SNAPSHOT=200;
// Byte 3 bit 5: CREATE_TWIN can bind the operation to an exact peer MAC.
constexpr uint8_t TWIN_IDENTITY_GUARD_FLAG=0x20;
constexpr uint8_t HANDOFF_CANDIDATE_FLAG=0x80;
constexpr uint8_t MIXED_TWIN_HANDOFF_FLAG=0x40; // Snapshot byte 3, not weight flags.
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
struct Command {uint8_t type=0;uint32_t epoch=0,session=0,deadline=0;uint16_t id=0,field=0;int32_t value=0;char expectedPeer[18]{};bool handoff=false,offerReady=false;};
inline bool decode(const uint8_t* p,size_t size,Command& out){
 if(!p||size!=24||p[0]!=MAGIC||p[1]!=VERSION||(p[2]!=2&&p[2]!=3&&p[2]!=5&&p[2]!=6&&p[2]!=7&&p[2]!=9)||(p[3]!=0&&!((p[2]==5||p[2]==7)&&p[3]==1)))return false;
 out={p[2],u32(p+4),u32(p+8),u32(p+12),u16(p+16),u16(p+18),static_cast<int32_t>(u32(p+20))};
 if((out.type==5||out.type==7)&&(out.field!=0||out.value!=0)){
   std::snprintf(out.expectedPeer,sizeof(out.expectedPeer),"%02X:%02X:%02X:%02X:%02X:%02X",p[18],p[19],p[20],p[21],p[22],p[23]);
   out.field=0;out.value=0;
 }
 out.handoff=out.type==5&&p[3]==1;out.offerReady=out.type==7&&p[3]==1;
 if(out.handoff&&!out.expectedPeer[0])return false;
 if(out.type==7)return out.id==0&&(!out.offerReady||out.expectedPeer[0]);
 if(out.type==9)return out.id!=0&&out.field>=1&&out.field<=3&&out.value>0&&out.value<=65535;
 return out.id!=0&&((out.type!=2&&out.type!=5&&out.type!=6)||out.field==0)&&((out.type!=5&&out.type!=6)||out.value==0);
}
inline bool encodeAddress(const char* address,uint8_t* out){
 unsigned int b[6];char trailing;
 if(!address||std::sscanf(address,"%2x:%2x:%2x:%2x:%2x:%2x%c",b,b+1,b+2,b+3,b+4,b+5,&trailing)!=6)return false;
 for(int i=0;i<6;++i)out[i]=static_cast<uint8_t>(b[i]);return true;
}
inline bool twinRequest(uint8_t* out,uint32_t epoch,uint32_t session,uint16_t id,const char* peer){
 if(!epoch||!id||!encodeAddress(peer,out+14))return false;
 out[0]=MAGIC;out[1]=VERSION;out[2]=8;out[3]=0;
 put32(out+4,epoch);put32(out+8,session);put16(out+12,id);return true;
}
inline bool validRequest(const Command& c,uint32_t epoch,uint32_t session,uint16_t lastId,uint32_t now){
 const int32_t remaining=static_cast<int32_t>(c.deadline-now);
 return epoch&&c.epoch==epoch&&c.session==session&&c.id>lastId&&remaining>0&&remaining<=2000;
}
// Offers use their expiry as an independent monotonic heartbeat sequence. They
// never advance the finite motor-command ID counter and cannot extend validity
// by replaying the same deadline, including across the millis() wrap boundary.
inline bool validOffer(const Command& c,uint32_t epoch,uint32_t session,bool seen,uint32_t previousDeadline,uint32_t now){
 const int32_t remaining=static_cast<int32_t>(c.deadline-now);
 return c.type==7&&c.id==0&&epoch&&c.epoch==epoch&&c.session==session&&remaining>0&&remaining<=2000&&
   (!seen||static_cast<int32_t>(c.deadline-previousDeadline)>0);
}
inline void result(uint8_t* p,const Command& c,uint8_t code){
 for(int i=0;i<16;++i)p[i]=0;
 p[0]=MAGIC;p[1]=VERSION;p[2]=4;p[3]=code;put32(p+4,c.epoch);put32(p+8,c.session);put16(p+12,c.id);
}
}
