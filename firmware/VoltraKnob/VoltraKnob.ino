#include <Arduino.h>
#include <lvgl.h>
#include "src/voice/voice.h"
#include "src/board/board.h"
#include "src/ui/ui.h"
#include "src/link/voltra_link.h"
#include "src/tv/tv_bridge.h"
void setup(){
 // Keep touch/rendering ahead of speech inference (priority 5); the loop
 // blocks between updates so audio still runs. BLE/audio feed stay on core 0.
 vTaskPrioritySet(nullptr,6);
 Serial.setTxBufferSize(2048);Serial.begin(115200);Serial.setTxTimeoutMs(0);delay(100);Serial.println("[boot] Guition Voltra Remote / kg / two trainers");
 if(!board::begin()){Serial.println("[error] Board initialization failed; BLE disabled");while(true)delay(1000);}
 // Register the TV peripheral before the central worker starts scanning.
 // NimBLE cannot reset its GATT database while discovery is active.
 vlink::voltraLink.begin();ui::begin();tvbridge::begin();vlink::voltraLink.start();voice::begin();Serial.println("[ready] Display initialized; scanning for two Voltras");
}
void loop(){
 static uint32_t previousLoop=0,maxLoopGap=0,loops=0;
 static uint32_t windowGap=0,uiMaxUs=0,renderMaxUs=0,slowLoops=0;
 const uint32_t now=millis();
 if(previousLoop && now-previousLoop>maxLoopGap)maxLoopGap=now-previousLoop;
 if(previousLoop){const uint32_t gap=now-previousLoop;windowGap=std::max(windowGap,gap);if(gap>50)++slowLoops;}
 previousLoop=now;++loops;
 const uint32_t uiStart=micros();ui::loop();voice::loop();tvbridge::loop();
 uiMaxUs=std::max(uiMaxUs,uint32_t(micros()-uiStart));
 const uint32_t renderStart=micros();lv_timer_handler();
 renderMaxUs=std::max(renderMaxUs,uint32_t(micros()-renderStart));
 // Bounded, read-only diagnostics for comparing the main readout with the TV.
 // Keep USB logging out of the BLE notification callback.
 static uint32_t lastWeightLog=0,lastMotionSample=0,lastPairSample=0;
 if(now-lastWeightLog>=250&&Serial.availableForWrite()>=180){
   lastWeightLog=now;const auto s=vlink::voltraLink.state();
   if(s.motionMs&&s.motionMs!=lastMotionSample){
     lastMotionSample=s.motionMs;const auto moving=vlink::movingWeightReading(s,now);
     Serial.printf("[moving-weight] mode=%d fitness=%d forceTenthsLb=%d cableMm=%d velocityMmS=%d sampleAgeMs=%lu displayedTenthsLb=%d\n",s.mode,s.fitnessMode,s.activeWeightTenths,s.cableLengthMm,s.cableVelocityMmPerSecond,static_cast<unsigned long>(now-s.motionMs),moving.tenthsLb);
   }
   if(s.pairMotionMs&&s.pairMotionMs!=lastPairSample){
     lastPairSample=s.pairMotionMs;const auto moving=vlink::movingWeightReading(s,now);
     Serial.printf("[twin-weight] mode=%d fitness=%d combinedTenthsLb=%d moving=%u ageMs=%lu displayedTenthsLb=%d reps=%d\n",s.mode,s.fitnessMode,s.pairForceTenths,s.pairMoving,static_cast<unsigned long>(now-s.pairMotionMs),moving.tenthsLb,vlink::repetitionReading(s,now));
   }
 }
 static uint32_t lastLog=0;
 if(millis()-lastLog>=5000 && Serial.availableForWrite()>=700){
   lastLog=millis();Serial.printf("[loop] count=%lu maxGapMs=%lu windowGapMs=%lu slowLoops=%lu uiMaxUs=%lu renderMaxUs=%lu\n",(unsigned long)loops,(unsigned long)maxLoopGap,(unsigned long)windowGap,(unsigned long)slowLoops,(unsigned long)uiMaxUs,(unsigned long)renderMaxUs);windowGap=uiMaxUs=renderMaxUs=slowLoops=0;board::logInputs();voice::logStatus();Serial.printf("[ui] selecting=%d\n",ui::selectingTrainer());const auto s=vlink::voltraLink.state();
   Serial.printf("[unload] touch=%lu voice=%lu other=%lu lastRequestMs=%lu requestSession=%lu stopWrites=%lu lastStopMs=%lu\n",(unsigned long)s.unloadTouchRequests,(unsigned long)s.unloadVoiceRequests,(unsigned long)s.unloadOtherRequests,(unsigned long)s.lastUnloadRequestMs,(unsigned long)s.lastUnloadRequestSession,(unsigned long)s.stopFramesSent,(unsigned long)s.lastStopFrameMs);
   Serial.printf("[twin] connection=%d savedRole=%d main=%d sub=%d total=%d\n",s.twinConnection,s.twinLastRole,s.twinMainForce,s.twinSubForce,s.twinTotalForce);
   Serial.printf("[telemetry] unit=%d forceTenthsLb=%d forceAgeMs=%ld repAgeMs=%ld\n",s.weightUnitRaw,s.activeWeightTenths,s.forceMs?static_cast<long>(millis()-s.forceMs):-1L,s.repMs?static_cast<long>(millis()-s.repMs):-1L);
   Serial.printf("[target-weight] tenthsLb=%d ageMs=%ld activeTenthsLb=%d source=%u\n",s.targetForceTenths,s.targetForceMs?static_cast<long>(millis()-s.targetForceMs):-1L,vlink::activeWeightReading(s,millis()).tenthsLb,static_cast<unsigned>(vlink::activeWeightReading(s,millis()).source));
   Serial.printf("[runtime-weight] lbs=%d ageMs=%ld\n",s.runtimeWeightLbs,s.runtimeWeightMs?static_cast<long>(millis()-s.runtimeWeightMs):-1L);
   Serial.printf("[status] link=%u selected=%d trainer1=%s trainer2=%s weightLb=%d chainsLb=%d inverseEnabled=%d eccLb=%d mode=%d fitness=%d screen=%d cable=%d offset=%d lengthUnit=%d cableActive=%d heap=%u\n",static_cast<unsigned>(s.link),s.selected,s.trainers[0].address,s.trainers[1].address,s.weightLbs,s.chainsLbs,s.inverseChainsEnabled,s.eccentricLbs,s.mode,s.fitnessMode,s.cableScreenId,s.cableCm,s.cableOffsetCm,s.lengthUnitRaw,s.cableActive,static_cast<unsigned>(ESP.getFreeHeap()));
 }
 delay(5);
}
