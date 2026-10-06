#include "voice.h"
#include "recognition.h"
#include "audio_input.h"
#include "capture.h"
#include "replay.h"
#include <Arduino.h>
#include <ESP_I2S.h>
#include <ESP_SR.h>
#include <esp_partition.h>
#include <esp_mn_speech_commands.h>
#include <atomic>
#include <cmath>
#include <lvgl.h>
#include "../ui/theme.h"
#include "../ui/ui.h"
#include "../link/voltra_link.h"
extern "C" void sr_command_diagnostics(unsigned*,unsigned*,int*,int*);
extern "C" void sr_command_audio_debug(char*,size_t,char*,size_t,unsigned*,unsigned*,float*);
namespace voice {
namespace {
I2SClass microphone;
QueueHandle_t events=nullptr;
std::atomic<bool> ready{false};
std::atomic<unsigned> chunks{0},errors{0},peak{0},windowPeak{0},mean{0},dropped{0};
std::atomic<int> dc{0},modeError{0},lastCommand{-1};
std::atomic<unsigned> rawCommands{0},rawTimeouts{0},registered{0};
std::atomic<unsigned> rms{0},windowRms{0};
lv_obj_t *badge=nullptr,*notice=nullptr;
uint32_t noticeMs=0;
Recognition recognition{true,true};
std::atomic<uint32_t> commandSession{0};
std::atomic<int> commandMode{-1};
AudioInput audioInput;
std::atomic<unsigned> gainPercent{400};
// CMU pronunciations encoded with Espressif's MultiNet alphabet:
// load=L OW1 D, weight=W EY1 T, release=R IY0 L IY1 S.
// The linked/voiced Load variant below matches the saved user sample; it
// retained Release and rejected the tested confusable clip at two gains.
// Motor requests use the same state guards as touch, with session-bound events.
// A focused Release keyword outperformed its full phrase in replay.
// No detection threshold reduction.
// MultiNet7 recommends lowercase graphemes and explicit phonemes, unlike MN6.
static const sr_cmd_t commands[]={
 {1,"load weight","Lb WdD"},
 {2,"release","RmLmS"}
};
constexpr size_t commandCount=sizeof(commands)/sizeof(commands[0]);
void publish(EventKind kind){Event e{kind,millis(),commandSession.load(),commandMode.load()};if(xQueueSend(events,&e,0)!=pdTRUE)++dropped;}
esp_err_t fill(void*,void* out,size_t len,size_t* bytesRead,uint32_t){
 // A bounded audio read; no logging, BLE writes or LVGL calls on this task.
 *bytesRead=microphone.readBytes(static_cast<char*>(out),len);
 if(*bytesRead!=len){++errors;memset(static_cast<uint8_t*>(out)+*bytesRead,0,len-*bytesRead);}
 const int16_t* samples=static_cast<int16_t*>(out);unsigned maximum=0,total=0;int64_t sum=0,squares=0;
 for(size_t i=0;i<len/2;++i){const unsigned a=abs(int(samples[i]));maximum=std::max(maximum,a);total+=a;sum+=samples[i];squares+=int64_t(samples[i])*samples[i];}
 const size_t n=len/2;const int average=n?sum/n:0;const unsigned ac=n?unsigned(std::sqrt(std::max(0.0,double(squares)/n-double(average)*average))):0;
 dc.store(average);rms.store(ac);unsigned oldRms=windowRms.load();while(oldRms<ac&&!windowRms.compare_exchange_weak(oldRms,ac)){}
 unsigned previous=windowPeak.load();while(previous<maximum&&!windowPeak.compare_exchange_weak(previous,maximum)){}
 peak.store(maximum);mean.store(len?total/(len/2):0);++chunks;
 voice_capture::raw(static_cast<int16_t*>(out),len/2);
 audioInput.process(static_cast<int16_t*>(out),len/2);gainPercent.store(unsigned(audioInput.gain()*100));
 *bytesRead=len;return ESP_OK;
}
void onEvent(void*,sr_event_t event,int command,int){
 switch(event){
 case SR_EVENT_WAKEWORD:
 case SR_EVENT_WAKEWORD_CHANNEL:modeError.store(sr_set_mode(SR_MODE_COMMAND));break;
 case SR_EVENT_COMMAND:
  ++rawCommands;lastCommand.store(command);
  if(command==1)publish(EventKind::Load);else if(command==2)publish(EventKind::Release);
  modeError.store(sr_set_mode(SR_MODE_COMMAND));break;
 case SR_EVENT_TIMEOUT:++rawTimeouts;publish(EventKind::Timeout);modeError.store(sr_set_mode(SR_MODE_COMMAND));break;
 default:break;
 }
}
void startTask(void*){
#ifdef VOLTRA_VOICE_REPLAY
 voice_replay::run();
#endif
 // Preserve all existing partitions; models live in the previously unused flash.
 const auto* model=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"model");
 uint32_t count=0;
 if(!model||esp_partition_read(model,0,&count,sizeof(count))!=ESP_OK||count==0||count>16||ESP.getFreePsram()<4*1024*1024||ESP.getFreeHeap()<90000){publish(EventKind::Error);vTaskDelete(nullptr);return;}
 microphone.setPinsPdmRx(5,4);microphone.setTimeout(100);
 if(!microphone.begin(I2S_MODE_PDM_RX,16000,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_PDM_SLOT_LEFT)){publish(EventKind::Error);vTaskDelete(nullptr);return;}
 const auto result=sr_start(fill,nullptr,SR_CHANNELS_MONO,SR_MODE_COMMAND,"M",commands,commandCount,onEvent,nullptr);
 if(result==ESP_OK){
  for(size_t i=0;i<commandCount;++i)if(esp_mn_commands_get_from_index(i))++registered;
  ready.store(true);publish(EventKind::Ready);
 }else publish(EventKind::Error);
 vTaskDelete(nullptr);
}
void show(const char* text){lv_label_set_text(notice,text);lv_obj_remove_flag(notice,LV_OBJ_FLAG_HIDDEN);noticeMs=millis();}
}
void begin(){
 events=xQueueCreate(8,sizeof(Event));
 badge=lv_label_create(lv_layer_top());lv_label_set_text(badge,"");
 // No voice text in normal use. Only explicit microphone diagnostics reveal it.
 lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_style_text_font(badge,&font_inter_12,0);lv_obj_set_style_text_color(badge,lv_color_hex(0x969696),0);lv_obj_align(badge,LV_ALIGN_BOTTOM_MID,0,-9);

 lv_obj_set_ext_click_area(badge,12);
 lv_obj_add_event_cb(badge,[](lv_event_t*){
  if(ready.load()&&voice_capture::armed()&&voice_capture::start())show("Recording 8 seconds\nSay the test command");
 },LV_EVENT_CLICKED,nullptr);
 notice=lv_label_create(lv_layer_top());lv_obj_set_width(notice,246);lv_obj_set_style_text_align(notice,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_font(notice,&font_inter_16,0);lv_obj_set_style_text_color(notice,lv_color_white(),0);
 lv_obj_set_style_bg_color(notice,lv_color_hex(0x222222),0);lv_obj_set_style_bg_opa(notice,LV_OPA_COVER,0);lv_obj_set_style_pad_all(notice,12,0);lv_obj_set_style_radius(notice,12,0);
 lv_obj_align(notice,LV_ALIGN_TOP_MID,0,72);lv_obj_add_flag(notice,LV_OBJ_FLAG_HIDDEN);
 // A notice must not turn an obscured control into an accidental tap target.
 lv_obj_add_flag(notice,LV_OBJ_FLAG_CLICKABLE);
 if(!events||xTaskCreatePinnedToCore(startTask,"Voice init",8192,nullptr,1,nullptr,0)!=pdPASS){lv_label_set_text(badge,"Voice unavailable");return;}
}
void loop(){
 const auto state=vlink::voltraLink.state();
 commandMode.store(state.mode);commandSession.store(state.session);
 voice_capture::loop();
 static bool captureArmed=false;
 if(ready.load()&&voice_capture::armed()&&!captureArmed){
  captureArmed=true;lv_obj_remove_flag(badge,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(badge,LV_OBJ_FLAG_CLICKABLE);lv_label_set_text(badge,"Tap for 8s mic test");
 }
 static bool captureReported=false;
 if(voice_capture::complete()&&!captureReported){captureReported=true;show("Audio captured\nVoice test only");lv_label_set_text(badge,"Audio captured · test only");}
 if(!events)return;const uint32_t now=millis();Event e;
 while(xQueueReceive(events,&e,0)==pdTRUE){
  if(e.kind==EventKind::Ready){
   continue;
  }
  if(e.kind==EventKind::Error){lv_label_set_text(badge,"Voice unavailable");continue;}
  if(!recognition.accept(e,now))continue;
  if(e.kind==EventKind::Wake)show("Listening...\nVoice test only");
  if(e.kind==EventKind::Load||e.kind==EventKind::Release){
   // Recording diagnostics cannot actuate the trainer.
   if(!voice_capture::armed())ui::voiceAction(e);
  }
  // Continuous recognition silently restarts after each model timeout.
 }
 if(noticeMs&&!recognition.listening&&now-noticeMs>3500&&(!voice_capture::started()||voice_capture::complete())){lv_obj_add_flag(notice,LV_OBJ_FLAG_HIDDEN);noticeMs=0;}
}
void logStatus(){
#ifdef VOLTRA_VOICE_REPLAY
 voice_replay::log();
#endif
 if(Serial.availableForWrite()<800)return;
 char raw[160],match[96];unsigned processedPeak=0,speechFrames=0;float confidence=0;
 sr_command_audio_debug(raw,sizeof(raw),match,sizeof(match),&processedPeak,&speechFrames,&confidence);
 Serial.printf("[voice-decoder] peak=%u speechFrames=%u confidence=%.3f raw=%s match=%s\n",processedPeak,speechFrames,confidence,raw,match);

 unsigned frames=0,resets=0;int state=0,initErrors=0;sr_command_diagnostics(&frames,&resets,&state,&initErrors);
 Serial.printf("[voice] testOnly=0 directRelease=1 directLoad=1 frames=%u resets=%u mnState=%d initErrors=%d ready=%d registered=%u rawCmd=%u lastCmd=%d rawTimeout=%u modeError=%d chunks=%u errors=%u peak=%u mean=%u dc=%d rms=%u gainPct=%u wake=%u load=%u release=%u timeout=%u dropped=%u psram=%u\n",frames,resets,state,initErrors,int(ready.load()),registered.load(),rawCommands.load(),lastCommand.load(),rawTimeouts.load(),modeError.load(),chunks.load(),errors.load(),windowPeak.exchange(0),mean.load(),dc.load(),windowRms.exchange(0),gainPercent.load(),recognition.wakes,recognition.loads,recognition.releases,recognition.timeouts,dropped.load(),unsigned(ESP.getFreePsram()));
}
}
