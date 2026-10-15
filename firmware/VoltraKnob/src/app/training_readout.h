#pragma once
#include "remote_model.h"
#include "../link/voltra_link.h"

namespace remote {
// Presentation only: configured values remain visible at rest. Fresh force while
// the cable moves may replace the primary readout, never an edit or modifier page.
struct TrainingReadout {
  bool liveWeight=false;
  bool unloaded=false;
  int forceTenthsLb=-1;
};
inline TrainingReadout trainingReadout(const vlink::DeviceState& state,uint32_t now,
                                      bool primaryPage,bool editing){
  TrainingReadout result;
  if(!vlink::fresh(state,now)||!voltra::supportedMode(state.mode))return result;
  result.unloaded=primaryPage&&state.fitnessMode==4&&!state.loaded&&!state.armed;
  if(!primaryPage||editing)return result;
  const auto active=vlink::movingWeightReading(state,now);
  if(active.source==vlink::ActiveWeightSource::CableForce){
    result.liveWeight=true;result.forceTenthsLb=active.tenthsLb;
  }
  return result;
}
// Measured force has finer precision than the trainer's half-kg setting grid.
inline double telemetryKilograms(int tenthsLb){return tenthsLb*KG_PER_LB/10.0;}
}
