#pragma once
#include "bridge_protocol.h"
#include "../app/setup_activity.h"

namespace tvbridge {
// A short display lease spans expected trainer/TV reconnects. It expires even
// if the app crashes; old heartbeats cannot extend it or replace a newer state.
struct SetupActivityLease {
 uint32_t epoch=0,deadline=0;
 bool seen=false;
 remote::SetupActivity activity=remote::SetupActivity::None;
 bool accept(const Command& c,uint32_t currentEpoch,uint32_t session,uint32_t now){
  const int32_t remaining=static_cast<int32_t>(c.deadline-now);
  if(c.type!=11||c.id||c.value||c.field>5||!currentEpoch||c.epoch!=currentEpoch||
     c.session!=session||remaining<=0||remaining>2000||
     (seen&&epoch==c.epoch&&static_cast<int32_t>(c.deadline-deadline)<=0))return false;
  epoch=c.epoch;deadline=c.deadline;seen=true;activity=static_cast<remote::SetupActivity>(c.field);return true;
 }
 remote::SetupActivity current(uint32_t now)const{
  return seen&&static_cast<int32_t>(deadline+6000-now)>0?activity:remote::SetupActivity::None;
 }
};
}
