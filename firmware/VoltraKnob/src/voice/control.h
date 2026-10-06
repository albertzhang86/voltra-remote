#pragma once
#include "recognition.h"
#include "../link/voltra_link.h"
namespace voice {
// Load is explicit, never a toggle. Release remains available on any page,
// including during a pending load or when status notifications are stale.
inline bool canAct(const Event& e,const vlink::DeviceState& s,uint32_t now,
                   bool controlPage,bool pending,bool loadPending,bool releaseHold){
 if(now-e.ms>750||e.session!=s.session||s.selected<0||s.link!=vlink::LinkState::Ready)return false;
 if(e.kind==EventKind::Release)return true;
 if(e.kind!=EventKind::Load||e.mode!=s.mode||!controlPage||pending||loadPending||releaseHold)return false;
 if(s.controlBusyUntil&&static_cast<int32_t>(s.controlBusyUntil-now)>0)return false;
 return vlink::canLoad(s,now);
}
}
