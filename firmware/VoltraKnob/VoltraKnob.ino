#include <Arduino.h>
#include <lvgl.h>
#include "src/voice/voice.h"
#include "src/board/board.h"
#include "src/ui/ui.h"
#include "src/link/voltra_link.h"
void setup(){
 // Keep touch/rendering ahead of speech inference (priority 5); the loop
 // blocks between updates so audio still runs. BLE/audio feed stay on core 0.
 vTaskPrioritySet(nullptr,6);
 Serial.setTxBufferSize(2048);Serial.begin(115200);Serial.setTxTimeoutMs(0);delay(100);Serial.println("[boot] Guition Voltra Remote / kg / two trainers");
 if(!board::begin()){Serial.println("[error] Board initialization failed; BLE disabled");while(true)delay(1000);}
 vlink::voltraLink.begin();ui::begin();voice::begin();Serial.println("[ready] Display initialized; scanning for two Voltras");
}
void loop(){
 static uint32_t previousLoop=0,maxLoopGap=0,loops=0;
 static uint32_t windowGap=0,uiMaxUs=0,renderMaxUs=0,slowLoops=0;
 const uint32_t now=millis();
 if(previousLoop && now-previousLoop>maxLoopGap)maxLoopGap=now-previousLoop;
 if(previousLoop){const uint32_t gap=now-previousLoop;windowGap=std::max(windowGap,gap);if(gap>50)++slowLoops;}
 previousLoop=now;++loops;
 const uint32_t uiStart=micros();ui::loop();voice::loop();
 uiMaxUs=std::max(uiMaxUs,uint32_t(micros()-uiStart));
 const uint32_t renderStart=micros();lv_timer_handler();
 renderMaxUs=std::max(renderMaxUs,uint32_t(micros()-renderStart));
 static uint32_t lastLog=0;
 if(millis()-lastLog>=5000 && Serial.availableForWrite()>=700){
   lastLog=millis();Serial.printf("[loop] count=%lu maxGapMs=%lu windowGapMs=%lu slowLoops=%lu uiMaxUs=%lu renderMaxUs=%lu\n",(unsigned long)loops,(unsigned long)maxLoopGap,(unsigned long)windowGap,(unsigned long)slowLoops,(unsigned long)uiMaxUs,(unsigned long)renderMaxUs);windowGap=uiMaxUs=renderMaxUs=slowLoops=0;board::logInputs();voice::logStatus();Serial.printf("[ui] selecting=%d\n",ui::selectingTrainer());const auto s=vlink::voltraLink.state();
   Serial.printf("[twin] connection=%d savedRole=%d main=%d sub=%d total=%d\n",s.twinConnection,s.twinLastRole,s.twinMainForce,s.twinSubForce,s.twinTotalForce);
   Serial.printf("[status] link=%u selected=%d trainer1=%s trainer2=%s weightLb=%d chainsLb=%d inverseEnabled=%d eccLb=%d mode=%d fitness=%d screen=%d cable=%d offset=%d lengthUnit=%d cableActive=%d heap=%u\n",static_cast<unsigned>(s.link),s.selected,s.trainers[0].address,s.trainers[1].address,s.weightLbs,s.chainsLbs,s.inverseChainsEnabled,s.eccentricLbs,s.mode,s.fitnessMode,s.cableScreenId,s.cableCm,s.cableOffsetCm,s.lengthUnitRaw,s.cableActive,static_cast<unsigned>(ESP.getFreeHeap()));
 }
 delay(5);
}
