#include "replay.h"
#ifdef VOLTRA_VOICE_REPLAY
// Diagnostic-only: generated private fixture must never be packaged in a release.
#include "private_replay_fixture.h"
#include <Arduino.h>
#include <atomic>
#include <algorithm>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <model_path.h>
extern "C" {
#include <flite_g2p.h>
}
namespace voice_replay {
namespace {
struct Trial {const char* name;int grammar;float threshold,gain;bool clean;int clip=0;};
const Trial trials[]={
 {"voiced-final-user",5,-1,1,true,0},
 {"voiced-final-negative-full",5,-1,1,true,2},
 {"voiced-final-negative-low",5,-1,0.2f,true,2},
 {"voiced-final-release",5,-1,1,true,1},
 {"linked-voiced-user",7,-1,1,true,0},
 {"linked-voiced-negative-full",7,-1,1,true,2},
 {"linked-voiced-negative-low",7,-1,0.2f,true,2},
 {"linked-voiced-release",7,-1,1,true,1}
};
constexpr unsigned count=sizeof(trials)/sizeof(trials[0]);
char reports[count+1][900];std::atomic<unsigned> finished{0};
void publish(unsigned index,const char* text){snprintf(reports[index],sizeof(reports[index]),"%s",text);finished.store(index+1,std::memory_order_release);}
}
void run(){
 auto* models=esp_srmodel_init("model");
 char* name=models?esp_srmodel_filter(models,ESP_MN_PREFIX,ESP_MN_ENGLISH):nullptr;
 auto* mn=name?esp_mn_handle_from_name(name):nullptr;
 if(!mn){publish(0,"[replay] error=model-not-found");return;}
 for(unsigned t=0;t<count;++t){
  const Trial& trial=trials[t];char report[900];
  auto* model=mn->create(name,8000);
  if(!model){publish(t,"[replay] error=model-allocation");break;}
  esp_mn_commands_alloc(mn,model);
  const char* variants[]={"LbD WdT","LbD Wd","Lb WdT","LbDWdT","LbD","LbD WdD","Lb Wd","Lb WdD"};
  const char* registeredPhonemes=variants[trial.grammar];
  int addResult=esp_mn_commands_phoneme_add(1,trial.grammar==4?"load":"load weight",registeredPhonemes);
  addResult|=esp_mn_commands_phoneme_add(2,"release","RmLmS");
  auto* errors=esp_mn_commands_update();int errorCount=errors?errors->num:0;
  if(trial.threshold>=0)mn->set_det_threshold(model,trial.threshold);
  int chunk=mn->get_samp_chunksize(model),rate=mn->get_samp_rate(model);
  auto* frame=static_cast<int16_t*>(malloc(chunk*sizeof(int16_t)));
  unsigned detections=0,timeouts=0;char hits[360]={},raw[160]={};size_t used=0;
  if(frame&&!errorCount&&addResult==0){
   mn->clean(model);
   for(size_t offset=0;offset<replay_sample_count;offset+=chunk){
    for(int i=0;i<chunk;++i){float v=offset+i<replay_sample_count?(trial.clip==2?replay_confusable[offset+i]:trial.clip==1?replay_negative[offset+i]:replay_samples[offset+i])*trial.gain:0;frame[i]=int16_t(std::max(-32768.f,std::min(32767.f,v)));}
    auto state=mn->detect(model,frame);
    auto* diagnostic=mn->get_results(model);
    if(diagnostic&&diagnostic->raw_string[0])snprintf(raw,sizeof(raw),"%s",diagnostic->raw_string);
    if(state==ESP_MN_STATE_DETECTED){
     auto* result=mn->get_results(model);++detections;
     if(result&&result->num&&used<sizeof(hits)-60){int n=snprintf(hits+used,sizeof(hits)-used," %ums:%.3f:%d",unsigned((offset+chunk)*1000/rate),result->prob[0],result->command_id[0]);if(n>0)used+=n;}
     if(trial.clean)mn->clean(model);
    }else if(state==ESP_MN_STATE_TIMEOUT){++timeouts;mn->clean(model);}
    vTaskDelay(1);
   }
  }
  snprintf(report,sizeof(report),"[replay] trial=%s model=%s rate=%d chunk=%d add=%d errors=%d phonemes=%s hits=%u timeouts=%u%s raw=%s",trial.name,name,rate,chunk,addResult,errorCount,registeredPhonemes,detections,timeouts,hits,raw);
  free(frame);esp_mn_commands_free();mn->destroy(model);publish(t,report);vTaskDelay(20);
 }
 publish(count,"[replay] complete; returning to live recognition-only mode");
 esp_srmodel_deinit(models);
}
void log(){
 static unsigned next=0;
 unsigned available=finished.load(std::memory_order_acquire);
 if(next<available&&Serial.availableForWrite()>=1000){Serial.println(reports[next++]);}
 // Repeat after the whole suite so a reader connecting after boot can retrieve it.
 if(available==count+1&&next==available)next=0;
}
}
#endif
