#pragma once
#include <cstdint>
namespace voice {
constexpr uint32_t LISTEN_WINDOW_MS=10000;
enum class EventKind:uint8_t {Ready,Wake,Load,Release,Timeout,Error};
struct Event {EventKind kind;uint32_t ms;uint32_t session=0;int mode=-1;};
// Recognition-only acceptance gate. Deliberately has no trainer/motor dependency.
struct Recognition {
 bool directRelease=false;
 bool directLoad=false;
 bool listening=false;
 uint32_t wakeMs=0,lastCommandMs=0,lastLoadMs=0,lastReleaseMs=0;
 unsigned loads=0,releases=0,wakes=0,timeouts=0;
 bool accept(const Event& e,uint32_t now){
  if(now-e.ms>750)return false;
  if(directRelease||directLoad){
   if(e.kind==EventKind::Timeout){++timeouts;return true;}
   if(e.kind==EventKind::Load&&directLoad){
    if(loads&&now-lastLoadMs<500)return false;
    lastCommandMs=lastLoadMs=now;++loads;return true;
   }
   if(e.kind==EventKind::Release&&directRelease){
    if(releases&&now-lastReleaseMs<500)return false;
    lastCommandMs=lastReleaseMs=now;++releases;return true;
   }
   return false;
  }
  if(e.kind==EventKind::Wake){
   if(listening||(lastCommandMs&&now-lastCommandMs<1500))return false;
   listening=true;wakeMs=e.ms;++wakes;return true;
  }
  if(e.kind==EventKind::Timeout){listening=false;++timeouts;return true;}
  if(e.kind!=EventKind::Load&&e.kind!=EventKind::Release)return false;
  const bool allowed=listening&&e.ms-wakeMs<=LISTEN_WINDOW_MS;
  listening=false;if(!allowed)return false;
  lastCommandMs=now;if(e.kind==EventKind::Load)++loads;else ++releases;return true;
 }
 bool expire(uint32_t now){if(listening&&now-wakeMs>LISTEN_WINDOW_MS){listening=false;++timeouts;return true;}return false;}
};
}
