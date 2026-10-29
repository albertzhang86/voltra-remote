#pragma once
#include "voltra_link.h"

namespace vlink {
enum class BackgroundRead:uint8_t {None,Settings,Endpoint,Cable,ModeParameter,RuntimeWeight,WeightUnit,LengthUnit,MaximumForce};
inline const char* backgroundReadName(BackgroundRead read){
 switch(read){
 case BackgroundRead::Settings:return "settings";
 case BackgroundRead::Endpoint:return "endpoint";
 case BackgroundRead::Cable:return "cable";
 case BackgroundRead::ModeParameter:return "mode-config";
 case BackgroundRead::RuntimeWeight:return "wire-force";
 case BackgroundRead::WeightUnit:return "weight-unit";
 case BackgroundRead::LengthUnit:return "length-unit";
 case BackgroundRead::MaximumForce:return "maximum-force";
 default:return "none";
 }
}
inline bool freshIndependentForceStream(const DeviceState& s,uint32_t now){
 return fresh(s,now)&&endpointFresh(s,now)&&s.endpoint.independent()&&voltra::supportedMode(s.mode)&&
   s.motionMs&&s.motionMs==s.forceMs&&now-s.motionMs<500&&
   s.activeWeightTenths>=0&&s.activeWeightTenths<=10000;
}
// One ordinary query per slot, with no catch-up burst after a slow ATT write.
// Foreground commands keep their own explicit confirmation reads and run first.
struct BackgroundPollSchedule {
 static constexpr uint32_t SLOT_MS=100;
 uint32_t last[8]{};bool issued[8]{};
 uint32_t quietSince=0;bool hasQuietStart=false,started=false;
 int configCursor=0,observedMode=-1;
 void writeCompleted(uint32_t now){quietSince=now;hasQuietStart=true;}
 BackgroundRead next(const DeviceState& s,uint32_t now,bool foreground){
   if(foreground||(hasQuietStart&&now-quietSince<SLOT_MS))return BackgroundRead::None;
   if(!started){started=true;for(auto& stamp:last)stamp=now;}
   if(s.mode!=observedMode){
     observedMode=s.mode;configCursor=0;issued[3]=false;last[3]=now;
   }
   const bool modeConfig=s.mode==2||s.mode==4||s.mode==7;
   const bool wire=endpointFresh(s,now)&&s.endpoint.independent()&&voltra::supportedMode(s.mode)&&
     !freshIndependentForceStream(s,now);
   const bool enabled[]={true,true,true,modeConfig,wire,true,true,true};
   const uint32_t interval[]={1000,1000,1000,1000,1000,5000,5000,5000};
   // Oldest due read wins; status/endpoint/cable win equal deadlines. Fixed
   // priority starves units under slow ACKs. The diagnostic wire fallback can
   // run at 1 Hz; actual displayed live force always comes from notifications.
   int selected=-1;uint32_t oldest=0;
   for(unsigned i=0;i<8;++i)if(enabled[i]&&i!=4){
     const uint32_t age=now-last[i];
     if(issued[i]&&age<interval[i])continue;
     const uint32_t overdue=issued[i]?age-interval[i]:age;
     if(selected<0||overdue>oldest){selected=i;oldest=overdue;}
   }
   // The wire register is diagnostic only; it must not starve configuration
   // or unit reads when acknowledged writes consume most available slots.
   if(selected<0&&wire&&(!issued[4]||now-last[4]>=interval[4]))selected=4;
   if(selected<0)return BackgroundRead::None;
   issued[selected]=true;last[selected]=now;writeCompleted(now);
   return static_cast<BackgroundRead>(selected+1);
 }
 int nextModeParameter(int mode){
   for(int tried=0;tried<voltra::MODE_PARAMETER_COUNT;++tried){
     const int index=configCursor;configCursor=(configCursor+1)%voltra::MODE_PARAMETER_COUNT;
     if(voltra::MODE_PARAMETERS[index].mode==mode)return index;
   }
   return -1;
 }
};
}
