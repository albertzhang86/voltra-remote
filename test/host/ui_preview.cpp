#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <initializer_list>
#include <lvgl.h>
#include "board/board.h"
#include "link/voltra_link.h"
#include "ui/ui.h"
#include "ui/theme.h"
static uint32_t now=100;
uint32_t millis(){return now;}
static vlink::DeviceState device;
static int loadCalls=0,unloadCalls=0;
static int twinCalls=0;
static bool rejectTwinRequest=false;
static int flushCalls=0;
static uint16_t framebuffer[360*360];
static bool touching=false;
static int pointerX=180,pointerY=45;
static uint32_t ringColor=0;
static int isoCalls=0,isoRequestedIndex=-1,isoRequestedValue=-1;
static int rotation=0,settingCalls=0,bandCalls=0,bandRequested=-1,damperCalls=0,damperRequested=-1;
namespace tvbridge {
bool open=false,savedTv=false,tvConnected=false;
int forgetCalls=0,findCalls=0;
void openPairing(){open=true;} void disconnect(){open=false;}
bool connected(){return tvConnected;} bool pairing(){return open;} int passkey(){return open?123456:-1;}
bool trusted(){return savedTv;} bool findingTrainers(){return false;} void findTrainers(){if(!tvConnected&&vlink::canSwitch(device,now))++findCalls;}
bool forgetTvs(){++forgetCalls;savedTv=false;tvConnected=false;open=false;return true;}
}
static bool usbConnected=false;
namespace board {bool usbPowerConnected(){return usbConnected;} int batteryPercent(){return 76;} void traceDial(const char*,int,int,int,int,int,bool,bool,uint32_t){} void setRingColor(uint32_t rgb){ringColor=rgb;} bool pollRotate(RotateEvent& ev){if(!rotation)return false;ev={static_cast<int8_t>(rotation),now};rotation=0;return true;}}
namespace vlink {
VoltraLink voltraLink;
DeviceState VoltraLink::state(){return device;}
void VoltraLink::setDiscoveryVisible(bool){}
bool VoltraLink::forgetTrainer(int slot,const char* expected){if(!canForgetTrainer(device,slot,now)||strcmp(expected,device.trainers[slot].address))return false;device.trainers[slot]={};if(device.selected==slot){device.selected=-1;++device.session;device.link=LinkState::Scanning;}return true;}
void VoltraLink::rediscoverTrainers(){}
bool VoltraLink::selectNearbyTrainer(const char* address){
 if(!canSwitch(device,now))return false;
 const Trainer* candidate=nullptr;for(const auto& trainer:device.nearby)if(!strcmp(trainer.address,address)&&nearbyAdvertisement(trainer,now))candidate=&trainer;
 if(!candidate)return false;
 for(int i=0;i<2;++i)if(!device.trainers[i].address[0]){device.trainers[i]=*candidate;device.selected=i;++device.session;return true;}
 strcpy(device.message,"Saved slots full: Manage devices");return false;
}
bool VoltraLink::selectTrainer(int n){if(pairedFollower(device,n,now)||!canSwitch(device,now))return false;device.selected=n;++device.session;return true;}
bool VoltraLink::requestParameter(Parameter p){
 if(!fresh(device,now))return false;
 if(p==Parameter::Chains||p==Parameter::InverseChains){if(!canEdit(device,now))return false;device.inverseChainsEnabled=p==Parameter::InverseChains?1:0;}
 return true;
}
bool VoltraLink::requestSetting(Parameter p,int value){if(!canEditParameter(device,now,p))return false;++settingCalls;if(p==Parameter::Weight)device.weightLbs=value;else if(p==Parameter::Chains){device.chainsLbs=value;device.inverseChainsEnabled=0;}else if(p==Parameter::InverseChains){device.chainsLbs=value;device.inverseChainsEnabled=1;}else device.eccentricLbs=value;return true;}
bool VoltraLink::requestBandMaximum(int pounds){if(!canEditBand(device,now)||pounds<15||pounds>200)return false;++bandCalls;bandRequested=pounds;return true;}
bool VoltraLink::requestDamperLevel(int index){if(!canEditDamper(device,now)||index<0||index>9)return false;++damperCalls;damperRequested=index;return true;}
bool VoltraLink::requestIsokinetic(int index,int value){if(!canEditIsokinetic(device,now,index)||!voltra::validIsokineticSetting(index,value))return false;++isoCalls;isoRequestedIndex=index;isoRequestedValue=value;return true;}
bool VoltraLink::requestCable(bool open){if(open&&(!canSwitch(device,now)||device.loaded||device.armed||device.mode<=0))return false;device.cableActive=open;return true;}
bool VoltraLink::requestLoad(uint32_t expectedSession){if(expectedSession!=UINT32_MAX&&expectedSession!=device.session)return false;++loadCalls;return canLoad(device,now);}
bool VoltraLink::requestUnload(uint32_t expectedSession,UnloadSource){if(expectedSession!=UINT32_MAX&&expectedSession!=device.session)return false;++unloadCalls;return true;}
bool VoltraLink::requestTwin(bool create,uint32_t,uint32_t,uint32_t){
 ++twinCalls;if(rejectTwinRequest)return false;
 if(!canChangeTwin(device,now,create))return false;
 device.twinConnection=create?2:0;device.endpoint.status=create?0x12:0;device.twinMs=device.endpointMs=now;
 strcpy(device.endpoint.peer,create?device.trainers[1-device.selected].address:"00:00:00:00:00:00");return true;
}
bool VoltraLink::requestWeightMode(){return requestMode(1);}
bool VoltraLink::requestMode(int mode){if(!canSwitch(device,now)||!fresh(device,now))return false;device.mode=mode;device.fitnessMode=mode?4:0;return true;}
}
bool simulateAdvertising=true;
void snapshot(const char* path){
 device.endpoint.valid=true;device.endpoint.status=device.twinConnection==2?0x12:0;
 strcpy(device.endpoint.local,device.trainers[device.selected>=0?device.selected:0].address);
 strcpy(device.endpoint.peer,device.twinConnection==2?device.trainers[1-(device.selected>=0?device.selected:0)].address:"00:00:00:00:00:00");
 device.endpointMs=device.twinConnection==2?device.twinMs:now;
 if(simulateAdvertising)for(auto& t:device.trainers){t.seen=true;t.lastSeenMs=now;}
 for(int i=0;i<10;++i){now+=20;device.statusMs=now;if(device.twinRoleMs&&now-device.twinRoleMs<3500)device.twinRoleMs=now;ui::loop();lv_timer_handler();}
 FILE* f=fopen(path,"wb");fprintf(f,"P6\n360 360\n255\n");
 for(int y=0;y<360;++y)for(int x=0;x<360;++x){uint16_t p=framebuffer[y*360+x];unsigned char rgb[3]={static_cast<unsigned char>((p>>11)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};if((x-180)*(x-180)+(y-180)*(y-180)>180*180)memset(rgb,0,3);fwrite(rgb,1,3,f);}fclose(f);
}
int main(){
 lv_init();lv_tick_set_cb(millis);auto* d=lv_display_create(360,360);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
 // uint16_t guarantees only 2-byte alignment; LVGL requires 4 even on the host.
 alignas(LV_DRAW_BUF_ALIGN) static uint16_t buf[360*30];lv_display_set_buffers(d,buf,nullptr,sizeof(buf),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t* d,const lv_area_t* a,uint8_t* px){++flushCalls;auto* p=reinterpret_cast<uint16_t*>(px);for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)framebuffer[y*360+x]=*p++;lv_display_flush_ready(d);});
 auto* pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(pointer,[](lv_indev_t* input,lv_indev_data_t* data){data->point.x=pointerX;data->point.y=pointerY;data->state=touching?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;ui::filterTouch(input,data);});
 strcpy(device.trainers[0].address,"10:20:30:aa:bb:01");strcpy(device.trainers[1].address,"10:20:30:aa:bb:02");
 strcpy(device.trainers[0].name,"VOLTRA - Left");strcpy(device.trainers[1].name,"VOLTRA - Right");
 for(auto& trainer:device.trainers){trainer.seen=true;trainer.lastSeenMs=now;}
 device.link=vlink::LinkState::Ready;device.mode=1;device.fitnessMode=4;device.weightLbs=61;device.chainsLbs=10;device.inverseChainsEnabled=0;device.cableCm=30;device.cableOffsetCm=0;device.cableMs=now;device.lengthUnitRaw=0;device.eccentricLbs=15;device.battery=86;device.reps=8;device.statusMs=100;device.settingsMs=100;device.selected=0;device.session=1;
 ui::begin();
 auto* batteryWidget=lv_obj_get_child(lv_layer_top(),-1);
 assert(!lv_obj_has_flag(batteryWidget,LV_OBJ_FLAG_CLICKABLE));
 lv_obj_update_layout(batteryWidget);assert(lv_obj_get_width(batteryWidget)==66);
 now+=900;snapshot("build/preview-splash.ppm");now+=1200;snapshot("build/preview-selector.ppm");
 assert(tvbridge::findCalls==0); // Startup leaves trusted-TV reconnection available.
 auto swipePointer=[&](bool left){
  const int before=unloadCalls;
  pointerX=left?210:150;pointerY=160;touching=true;snapshot("build/swipe-start.ppm");
  for(int i=0;i<4;++i){pointerX+=left?-30:30;now+=35;device.statusMs=now;ui::loop();lv_timer_handler();}
  touching=false;snapshot("build/swipe-end.ppm");
  assert(unloadCalls==before); // Swiping over a number must not unload.
 };
 auto* trainerSelector=lv_obj_get_child(lv_screen_active(),-1);
 auto* toolsPanel=lv_obj_get_child(trainerSelector,5);
 auto* tvPanel=lv_obj_get_child(lv_layer_top(),0);
 // A real touch near the new button's lower-right corner must open the page,
 // including while loaded. Viewing TV settings must not load/unload the trainer.
 device.loaded=true;device.fitnessMode=5;
 swipePointer(false);snapshot("build/preview-connection-tools.ppm");
 assert(!lv_obj_has_flag(toolsPanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=false;device.fitnessMode=4;snapshot("build/preview-connections-ready.ppm");
 device.loaded=true;device.fitnessMode=5;
 pointerX=280;pointerY=160;touching=true;snapshot("build/preview-tv-link-touch.ppm");
 touching=false;snapshot("build/preview-tv-loaded.ppm");
 assert(!lv_obj_has_flag(tvPanel,LV_OBJ_FLAG_HIDDEN));
 assert(lv_obj_has_state(lv_obj_get_child(tvPanel,3),LV_STATE_DISABLED));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(tvPanel,1)),"Unload VOLTRA to pair")==0);
 assert(loadCalls==0&&unloadCalls==0);
 device.loaded=false;device.fitnessMode=4;snapshot("build/preview-tv-ready.ppm");
 assert(!lv_obj_has_state(lv_obj_get_child(tvPanel,3),LV_STATE_DISABLED));
 lv_obj_send_event(lv_obj_get_child(tvPanel,3),LV_EVENT_CLICKED,nullptr);
 rotation=1;snapshot("build/preview-tv-pairing.ppm");
 assert(strcmp(lv_label_get_text(lv_obj_get_child(tvPanel,2)),"123456")==0);
 assert(settingCalls==0&&loadCalls==0);
 lv_obj_send_event(lv_obj_get_child(tvPanel,4),LV_EVENT_CLICKED,nullptr);
 swipePointer(true);assert(lv_obj_has_flag(toolsPanel,LV_OBJ_FLAG_HIDDEN));
 auto* selector=lv_obj_get_child(lv_screen_active(),-1);lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(selector,2),0),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-weight.ppm");
 // Twin: observed controller state, combined rounded weight, icon, disabled follower.
 device.weightLbs=179;device.twinConnection=2;device.twinMs=now;device.twinLastRole=1;device.twinRoleMs=now;
 snapshot("build/preview-twin-weight.ppm");
 auto* weightNumber=lv_obj_get_child(lv_screen_active(),4);

 assert(strcmp(lv_label_get_text(weightNumber),"163.0")==0);
 assert(!lv_obj_has_flag(lv_obj_get_child(lv_screen_active(),7),LV_OBJ_FLAG_HIDDEN));
 // Every modifier tile and selected numeral use combined Twin weights.
 auto* chainTab=lv_obj_get_child(lv_screen_active(),9);
 auto* inverseTab=lv_obj_get_child(lv_screen_active(),10);
 auto* eccentricTab=lv_obj_get_child(lv_screen_active(),11);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(chainTab,0)),"9.0")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(inverseTab,0)),"0.0")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(eccentricTab,0)),"14.0")==0);
 const int twinWrites=settingCalls;
 lv_obj_send_event(eccentricTab,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-eccentric.ppm");
 assert(strcmp(lv_label_get_text(weightNumber),"14.0")==0);
 lv_obj_send_event(inverseTab,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-inverse.ppm");
 assert(strcmp(lv_label_get_text(weightNumber),"9.0")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(chainTab,0)),"0.0")==0);
 lv_obj_send_event(chainTab,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-chains.ppm");
 assert(strcmp(lv_label_get_text(weightNumber),"9.0")==0);
 assert(settingCalls==twinWrites); // Display scaling must not write doubled loads.
 rotation=1;snapshot("build/preview-twin-chains-dial.ppm");snapshot("build/preview-twin-chains-sent.ppm");
 assert(device.chainsLbs==11); // A displayed 1kg combined increment writes the per-unit target.
 assert(strcmp(lv_label_get_text(weightNumber),"10.0")==0);
 device.chainsLbs=10;snapshot("build/preview-twin-chains-restored.ppm");

 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),8),LV_EVENT_CLICKED,nullptr);
 device.twinMs=now;device.twinRoleMs=now;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);
 snapshot("build/preview-twin-selector.ppm");
 auto* follower=lv_obj_get_child(lv_obj_get_child(selector,2),1);
 assert(lv_obj_has_state(follower,LV_STATE_DISABLED));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(follower,1)),"Paired")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(selector,2),0),1)),"Control")==0);
 lv_obj_send_event(follower,LV_EVENT_CLICKED,nullptr);assert(device.selected==0);
 // Explicit override releases the session-level UI lock.
 lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(selector,2),0),LV_EVENT_CLICKED,nullptr);
 device.twinConnection=0;device.weightLbs=61;snapshot("build/preview-twin-ended.ppm");
 assert(!lv_obj_has_state(follower,LV_STATE_DISABLED));
 assert(lv_obj_has_flag(lv_obj_get_child(lv_screen_active(),7),LV_OBJ_FLAG_HIDDEN));
 assert(strcmp(lv_label_get_text(weightNumber),"27.5")==0);
 // Primary readouts keep configured settings at rest, show fresh cable force
 // during movement, and return to mode-specific settings when movement stops.
 const auto readoutBaseline=device;
 auto* readoutUnit=lv_obj_get_child(lv_screen_active(),6);
 auto* unloadedIndicator=lv_obj_get_child(lv_screen_active(),5);
 for(int mode:{1,2,4,7}){
   device.mode=mode;device.weightLbs=8;device.weightUnitRaw=1;device.settingsMs=now;
   device.modeValues[0]=22;device.modeValues[1]=100;device.modeValues[5]=4;device.modeValues[7]=500;
   device.modeValues[8]=0;device.modeValues[9]=0;device.modeValues[10]=5;device.modeValues[11]=5;
   for(auto& stamp:device.modeValueMs)stamp=now;
   device.loaded=false;device.fitnessMode=4;device.forceMs=device.motionMs=0;
   char file[100];snprintf(file,sizeof(file),"build/preview-readout-%d-unloaded.ppm",mode);snapshot(file);
   const char* configured=mode==1?"3.5":mode==2?"10.0":mode==4?"17":"0.5";
   const char* configuredUnit=mode==1?"kg":mode==2?"Max kg":mode==4?"Factor":"m/s";
   assert(!strcmp(lv_label_get_text(weightNumber),configured));
   assert(!strcmp(lv_label_get_text(readoutUnit),configuredUnit));
   assert(!lv_obj_has_flag(unloadedIndicator,LV_OBJ_FLAG_HIDDEN));
   if(mode!=2)assert(lv_obj_get_style_text_opa(weightNumber,0)==LV_OPA_50);
   device.loaded=true;device.fitnessMode=5;
   snprintf(file,sizeof(file),"build/preview-readout-%d-rest.ppm",mode);snapshot(file);
   assert(!strcmp(lv_label_get_text(weightNumber),configured));
   assert(lv_obj_has_flag(unloadedIndicator,LV_OBJ_FLAG_HIDDEN));
   device.activeWeightTenths=120;device.forceMs=device.motionMs=now;
   device.cableLengthMm=300;device.cableVelocityMmPerSecond=100;
   snprintf(file,sizeof(file),"build/preview-readout-%d-live.ppm",mode);snapshot(file);
   assert(!strcmp(lv_label_get_text(weightNumber),"5.4"));
   assert(!strcmp(lv_label_get_text(readoutUnit),"kg"));
   const int liveFlushes=flushCalls;snapshot("build/readout-live-steady.ppm");assert(flushCalls==liveFlushes);
   device.cableVelocityMmPerSecond=0;device.forceMs=device.motionMs=now;
   snprintf(file,sizeof(file),"build/preview-readout-%d-stationary.ppm",mode);snapshot(file);
   assert(!strcmp(lv_label_get_text(weightNumber),configured));
   assert(!strcmp(lv_label_get_text(readoutUnit),configuredUnit));
   device.cableVelocityMmPerSecond=-100;device.forceMs=device.motionMs=now;
   snapshot("build/readout-eccentric.ppm");assert(!strcmp(lv_label_get_text(weightNumber),"5.4"));
   now+=2100;for(auto& stamp:device.modeValueMs)stamp=now;
   snprintf(file,sizeof(file),"build/preview-readout-%d-restored.ppm",mode);snapshot(file);
   assert(!strcmp(lv_label_get_text(weightNumber),configured));
   assert(!strcmp(lv_label_get_text(readoutUnit),configuredUnit));
 }
 device=readoutBaseline;++device.session;device.settingsMs=now;
 snapshot("build/readout-baseline-restored.ppm");
 // A single tap loads once; a long press alone does not load until release/click.
 auto* action=lv_obj_get_child(lv_screen_active(),4);
 device.settingsMs=now;device.statusMs=now;
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);
 now+=100;lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);
 lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==1);
 lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==1);
 // Next press unloads even before acknowledgement, without reloading on release.
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);assert(unloadCalls==0);
 lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==1);assert(unloadCalls==1);
 // A different trainer session between press and click cancels the load.
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);++device.session;
 lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==1);
 device.loaded=true;device.fitnessMode=5;snapshot("build/preview-loaded.ppm");
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(unloadCalls==2);
 auto* modePanel=lv_obj_get_child(lv_screen_active(),-2);
 // Use actual pointer gestures starting on the loaded weight numeral.

 swipePointer(true);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=false;device.armed=true;swipePointer(true);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.armed=false;device.fitnessMode=4;device.settingsMs=now;
 swipePointer(true);assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 // A load on the trainer itself dismisses an already-open mode menu.
 device.loaded=true;device.fitnessMode=5;snapshot("build/mode-menu-external-load.ppm");
 assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=false;device.fitnessMode=4;device.settingsMs=now;
 swipePointer(true);
 auto* band=lv_obj_get_child(modePanel,1);
 lv_obj_send_event(band,LV_EVENT_CLICKED,nullptr);assert(device.mode==2);
 assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 swipePointer(true);snapshot("build/preview-mode-menu.ppm");
 const int settingsBefore=settingCalls;rotation=1;snapshot("build/preview-mode-dial.ppm");
 assert(settingCalls==settingsBefore);assert(device.mode==2); // Browse, don't send weights or modes.
 lv_obj_send_event(lv_obj_get_child(modePanel,4),LV_EVENT_CLICKED,nullptr);assert(device.mode==4);
 assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN)); // Center confirms the browsed mode.
 swipePointer(true);swipePointer(false);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 for(int mode: {1,2,4,6,7,8,0,3}) {
  device.mode=mode;char path[80];snprintf(path,sizeof(path),"build/preview-mode-%d.ppm",mode);snapshot(path);
  assert(ringColor==((mode==0||mode==3)?0:ui::theme::dimColor(ui::theme::modeAccent(mode))));
 }
 // Exercise actual LVGL hit-testing, not just a directly dispatched button event.
 device.loaded=false;device.armed=false;device.mode=0;device.fitnessMode=0;
 snapshot("build/idle-fallback.ppm");swipePointer(false);
 pointerX=180;pointerY=71;
 touching=true;snapshot("build/preview-idle-touch.ppm");
 touching=false;snapshot("build/preview-idle-selector.ppm");
 assert(!lv_obj_has_flag(selector,LV_OBJ_FLAG_HIDDEN));
 pointerY=200;touching=true;snapshot("build/preview-idle-select-down.ppm");
 touching=false;snapshot("build/preview-idle-selected.ppm");
 assert(device.selected==1);
 assert(lv_obj_has_flag(selector,LV_OBJ_FLAG_HIDDEN));
 assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 lv_obj_send_event(lv_obj_get_child(modePanel,4),LV_EVENT_CLICKED,nullptr);
 assert(device.mode==1);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 // Actual pointer tap on the numeral loads; rotating while loaded changes weight.
 device.mode=1;device.fitnessMode=4;device.loaded=false;device.settingsMs=now;
 snapshot("build/preview-tap-ready.ppm");
 const int loadsBefore=loadCalls;pointerX=180;pointerY=160;
 touching=true;snapshot("build/preview-tap-down.ppm");
 touching=false;snapshot("build/preview-tap-release.ppm");assert(loadCalls==loadsBefore+1);
 device.loaded=true;device.fitnessMode=5;snapshot("build/preview-tap-loaded.ppm");assert(ringColor==ui::theme::modeAccent(1));
 const int weightBefore=device.weightLbs,loadedWritesBefore=settingCalls;rotation=1;
 snapshot("build/preview-adjust-loaded.ppm");assert(lv_obj_get_style_text_font(action,0)==&font_inter_80);snapshot("build/preview-adjust-confirmed.ppm");
 assert(settingCalls==loadedWritesBefore+1);assert(device.weightLbs>weightBefore);
 now+=600;snapshot("build/preview-dial-settled.ppm");assert(lv_obj_get_style_text_font(action,0)==&font_inter_72);
 // Four icon/value controls must hit the intended independent setting.
 device.loaded=false;device.fitnessMode=4;device.settingsMs=now;
 const int x[]={118,242,118,242},y[]={240,240,289,289};
 for(int slot=1;slot<4;++slot){
  const int tappedWeight=device.chainsLbs,tappedEcc=device.eccentricLbs,tapWrites=settingCalls;
  pointerX=x[slot];pointerY=y[slot];touching=true;snapshot("build/modifier-down.ppm");touching=false;snapshot("build/modifier-up.ppm");
  assert(device.chainsLbs==tappedWeight&&device.eccentricLbs==tappedEcc&&settingCalls==tapWrites);
  const int before=slot==1?device.chainsLbs:slot==2?vlink::chainWeight(device,true):device.eccentricLbs;
  rotation=1;snapshot("build/modifier-turn.ppm");snapshot("build/modifier-confirmed.ppm");
  const int after=slot==1?device.chainsLbs:slot==2?vlink::chainWeight(device,true):device.eccentricLbs;assert(after>before);
 }
 assert(strcmp(lv_label_get_text(action),"7.5")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(),6)),"kg")==0);
 const int before=settingCalls;device.loaded=true;device.fitnessMode=5;rotation=1;snapshot("build/modifier-loaded.ppm");assert(settingCalls==before);
 // Right swipe opens cable page; rotation does not change resistance there.
 device.loaded=false;device.fitnessMode=4;device.cableMs=now;
 swipePointer(false);auto* cablePanel=lv_obj_get_child(lv_screen_active(),-3);
 assert(!lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));assert(device.cableActive);
 const int cableSettingsBefore=settingCalls;rotation=1;snapshot("build/preview-cable-cm.ppm");assert(settingCalls==cableSettingsBefore);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(cablePanel,2)),"cm")==0);
 device.lengthUnitRaw=1;device.cableMs=now;snapshot("build/preview-cable-inch.ppm");
 assert(strcmp(lv_label_get_text(lv_obj_get_child(cablePanel,2)),"in")==0);
 // Movement alone and a routine poll of the old saved length do not dismiss.
 device.cableCm=60;device.cableMs=now;now+=1200;snapshot("build/preview-cable-moving.ppm");assert(!lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));
 device.cableOffsetCm=60;device.cableMs=now;snapshot("build/preview-cable-saved.ppm");assert(lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));assert(!device.cableActive);
 // Saving the same offset also returns after the device enters then leaves its cable screen.
 device.cableScreenId=10;device.cableMs=now;swipePointer(false);device.cableScreenId=20;snapshot("build/cable-screen-entered.ppm");
 now+=1200;device.cableScreenId=10;device.cableMs=now;snapshot("build/cable-same-length.ppm");assert(lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));
 // Loaded or armed swipes stay on the control page and cannot start adjustment.
 device.loaded=true;device.fitnessMode=5;swipePointer(false);assert(!device.cableActive);assert(lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=false;device.armed=true;swipePointer(false);assert(!device.cableActive);assert(lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));device.armed=false;
 // Slow 48px drags work across empty space, the number, buttons and footer.
 auto slowSwipe=[&](bool left,int x,int y){
  const int loads=loadCalls,unloads=unloadCalls,settings=settingCalls;
  pointerX=x;pointerY=y;touching=true;snapshot("build/slow-swipe-start.ppm");
  for(int step=0;step<8;++step){pointerX+=left?-4:4;now+=100;device.statusMs=now;ui::loop();lv_timer_handler();}
  touching=false;snapshot("build/slow-swipe-end.ppm");
  assert(loadCalls==loads&&unloadCalls==unloads&&settingCalls==settings);
 };
 device.loaded=false;device.fitnessMode=4;
 for(int y:{105,160,240,300}){
  slowSwipe(true,230,y);assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
  slowSwipe(false,110,y);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
  slowSwipe(false,110,y);assert(!lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));
  slowSwipe(true,230,y);assert(lv_obj_has_flag(cablePanel,LV_OBJ_FLAG_HIDDEN));
 }
 assert(lv_obj_has_flag(lv_obj_get_child(lv_screen_active(),3),LV_OBJ_FLAG_HIDDEN));
 snapshot("build/preview-no-title.ppm");
 now+=1000;snapshot("build/preview-steady.ppm");const int flushBefore=flushCalls;
 snapshot("build/preview-steady-again.ppm");assert(flushCalls==flushBefore);
 device.loaded=false;device.fitnessMode=4;snapshot("build/preview-idle-final.ppm");const int idleFlushBefore=flushCalls;
 snapshot("build/preview-idle-steady.ppm");assert(flushCalls==idleFlushBefore);
 // First fresh idle connection opens mode selection, but unknown/connecting does not.
 ++device.session;device.link=vlink::LinkState::Connecting;device.mode=-1;
 snapshot("build/preview-connecting-unknown.ppm");assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.link=vlink::LinkState::Ready;device.mode=0;device.fitnessMode=0;
 snapshot("build/preview-initial-idle-modes.ppm");assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 auto* center=lv_obj_get_child(modePanel,4);auto* name=lv_obj_get_child(center,0);
 assert(strcmp(lv_label_get_text(name),"Weight\nTraining")==0);
 rotation=1;snapshot("build/preview-mode-name-band.ppm");assert(strcmp(lv_label_get_text(name),"Resistance\nBand")==0);
 for(const char* path:{"build/preview-mode-name-damper.ppm","build/preview-mode-name-isokinetic.ppm","build/preview-mode-name-weight.ppm","build/preview-mode-name-band.ppm"}){rotation=1;snapshot(path);}
 lv_obj_send_event(center,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-mode-name-confirmed.ppm");
 assert(device.mode==2);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.mode=0;snapshot("build/preview-idle-reopen.ppm");assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 for(int unsupported:{3,6,8,99}){device.mode=unsupported;snapshot("build/unsupported-mode.ppm");assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));}
 device.mode=1;snapshot("build/supported-mode-return.ppm");assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 ++device.session;device.mode=1;snapshot("build/preview-initial-active-mode.ppm");assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.mode=2;device.fitnessMode=4;device.lengthUnitRaw=0;device.weightUnitRaw=1;device.twinConnection=2;device.twinMs=now;device.twinLastRole=1;device.twinRoleMs=now;device.modeValues[0]=106;device.modeValueMs[0]=now;device.modeValues[1]=150;device.modeValueMs[1]=now;
 snapshot("build/preview-band-max-kg.ppm");assert(strcmp(lv_label_get_text(action),"48.0")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(),6)),"Max kg")==0);
 snapshot("build/preview-band-steady.ppm");const int bandFlushBefore=flushCalls;
 snapshot("build/preview-band-steady-again.ppm");assert(flushCalls==bandFlushBefore);
 device.twinMs=now;device.modeValueMs[0]=now;
 const int bandLoadStart=loadCalls,bandStopStart=unloadCalls;
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==bandLoadStart+1);
 device.loaded=true;device.fitnessMode=5;snapshot("build/band-loaded.ppm");
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(unloadCalls==bandStopStart+1);
 device.loaded=false;device.fitnessMode=4;snapshot("build/band-unloaded.ppm");
 device.weightUnitRaw=0;snapshot("build/preview-band-max-lbs.ppm");assert(strcmp(lv_label_get_text(action),"106")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(),6)),"Max lbs")==0);
 const int bandLoads=loadCalls,bandUnloads=unloadCalls,bandSettings=settingCalls;
 lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-band-tap.ppm");
 assert(loadCalls==bandLoads&&unloadCalls==bandUnloads&&settingCalls==bandSettings);
 rotation=1;snapshot("build/preview-band-turn.ppm");snapshot("build/preview-band-sent.ppm");
 assert(bandCalls==1&&bandRequested==107);assert(device.modeValues[0]==106); // No optimistic device confirmation.
 device.modeValues[0]=107;device.modeValueMs[0]=now;snapshot("build/preview-band-confirmed.ppm");
 device.weightUnitRaw=1;rotation=1;snapshot("build/preview-band-kg-turn.ppm");snapshot("build/preview-band-kg-sent.ppm");
 assert(bandCalls==2&&bandRequested==108);
 device.modeValues[0]=108;device.modeValueMs[0]=now;snapshot("build/preview-band-kg-confirmed.ppm");
 device.loaded=true;device.fitnessMode=5;rotation=1;snapshot("build/preview-band-loaded.ppm");snapshot("build/preview-band-loaded-wait.ppm");assert(bandCalls==2);
 device.loaded=false;device.fitnessMode=4;rotation=1;snapshot("build/preview-band-timeout-start.ppm");snapshot("build/preview-band-timeout-sent.ppm");assert(bandCalls==3);
 now+=5100;device.modeValueMs[0]=now;snapshot("build/preview-band-timeout.ppm");assert(strcmp(lv_label_get_text(action),"49.0")==0);
 rotation=1;snapshot("build/preview-band-mode-change-start.ppm");device.mode=1;snapshot("build/preview-band-mode-change.ppm");assert(bandCalls==3);
 device.mode=2;
 device.modeValueMs[0]=0;snapshot("build/preview-band-unknown.ppm");assert(strcmp(lv_label_get_text(action),"--")==0);
 device.mode=1;snapshot("build/preview-weight-after-band.ppm");assert(lv_obj_get_width(action)!=154);
 device.mode=4;device.fitnessMode=4;device.loaded=false;device.twinConnection=2;device.twinMs=now;device.twinLastRole=1;device.twinRoleMs=now;device.modeValues[5]=4;device.modeValueMs[5]=now;
 snapshot("build/preview-damper-factor.ppm");assert(strcmp(lv_label_get_text(action),"17")==0);
 auto* damperTwin=lv_obj_get_child(lv_screen_active(),7);assert(!lv_obj_has_flag(damperTwin,LV_OBJ_FLAG_HIDDEN));
 lv_area_t unitBounds,twinBounds;lv_obj_get_coords(lv_obj_get_child(lv_screen_active(),6),&unitBounds);lv_obj_get_coords(damperTwin,&twinBounds);assert(unitBounds.x2<twinBounds.x1);
 device.twinConnection=0;snapshot("build/preview-damper-single.ppm");assert(lv_obj_has_flag(damperTwin,LV_OBJ_FLAG_HIDDEN));
 device.twinConnection=2;device.twinMs=now-4000;snapshot("build/preview-damper-stale-twin.ppm");assert(lv_obj_has_flag(damperTwin,LV_OBJ_FLAG_HIDDEN));
 device.twinMs=now;snapshot("build/preview-damper-twin-restored.ppm");assert(!lv_obj_has_flag(damperTwin,LV_OBJ_FLAG_HIDDEN));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(),6)),"Factor")==0);
 auto* roman=lv_obj_get_child(lv_screen_active(),14);assert(strcmp(lv_label_get_text(roman),"V")==0);
 swipePointer(true);snapshot("build/preview-damper-menu.ppm");swipePointer(false);
 const int damperLoadBefore=loadCalls,damperUnloadBefore=unloadCalls;
 device.twinMs=now;device.modeValueMs[5]=now;
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);
 assert(loadCalls==damperLoadBefore+1);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==damperLoadBefore+1);
 device.loaded=true;device.fitnessMode=5;snapshot("build/preview-damper-loaded-tap.ppm");
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(unloadCalls==damperUnloadBefore+1);
 device.loaded=false;device.fitnessMode=4;device.twinMs=now;device.modeValueMs[5]=now;snapshot("build/preview-damper-unloaded-tap.ppm");
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_PRESS_LOST,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==damperLoadBefore+1);
 lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);device.mode=1;lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==damperLoadBefore+1);device.mode=4;
 snapshot("build/damper-steady.ppm");const int damperFlush=flushCalls;snapshot("build/damper-steady-again.ppm");assert(flushCalls==damperFlush);
 rotation=1;snapshot("build/damper-turn.ppm");snapshot("build/damper-send.ppm");assert(damperCalls==1&&damperRequested==5);assert(device.modeValues[5]==4);
 device.modeValues[5]=5;device.modeValueMs[5]=now;snapshot("build/damper-confirmed.ppm");assert(strcmp(lv_label_get_text(action),"21")==0);assert(strcmp(lv_label_get_text(roman),"VI")==0);
 device.loaded=true;device.fitnessMode=5;rotation=1;snapshot("build/damper-loaded.ppm");snapshot("build/damper-loaded-wait.ppm");assert(damperCalls==1);
 device.loaded=false;device.fitnessMode=4;rotation=1;snapshot("build/damper-timeout-start.ppm");snapshot("build/damper-timeout-sent.ppm");assert(damperCalls==2);
 now+=5100;device.modeValueMs[5]=now;snapshot("build/damper-timeout.ppm");assert(strcmp(lv_label_get_text(action),"21")==0);
 device.modeValueMs[5]=0;snapshot("build/damper-stale.ppm");assert(strcmp(lv_label_get_text(action),"--")==0);
 device.modeValues[5]=9;device.modeValueMs[5]=now;snapshot("build/damper-max.ppm");rotation=1;snapshot("build/damper-max-clamped.ppm");assert(damperCalls==2);
 device.modeValues[5]=0;device.modeValueMs[5]=now;snapshot("build/damper-min.ppm");rotation=-1;snapshot("build/damper-min-clamped.ppm");assert(damperCalls==2);
 rotation=1;snapshot("build/damper-cancel-start.ppm");device.mode=1;snapshot("build/damper-cancel.ppm");assert(damperCalls==2);assert(lv_obj_has_flag(roman,LV_OBJ_FLAG_HIDDEN));
 // Isokinetic: dedicated speed and eccentric editors, readback, bounds and loaded guards.
 device.mode=7;device.fitnessMode=4;device.loaded=false;device.twinConnection=0;device.weightUnitRaw=1;
 for(int i=7;i<=11;++i){device.modeValueMs[i]=now;device.modeValues[i]=i==7?100:i<10?0:5;}
 snapshot("build/preview-isokinetic.ppm");assert(strcmp(lv_label_get_text(action),"0.1")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(lv_screen_active(),6)),"m/s")==0);
 const int isoFlush=flushCalls;snapshot("build/iso-steady.ppm");assert(flushCalls==isoFlush);
 rotation=1;snapshot("build/iso-turn.ppm");assert(lv_obj_get_style_text_font(action,0)==&font_inter_80);snapshot("build/iso-send.ppm");
 assert(isoCalls==1&&isoRequestedIndex==7&&isoRequestedValue==200);assert(device.modeValues[7]==100);
 device.modeValues[7]=200;device.modeValueMs[7]=now;snapshot("build/iso-confirmed.ppm");assert(strcmp(lv_label_get_text(action),"0.2")==0);
 device.loaded=true;device.fitnessMode=5;rotation=1;snapshot("build/iso-loaded.ppm");assert(isoCalls==1);swipePointer(true);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=false;device.fitnessMode=4;
 auto* speedTab=lv_obj_get_child(lv_screen_active(),8);auto* returnTab=lv_obj_get_child(lv_screen_active(),9);
 auto* eccTab=lv_obj_get_child(lv_screen_active(),11);auto* limitTab=lv_obj_get_child(lv_screen_active(),10);
 lv_obj_send_event(eccTab,LV_EVENT_CLICKED,nullptr);snapshot("build/iso-auto.ppm");assert(strcmp(lv_label_get_text(action),"")==0);
 rotation=1;snapshot("build/iso-ecc-turn.ppm");snapshot("build/iso-ecc-send.ppm");assert(isoCalls==2&&isoRequestedIndex==9&&isoRequestedValue==100);
 device.modeValues[9]=100;device.modeValueMs[9]=now;snapshot("build/iso-ecc-confirmed.ppm");
 lv_obj_send_event(returnTab,LV_EVENT_CLICKED,nullptr);snapshot("build/iso-return-type.ppm");rotation=1;snapshot("build/iso-return-turn.ppm");snapshot("build/iso-return-send.ppm");assert(isoCalls==3&&isoRequestedIndex==8&&isoRequestedValue==1);
 device.modeValues[8]=1;device.modeValueMs[8]=now;snapshot("build/iso-return-confirmed.ppm");assert(strcmp(lv_label_get_text(action),"Weight")==0);
 lv_obj_send_event(eccTab,LV_EVENT_CLICKED,nullptr);snapshot("build/iso-resistance.ppm");assert(strcmp(lv_label_get_text(action),"2.5")==0);
 rotation=1;snapshot("build/iso-resistance-turn.ppm");snapshot("build/iso-resistance-send.ppm");assert(isoCalls==4&&isoRequestedIndex==10&&isoRequestedValue==7);
 device.modeValues[10]=7;device.modeValueMs[10]=now;snapshot("build/iso-resistance-confirmed.ppm");
 lv_obj_send_event(limitTab,LV_EVENT_CLICKED,nullptr);snapshot("build/iso-limit.ppm");rotation=1;snapshot("build/iso-limit-turn.ppm");snapshot("build/iso-limit-send.ppm");assert(isoCalls==5&&isoRequestedIndex==11);
 now+=5100;for(int i=7;i<=11;++i)device.modeValueMs[i]=now;snapshot("build/iso-timeout.ppm");assert(strcmp(lv_label_get_text(action),"2.5")==0);
 lv_obj_send_event(speedTab,LV_EVENT_CLICKED,nullptr);snapshot("build/iso-speed-again.ppm");
 device.modeValues[7]=2000;device.modeValueMs[7]=now;snapshot("build/iso-max.ppm");rotation=1;snapshot("build/iso-max-clamped.ppm");assert(isoCalls==5);
 device.modeValues[7]=100;device.modeValueMs[7]=now;snapshot("build/iso-min.ppm");rotation=-1;snapshot("build/iso-min-clamped.ppm");assert(isoCalls==5);
 const int isoLoadBefore=loadCalls;lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(loadCalls==isoLoadBefore+1);
 swipePointer(true);assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.loaded=true;device.fitnessMode=5;snapshot("build/iso-load-ack.ppm");const int isoStopBefore=unloadCalls;lv_obj_send_event(action,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(action,LV_EVENT_CLICKED,nullptr);assert(unloadCalls==isoStopBefore+1);
 device.loaded=false;device.fitnessMode=4;device.modeValueMs[7]=0;snapshot("build/iso-stale.ppm");assert(strcmp(lv_label_get_text(action),"--")==0);
 device.mode=1;device.fitnessMode=999;snapshot("build/unmapped-state.ppm");assert(!lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.fitnessMode=4;snapshot("build/mapped-state-return.ppm");assert(lv_obj_has_flag(modePanel,LV_OBJ_FLAG_HIDDEN));
 device.mode=2;device.fitnessMode=4;device.loaded=false;device.selected=0;device.twinConnection=2;device.twinMs=now;device.twinLastRole=2;device.twinRoleMs=now;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-role-swapped.ppm");
 assert(!lv_obj_has_state(lv_obj_get_child(lv_obj_get_child(selector,2),0),LV_STATE_DISABLED));assert(lv_obj_has_state(lv_obj_get_child(lv_obj_get_child(selector,2),1),LV_STATE_DISABLED));
 device.loaded=true;device.fitnessMode=5;lv_obj_send_event(lv_obj_get_child(selector,3),LV_EVENT_CLICKED,nullptr);snapshot("build/controller-switch-loaded.ppm");assert(lv_obj_has_state(lv_obj_get_child(lv_obj_get_child(selector,2),1),LV_STATE_DISABLED));
 device.loaded=false;device.fitnessMode=4;device.mode=0;device.twinMs=now;snapshot("build/preview-twin-exit-ready.ppm");
 lv_obj_send_event(lv_obj_get_child(selector,3),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-exited.ppm");assert(!lv_obj_has_state(lv_obj_get_child(lv_obj_get_child(selector,2),1),LV_STATE_DISABLED));
 lv_obj_send_event(lv_obj_get_child(lv_obj_get_child(selector,2),1),LV_EVENT_CLICKED,nullptr);assert(device.selected==1);
 simulateAdvertising=false;device.twinConnection=0;device.selected=0;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);
 device.trainers[1].lastSeenMs=now-11000;snapshot("build/preview-trainer-not-detected.ppm");
 auto* absent=lv_obj_get_child(lv_obj_get_child(selector,2),1);assert(lv_obj_has_state(absent,LV_STATE_DISABLED));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(absent,1)),"Saved · Not detected")==0);
 lv_obj_send_event(absent,LV_EVENT_CLICKED,nullptr);assert(device.selected==0);
 device.trainers[1].lastSeenMs=now;snapshot("build/preview-trainer-nearby.ppm");assert(!lv_obj_has_state(absent,LV_STATE_DISABLED));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(absent,1)),"Saved · Nearby")==0);
 auto* twinAction=lv_obj_get_child(selector,3);auto* connectionNotice=lv_obj_get_child(selector,1);
 // All supported unloaded modes expose a working action; users need not
 // discover a separate device-side idle screen before creating or exiting Twin.
 const int beforeTwinRequests=twinCalls;
 for(int mode:{1,2,4,7}){
   device.mode=mode;device.fitnessMode=4;device.loaded=false;device.armed=false;device.trainers[1].lastSeenMs=now;
   snapshot("build/preview-create-twin-ready.ppm");
   assert(!lv_obj_has_flag(twinAction,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_state(twinAction,LV_STATE_DISABLED));
   assert(!strcmp(lv_label_get_text(connectionNotice),"Ready to create Twin"));
   lv_obj_send_event(twinAction,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-created.ppm");
   assert(device.endpoint.paired());
   assert(!strcmp(lv_label_get_text(lv_obj_get_child(twinAction,0)),"Exit Twin"));
   lv_obj_send_event(twinAction,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-exit-unloaded.ppm");
   assert(!device.endpoint.paired());
 }
 assert(twinCalls==beforeTwinRequests+8);
 // Reopening the selector explicitly scans, even with a remembered TV. A
 // connected TV or an open pairing window is never interrupted by navigation.
 const bool priorPairing=tvbridge::open;tvbridge::open=false;tvbridge::savedTv=true;
 const int beforeFindCalls=tvbridge::findCalls;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);
 snapshot("build/preview-explicit-trainer-discovery.ppm");assert(tvbridge::findCalls==beforeFindCalls+1);
 snapshot("build/preview-discovery-window-stable.ppm");assert(tvbridge::findCalls==beforeFindCalls+1);
 tvbridge::tvConnected=true;lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);
 assert(tvbridge::findCalls==beforeFindCalls+1);
 tvbridge::tvConnected=false;tvbridge::open=true;lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);
 assert(tvbridge::findCalls==beforeFindCalls+1);tvbridge::open=priorPairing;
 device.loaded=true;device.fitnessMode=5;snapshot("build/preview-twin-unload-required.ppm");
 assert(lv_obj_has_state(twinAction,LV_STATE_DISABLED));
 assert(!strcmp(lv_label_get_text(connectionNotice),"Unload both trainers first"));
 lv_obj_send_event(twinAction,LV_EVENT_CLICKED,nullptr);assert(twinCalls==beforeTwinRequests+8);
 device.loaded=false;device.fitnessMode=4;device.mode=1;device.trainers[1].lastSeenMs=now;
 snapshot("build/preview-twin-retry-ready.ppm");rejectTwinRequest=true;
 lv_obj_send_event(twinAction,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-twin-request-rejected.ppm");
 assert(!strcmp(lv_label_get_text(connectionNotice),"Could not start · Try again"));
 rejectTwinRequest=false;now+=3100;device.trainers[1].lastSeenMs=now;
 // A saved but unobserved peer is discoverable as an action, without being
 // misrepresented as nearby or enabling an unverified pairing command.
 device.discoveryPausedForTv=true;device.trainers[1].seen=false;snapshot("build/preview-saved-reconnect.ppm");
 assert(!lv_obj_has_flag(twinAction,LV_OBJ_FLAG_HIDDEN));
 assert(lv_obj_has_state(twinAction,LV_STATE_DISABLED));
 assert(!strcmp(lv_label_get_text(connectionNotice),"Swipe right · Find trainers"));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(absent,1)),"Saved · Reconnect")==0);
 device.discoveryPausedForTv=false;device.trainers[1].seen=true;device.trainers[1].lastSeenMs=now;
 // A third nearby trainer remains discoverable with both saved slots occupied.
 auto& discovered=device.nearby[0];strcpy(discovered.address,"10:20:30:aa:bb:03");strcpy(discovered.name,"VOLTRA - New");discovered.seen=true;discovered.lastSeenMs=now;
 snapshot("build/preview-discovery-list.ppm");
 auto* list=lv_obj_get_child(selector,2);auto* newRow=lv_obj_get_child(list,2);
 assert(!lv_obj_has_flag(newRow,LV_OBJ_FLAG_HIDDEN));
 assert(strcmp(lv_label_get_text(lv_obj_get_child(newRow,1)),"New · Nearby")==0);
 assert(!lv_obj_has_flag(twinAction,LV_OBJ_FLAG_HIDDEN)); // Discovery ambiguity never hides the action.
 lv_obj_scroll_to_y(list,140,LV_ANIM_OFF);snapshot("build/preview-unsaved-nearby.ppm");
 const uint32_t beforeNewSession=device.session;
 lv_obj_send_event(newRow,LV_EVENT_CLICKED,nullptr);snapshot("build/preview-saved-slots-full.ppm");
 assert(device.session==beforeNewSession&&strcmp(device.trainers[1].address,"10:20:30:aa:bb:02")==0);
 assert(strcmp(lv_label_get_text(lv_obj_get_child(selector,1)),"Saved slots full · Manage devices")==0);
 // Recycled row identity between touch-down and release cannot select another device.
 lv_obj_send_event(newRow,LV_EVENT_PRESSED,nullptr);
 strcpy(discovered.address,"10:20:30:aa:bb:04");snapshot("build/discovery-row-changed.ppm");
 lv_obj_send_event(newRow,LV_EVENT_CLICKED,nullptr);assert(device.session==beforeNewSession);
 // A free saved slot accepts an explicitly selected nearby device.
 const auto savedRight=device.trainers[1];device.trainers[1]={};strcpy(device.message,"Connected");
 snapshot("build/preview-new-trainer-free-slot.ppm");lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
 lv_obj_send_event(lv_obj_get_child(list,1),LV_EVENT_CLICKED,nullptr);snapshot("build/new-trainer-selected.ppm");
 assert(device.selected==1&&strcmp(device.trainers[1].address,discovered.address)==0);
 device.trainers[1]=savedRight;device.nearby[0]={};device.selected=0;device.trainers[1].lastSeenMs=now;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-two-trainers-restored.ppm");
 // USB power renders only the bolt, without a misleading numeric percentage.
 usbConnected=true;now+=1000;snapshot("build/preview-selector-usb.ppm");
 assert(!strcmp(lv_label_get_text(lv_obj_get_child(batteryWidget,0)),""));
 usbConnected=false;now+=1000;snapshot("build/preview-selector-battery.ppm");
 assert(!strcmp(lv_label_get_text(lv_obj_get_child(batteryWidget,0)),"76%"));
 // Mitigation must block a voice Release before it reaches the link, even
 // with a loaded, connected trainer. Existing touch assertions above still pass.
 device.link=vlink::LinkState::Ready;device.selected=0;device.loaded=true;device.fitnessMode=5;
 const int beforeVoiceStops=unloadCalls,beforeVoiceLoads=loadCalls;
 assert(!ui::voiceAction({voice::EventKind::Release,now,device.session,device.mode}));
 assert(unloadCalls==beforeVoiceStops);
 device.loaded=false;device.fitnessMode=4;
 assert(!ui::voiceAction({voice::EventKind::Load,now,device.session,device.mode}));
 assert(loadCalls==beforeVoiceLoads);
 // Forget is explicit, cancellable, safe while unloaded, and sends no training commands.
 const int forgetLoads=loadCalls,forgetUnloads=unloadCalls,forgetSettings=settingCalls;
 device.selected=0;device.link=vlink::LinkState::Ready;device.mode=1;device.fitnessMode=4;device.loaded=false;
 lv_obj_send_event(lv_obj_get_child(lv_screen_active(),1),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-manage-entry.ppm");
 auto* manage=lv_obj_get_child(lv_layer_top(),1);auto* confirm=lv_obj_get_child(lv_layer_top(),2);
 swipePointer(false);snapshot("build/preview-tools-manage-entry.ppm");
 lv_obj_send_event(lv_obj_get_child(toolsPanel,3),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-manage-trainers.ppm");
 assert(!lv_obj_has_flag(manage,LV_OBJ_FLAG_HIDDEN));
 lv_obj_send_event(lv_obj_get_child(manage,2),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-forget-trainer.ppm");
 assert(!lv_obj_has_flag(confirm,LV_OBJ_FLAG_HIDDEN));
 lv_obj_send_event(lv_obj_get_child(confirm,4),LV_EVENT_CLICKED,nullptr);assert(device.trainers[0].address[0]);
 lv_obj_send_event(lv_obj_get_child(manage,2),LV_EVENT_CLICKED,nullptr);
 device.loaded=true;device.fitnessMode=5;snapshot("build/preview-forget-loaded.ppm");
 assert(lv_obj_has_state(lv_obj_get_child(confirm,3),LV_STATE_DISABLED));
 lv_obj_send_event(lv_obj_get_child(confirm,3),LV_EVENT_CLICKED,nullptr);assert(device.trainers[0].address[0]);
 device.loaded=false;device.fitnessMode=4;snapshot("build/preview-forget-trainer-ready.ppm");
 const uint32_t forgetSession=device.session;
 lv_obj_send_event(lv_obj_get_child(confirm,3),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-trainer-forgotten.ppm");
 assert(!device.trainers[0].address[0]&&device.selected==-1&&device.session>forgetSession);
 lv_obj_send_event(lv_obj_get_child(manage,4),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-forget-back.ppm");
 tvbridge::open=false;tvbridge::savedTv=true;tvbridge::tvConnected=true;
 lv_obj_send_event(lv_obj_get_child(toolsPanel,2),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-tv-forget-action.ppm");
 lv_obj_send_event(lv_obj_get_child(tvPanel,6),LV_EVENT_CLICKED,nullptr);snapshot("build/preview-forget-tv-confirm.ppm");
 lv_obj_send_event(lv_obj_get_child(confirm,4),LV_EVENT_CLICKED,nullptr);assert(tvbridge::forgetCalls==0&&tvbridge::savedTv);
 lv_obj_send_event(lv_obj_get_child(tvPanel,6),LV_EVENT_CLICKED,nullptr);
 snapshot("build/preview-forget-tv-ready.ppm");lv_obj_send_event(lv_obj_get_child(confirm,3),LV_EVENT_CLICKED,nullptr);
 snapshot("build/preview-tv-forgotten.ppm");assert(tvbridge::forgetCalls==1&&!tvbridge::savedTv&&!tvbridge::tvConnected);
 assert(loadCalls==forgetLoads&&unloadCalls==forgetUnloads&&settingCalls==forgetSettings);
 printf("Tap-number load, loaded weight edits, mode menu, session, availability and idle navigation tests passed.\n");
 return 0;
}
