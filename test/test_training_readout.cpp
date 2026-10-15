#include "minitest.h"
#include "app/training_readout.h"
#include <cstring>
#include <initializer_list>

int main(){
  vlink::DeviceState state;
  state.link=vlink::LinkState::Ready;state.statusMs=1000;
  state.endpoint.valid=true;state.endpoint.status=0;state.endpointMs=1000;
  std::strcpy(state.endpoint.local,"02:00:00:00:00:01");
  std::strcpy(state.endpoint.peer,"00:00:00:00:00:00");
  state.activeWeightTenths=60;state.forceMs=1000;
  state.motionMs=1000;state.cableLengthMm=300;state.cableVelocityMmPerSecond=100;
  for(int mode:{1,2,4,7}){
    state.mode=mode;state.fitnessMode=4;state.loaded=false;
    auto reading=remote::trainingReadout(state,1100,true,false);
    CHECK(reading.unloaded);CHECK(!reading.liveWeight); // Keep configured value, not zero.
    state.fitnessMode=5;state.loaded=true;
    reading=remote::trainingReadout(state,1100,true,false);
    CHECK(!reading.unloaded);CHECK(reading.liveWeight);CHECK_EQ(reading.forceTenthsLb,60);
    CHECK(!remote::trainingReadout(state,1100,true,true).liveWeight); // Pending setting wins.
    CHECK(!remote::trainingReadout(state,1100,false,false).liveWeight); // Modifier stays editable.
    CHECK(!remote::trainingReadout(state,1500,true,false).liveWeight); // Return to configured value.
    state.cableVelocityMmPerSecond=0;
    CHECK(!remote::trainingReadout(state,1100,true,false).liveWeight); // Stationary force is not activity.
    state.cableVelocityMmPerSecond=-100;
    CHECK(remote::trainingReadout(state,1100,true,false).liveWeight); // Eccentric movement also counts.
    state.cableVelocityMmPerSecond=100;
    state.armed=true;CHECK(!remote::trainingReadout(state,1100,true,false).liveWeight);state.armed=false;
  }
  state.mode=1;state.loaded=false;state.fitnessMode=999;
  CHECK(!remote::trainingReadout(state,1100,true,false).unloaded);
  state.fitnessMode=4;state.statusMs=0;
  CHECK(!remote::trainingReadout(state,1100,true,false).unloaded);
  state.statusMs=1000;state.mode=3;
  CHECK(!remote::trainingReadout(state,1100,true,false).unloaded);
  CHECK_EQ(remote::telemetryKilograms(0),0.0);
  CHECK(std::abs(remote::telemetryKilograms(60)-2.72155422)<0.000001);
  CHECK(std::abs(remote::telemetryKilograms(120)-5.44310844)<0.000001);
  std::printf("%d readout checks, %d failures\n",g_checks,g_failures);
  return g_failures?1:0;
}
