#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace remote {
constexpr double KG_PER_LB=0.45359237;
inline double kilograms(int pounds){return pounds*KG_PER_LB;}
// VOLTRA display scale checked against three user-observed device readings:
// 61 protocol lb -> 27.5 kg; 82 -> 37.5 kg; 93 -> 42.5 kg.
// Use one reversible half-kg grid for display AND encoder targets.
// 2.2 lb/kg gives 1.1 protocol lb per half-kg step; integer arithmetic avoids
// floating-point boundary errors. This matches the device UI, not SI conversion.
inline int displayHalfKilograms(int pounds){return pounds<0?-((-pounds*20+11)/22):(pounds*20+11)/22;}
inline double displayKilograms(int pounds){return displayHalfKilograms(pounds)*0.5;}
// Twin screen adds the two device-rounded weights: 179 lb -> 81.5 kg each -> 163 kg.
// Do not convert doubled raw pounds (that would incorrectly show 162.5 kg).
inline double displayWeightKilograms(int pounds,bool twin){return displayKilograms(pounds)*(twin?2:1);}
inline int kgStep(int pounds,int direction,int minimum,int maximum){
  const int halfKg=displayHalfKilograms(pounds)+direction;
  const int next=halfKg<0?-((-halfKg*11+5)/10):(halfKg*11+5)/10;
  return std::max(minimum,std::min(maximum,next));
}

inline int damperFactor(int index){constexpr int factors[]={5,8,11,14,17,21,30,33,41,50};return index>=0&&index<10?factors[index]:-1;}
inline const char* damperRoman(int index){constexpr const char* labels[]={"I","II","III","IV","V","VI","VII","VIII","IX","X"};return index>=0&&index<10?labels[index]:"--";}
struct Editor {
 int confirmed=0,target=0;
 bool known=false,pending=false,sent=false,timedOut=false;
 uint32_t changedMs=0,sentMs=0;
 void reset(){*this=Editor{};}
 void observe(int value){
   confirmed=value;
   if(!known||!pending){target=value;known=true;}
   if(pending&&sent&&value==target){pending=false;sent=false;timedOut=false;}
 }
 void turn(int direction,bool allowed,bool percent,uint32_t now,int low,int high){
   if(!allowed||!known||sent)return;
   const int value=percent?std::max(low,std::min(high,target+direction*5)):kgStep(target,direction,low,high);
   if(value==target)return;
   target=value;pending=target!=confirmed;changedMs=now;timedOut=false;
 }
 bool due(uint32_t now)const{return pending&&!sent&&now-changedMs>=250;}
 void submitted(uint32_t now){sent=true;sentMs=now;}
 void cancel(){target=confirmed;pending=false;sent=false;}
 void tick(uint32_t now){if(sent&&now-sentMs>=5000){cancel();timedOut=true;}}
};
}
