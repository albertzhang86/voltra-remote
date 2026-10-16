#include "../voice/control.h"
#include "ui.h"
#include "theme.h"
#include "mode_icons.h"
#include "connection_icons.h"
#include <cmath>
#include <cstring>
#include <Arduino.h>
#include <lvgl.h>
#include "../board/board.h"
#include "../link/voltra_link.h"
#include "../app/remote_model.h"
#include "../app/training_readout.h"
#include "../tv/tv_bridge.h"
LV_IMAGE_DECLARE(polar_bear_logo);
namespace ui {
namespace {
constexpr int TRAINER_ROWS=2+vlink::NEARBY_TRAINER_LIMIT;
lv_obj_t *trainerList,*connectionTools,*selectorCue,*discoverButton;
bool toolsPage=false;
int choiceSlots[TRAINER_ROWS];
char choiceAddresses[TRAINER_ROWS][18]{};
char pressedChoiceAddresses[TRAINER_ROWS][18]{};
bool choicePressed[TRAINER_ROWS]{};
uint32_t pressedChoiceSessions[TRAINER_ROWS]{};
lv_obj_t *screen,*trainerButton,*trainerLabel,*title,*number,*unit,*status,*details,*action,*unloadedDot,*selector,*choices[TRAINER_ROWS],*choiceLabels[TRAINER_ROWS],*choiceDetails[TRAINER_ROWS],*arc,*selectorNotice;
lv_scale_section_t* filledTicks;
lv_style_t filledTickStyle;
lv_obj_t *tabs[4],*tabValues[4],*isoLabels[4],*modePanel;
lv_obj_t *bandDetails,*damperRoman,*switchController,*switchControllerText;
bool choosingController=false;
uint32_t twinRequestRejectedUntil=0;
bool tvPage=false;
lv_obj_t *tvPanel,*tvStatus,*tvPin,*tvButton,*tvPairButton,*tvButtonText,*tvFindButton,*tvForgetButton,*tvBackButton;
lv_obj_t *managePanel,*manageRows[2],*manageNames[2],*manageButton;
lv_obj_t *forgetPanel,*forgetTitle,*forgetDescription,*forgetConfirm,*forgetStatus;
bool managingTrainers=false;
enum class ForgetTarget:uint8_t {None,Tvs,Trainer};
ForgetTarget forgetTarget=ForgetTarget::None;
int forgetSlot=-1;
char forgetAddress[18]{};
bool pairedChoice(const vlink::DeviceState& s,int slot,uint32_t now){return !choosingController&&vlink::twinActive(s,now)&&s.selected>=0&&slot!=s.selected&&!strcasecmp(s.trainers[slot].address,s.endpoint.peer);}
lv_obj_t *twinIcon;
uint32_t twinInk=theme::Muted,twinCutout=theme::Background;
lv_obj_t *cablePanel,*cableNumber,*cableUnit,*cableNotice;
bool choosingCable=false,cableRequested=false,cableMoved=false;
uint32_t cableSession=0,cableSaveSeq=0,cableStarted=0,dialPulseMs=0;
int cableBaseline=-1,cableInitial=-1,cableHomeScreen=-1;
bool cableScreenEntered=false;
bool choosingMode=false,unloadGesture=false,initialModeHandled=false,fallbackModeMenu=false;
int focusedMode=0;
int pendingSwipe=0;
bool repaintView=true;
constexpr int MODE_COUNT=4;
lv_obj_t *modeIcons[MODE_COUNT],*modeCenter,*modeName,*currentModeIcon;
int displayedMode=0;
const int modeIds[]={1,2,4,7};
bool mappedMode(int mode){for(int id:modeIds)if(id==mode)return true;return false;}
const char* modeNames[]={"Weight\nTraining","Resistance\nBand","Damper","Isokinetic"};
remote::Editor edits[4];
remote::Editor bandEdit,damperEdit,isoEdits[5];
int isoPage=0;
int isoIndex(const vlink::DeviceState& s,int page){return page==0?7:page==1?8:page==2?(s.modeValues[8]==1?10:9):11;}
void resetIso(){for(auto& e:isoEdits)e.reset();isoPage=0;}
void isoText(char* text,size_t size,const vlink::DeviceState& s,int index,const remote::Editor& edit,uint32_t now){
 if(!vlink::fresh(s,now)||!edit.known||!s.modeValueMs[index]||now-s.modeValueMs[index]>=8000){snprintf(text,size,"--");return;}
 if(index==8)snprintf(text,size,"%s",edit.target==0?"Speed":"Weight");
 else if(index==9&&edit.target==0)snprintf(text,size,"Auto");
 else if(index==7||index==9)snprintf(text,size,"%.1f",edit.target/1000.0);
 else if(s.weightUnitRaw==1)snprintf(text,size,"%.1f",remote::displayKilograms(edit.target));
 else if(s.weightUnitRaw==0)snprintf(text,size,"%d",edit.target);
 else snprintf(text,size,"--");
}
int bandSessionMode=-1;
int page=0;uint32_t session=0,lastRender=0;
bool choosing=true,loadGesture=false,splashActive=true;
uint32_t gestureSession=0,loadSentMs=0;
int gestureMode=-1;

lv_color_t color(uint32_t value){return lv_color_hex(value);}
void setText(lv_obj_t* obj,const char* text){if(strcmp(lv_label_get_text(obj),text)){lv_label_set_text(obj,text);}}
void setTextColor(lv_obj_t* obj,lv_color_t ink,lv_style_selector_t part){if(!lv_color_eq(lv_obj_get_style_text_color(obj,part),ink)){lv_obj_set_style_text_color(obj,ink,part);}}
void setBgColor(lv_obj_t* obj,lv_color_t ink,lv_style_selector_t part){if(!lv_color_eq(lv_obj_get_style_bg_color(obj,part),ink)){lv_obj_set_style_bg_color(obj,ink,part);}}
void setFont(lv_obj_t* obj,const lv_font_t* font,lv_style_selector_t part){if(lv_obj_get_style_text_font(obj,part)!=font){lv_obj_set_style_text_font(obj,font,part);}}
void setTextOpacity(lv_obj_t* obj,lv_opa_t opacity,lv_style_selector_t part){if(lv_obj_get_style_text_opa(obj,part)!=opacity){lv_obj_set_style_text_opa(obj,opacity,part);}}
void setBgOpacity(lv_obj_t* obj,lv_opa_t opacity,lv_style_selector_t part){if(lv_obj_get_style_bg_opa(obj,part)!=opacity){lv_obj_set_style_bg_opa(obj,opacity,part);}}
void setHidden(lv_obj_t* obj,bool hidden){if(hidden!=lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)){if(hidden)lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);}}
bool pending(){for(const auto& e:isoEdits)if(e.pending)return true;if(bandEdit.pending||damperEdit.pending)return true;for(auto& e:edits)if(e.pending)return true;return false;}
bool showTwinAction(const vlink::DeviceState& s,uint32_t now){
 if(s.endpoint.paired()||s.twinPending)return true;
 if(s.selected<0||s.selected>=2||!s.trainers[s.selected].address[0])return false;
 // Keep the action discoverable when TV radio use pauses advertisements.
 // Remembering a peer does not enable pairing: canChangeTwin still verifies it.
 if(s.trainers[1-s.selected].address[0])return true;
 for(const auto& trainer:s.nearby)
   if(vlink::nearbyAdvertisement(trainer,now)&&strcasecmp(trainer.address,s.trainers[s.selected].address))return true;
 return false;
}
const char* twinNotice(const vlink::DeviceState& s,uint32_t now,bool visible){
 if(!strcmp(s.message,"Saved slots full: Manage devices"))return "Saved slots full · Manage devices";
 if(s.twinPending)return s.message;
 if(twinRequestRejectedUntil&&static_cast<int32_t>(twinRequestRejectedUntil-now)>0)return "Could not start · Try again";
 if(s.twinCompleted&&!s.twinSucceeded)return s.message;
 if(!visible)return s.discoveryPausedForTv?"Swipe right · Find trainers":"Nearby trainers";
 if(!vlink::fresh(s,now))return "Connect a trainer first";
 if(s.loaded||s.armed||!voltra::fitnessModeUnloaded(s.fitnessMode))return "Unload both trainers first";
 if(s.cableActive)return "Close cable setup first";
 if(pending()||loadSentMs||s.parameterSelectionPending||
    (s.controlBusyUntil&&static_cast<int32_t>(s.controlBusyUntil-now)>0))return "Waiting for current change...";
 if(!vlink::endpointFresh(s,now))return "Reading trainer status...";
 if(s.selected<0||s.selected>=2||strcasecmp(s.trainers[s.selected].address,s.endpoint.local))return "Verifying trainer identity...";
 if(s.mode!=0&&!mappedMode(s.mode))return "Exit current activity first";
 if(s.twinCompleted&&s.twinSucceeded&&s.mode==0)return s.message;
 if(!s.endpoint.paired()&&!vlink::nearbyTwinPeer(s,now))
   return s.discoveryPausedForTv?"Swipe right · Find trainers":"Find a second trainer to pair";
 return s.endpoint.paired()?"Twin connected":"Ready to create Twin";
}
lv_obj_t* label(lv_obj_t* parent,const char* text,int x,int y,const lv_font_t* font,uint32_t ink=theme::Text){
 auto* l=lv_label_create(parent);setText(l,text);setFont(l,font,0);setTextColor(l,color(ink),0);lv_obj_align(l,LV_ALIGN_TOP_MID,x,y);return l;
}
lv_obj_t* button(lv_obj_t* parent,int width,int height,int x,int y,uint32_t fill){
 auto* b=lv_button_create(parent);lv_obj_set_size(b,width,height);lv_obj_align(b,LV_ALIGN_TOP_MID,x,y);setBgColor(b,color(fill),0);lv_obj_set_style_radius(b,16,0);lv_obj_set_style_shadow_width(b,0,0);return b;
}
lv_obj_t* connectionButton(lv_obj_t* parent,const char* text,ConnectionIcon icon,int y){
 // All three actions fit inside the round display with equal, generous touch targets.
 auto* b=button(parent,240,56,0,y,theme::Surface);lv_obj_set_style_pad_all(b,0,0);
 setTextColor(b,color(theme::Text),0);setTextColor(b,color(theme::Muted),LV_STATE_DISABLED);
 setBgColor(b,color(theme::SurfaceLow),LV_STATE_DISABLED);lv_obj_set_style_color_filter_opa(b,LV_OPA_TRANSP,LV_STATE_DISABLED);
 auto* textLabel=label(b,text,0,0,&font_inter_16);lv_obj_set_width(textLabel,170);lv_obj_align(textLabel,LV_ALIGN_LEFT_MID,56,0);
 lv_obj_remove_local_style_prop(textLabel,LV_STYLE_TEXT_COLOR,0);
 lv_obj_add_event_cb(b,[](lv_event_t* e){
  auto* obj=static_cast<lv_obj_t*>(lv_event_get_target(e));lv_area_t a;lv_obj_get_coords(obj,&a);
  const auto icon=static_cast<ConnectionIcon>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  drawConnectionIcon(lv_event_get_layer(e),a.x1+20,(a.y1+a.y2)/2-12,icon,lv_obj_get_style_text_color(obj,0));
 },LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(icon)));
 return b;
}
void openForget(ForgetTarget target,int slot=-1){
 if(pending()||loadSentMs)return;
 const auto s=vlink::voltraLink.state();
 if(target==ForgetTarget::Trainer){
   if(slot<0||slot>=2||!s.trainers[slot].address[0])return;
   forgetSlot=slot;strlcpy(forgetAddress,s.trainers[slot].address,sizeof(forgetAddress));
   setText(forgetTitle,"Forget trainer?");
   char text[140];snprintf(text,sizeof(text),"%s\nRemove this saved connection.\nTrainer settings stay unchanged.",s.trainers[slot].name[0]?s.trainers[slot].name:"VOLTRA");
   setText(forgetDescription,text);
 }else{
   setText(forgetTitle,"Forget TVs?");
   setText(forgetDescription,"Remove saved TV pairings.\nAlso forget the knob on your TV.\nThen choose Pair TV to reconnect.");
 }
 forgetTarget=target;setText(forgetStatus,"");setHidden(forgetPanel,false);
}
void confirmForget(lv_event_t*){
 const auto s=vlink::voltraLink.state();
 const bool allowed=forgetTarget==ForgetTarget::Tvs?
   vlink::canSwitch(s,millis())&&!s.loaded&&!s.armed&&!s.parameterSelectionPending:
   vlink::canForgetTrainer(s,forgetSlot,millis());
 if(!allowed||pending()||loadSentMs)return;
 const bool success=forgetTarget==ForgetTarget::Tvs?tvbridge::forgetTvs():vlink::voltraLink.forgetTrainer(forgetSlot,forgetAddress);
 if(!success){setText(forgetStatus,"Could not forget. Try again.");return;}
 forgetTarget=ForgetTarget::None;setHidden(forgetPanel,true);
}
void choose(lv_event_t* event){
 if(forgetTarget!=ForgetTarget::None||managingTrainers||toolsPage)return;
 const int row=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
 if(row<0||row>=TRAINER_ROWS)return;
 const auto code=lv_event_get_code(event);const auto s=vlink::voltraLink.state();
 if(code==LV_EVENT_PRESSED){choicePressed[row]=true;pressedChoiceSessions[row]=s.session;strlcpy(pressedChoiceAddresses[row],choiceAddresses[row],18);return;}
 if(code!=LV_EVENT_CLICKED)return;
 if(choicePressed[row]){choicePressed[row]=false;if(pressedChoiceSessions[row]!=s.session||strcmp(pressedChoiceAddresses[row],choiceAddresses[row]))return;}
 const int slot=choiceSlots[row];
 if(slot>=0&&strcmp(s.trainers[slot].address,choiceAddresses[row]))return;
 if(slot>=0&&(!vlink::trainerAvailable(s,slot,millis())||pairedChoice(s,slot,millis())))return;
 const bool selected=slot>=0?vlink::voltraLink.selectTrainer(slot):vlink::voltraLink.selectNearbyTrainer(choiceAddresses[row]);
 if(selected){choosingController=false;choosing=false;lv_obj_add_flag(selector,LV_OBJ_FLAG_HIDDEN);for(auto& e:edits)e.reset();bandEdit.reset();damperEdit.reset();resetIso();loadGesture=false;loadSentMs=0;}
}
void openSelector(lv_event_t*){
 choosing=true;toolsPage=false;setHidden(connectionTools,true);loadGesture=false;lv_obj_remove_flag(selector,LV_OBJ_FLAG_HIDDEN);
 // Explicit trainer selection opens a bounded discovery window. Trusted-TV
 // advertising otherwise pauses scanning even when no TV is connected.
 // Do not do this at startup or on every render: trusted TVs must reconnect.
 if(!tvbridge::connected()&&!tvbridge::pairing())tvbridge::findTrainers();
}
void changePage(lv_event_t* e){
 if(vlink::voltraLink.state().mode==7){if(!pending())isoPage=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));loadGesture=false;return;}
 page=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));loadGesture=false;
 if(!pending()&&!loadSentMs&&vlink::voltraLink.requestParameter(static_cast<vlink::Parameter>(page)))edits[page].reset();
}
void actionEvent(lv_event_t* event){
 const auto s=vlink::voltraLink.state();const auto code=lv_event_get_code(event);
 if((s.mode!=1&&s.mode!=2&&s.mode!=4&&s.mode!=7)||(s.mode==1&&page!=0)||(s.mode==7&&isoPage!=0)||choosing||choosingMode||choosingCable||splashActive){loadGesture=false;unloadGesture=false;return;}
 if(code==LV_EVENT_PRESSED){
  loadGesture=false;unloadGesture=false;gestureSession=s.session;gestureMode=s.mode;
  if(s.link!=vlink::LinkState::Ready)return;
  unloadGesture=s.loaded||s.armed||!vlink::fresh(s,millis())||loadSentMs;
  if(!unloadGesture)loadGesture=vlink::canLoad(s,millis())&&!pending();
 }
 if(code==LV_EVENT_CLICKED){
  const bool load=loadGesture,unload=unloadGesture;loadGesture=false;unloadGesture=false;
  if(s.session!=gestureSession||s.mode!=gestureMode)return;
  if(unload){vlink::voltraLink.requestUnload(gestureSession,vlink::UnloadSource::Touch);loadSentMs=0;}
  else if(load&&vlink::canLoad(s,millis())&&!pending()&&vlink::voltraLink.requestLoad())loadSentMs=millis();
 }
 if(code==LV_EVENT_PRESS_LOST){loadGesture=false;unloadGesture=false;}
}
void refreshModeIcons(){
 setText(modeName,modeNames[focusedMode]);setTextColor(modeName,color(theme::modeAccent(modeIds[focusedMode])),0);
 for(int i=0;i<MODE_COUNT;++i){const uint32_t ink=theme::modeAccent(modeIds[i]);
  setBgColor(modeIcons[i],color(i==focusedMode?theme::modeTint(ink):theme::Surface),0);
  lv_obj_set_style_border_color(modeIcons[i],color(ink),0);lv_obj_set_style_border_width(modeIcons[i],i==focusedMode?2:0,0);
 }
 lv_obj_set_style_border_color(modeCenter,color(theme::modeAccent(modeIds[focusedMode])),0);lv_obj_invalidate(modeCenter);
}
void closeModes(){fallbackModeMenu=false;choosingMode=false;loadGesture=false;unloadGesture=false;lv_obj_add_flag(modePanel,LV_OBJ_FLAG_HIDDEN);}
void openModes(bool fallback=false){
 const auto state=vlink::voltraLink.state();
 if(state.loaded||state.armed||loadSentMs||pending()||(!fallback&&!vlink::canSwitch(state,millis())))return;
 choosingMode=true;loadGesture=false;unloadGesture=false;
 focusedMode=0;const auto s=vlink::voltraLink.state();for(int i=0;i<MODE_COUNT;++i)if(modeIds[i]==s.mode)focusedMode=i;
 refreshModeIcons();lv_obj_remove_flag(modePanel,LV_OBJ_FLAG_HIDDEN);
}
void selectMode(lv_event_t* event){
 const int index=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
 if(index>=MODE_COUNT)return;
 if(index>=0)focusedMode=index;
 refreshModeIcons();
 const auto s=vlink::voltraLink.state();
 // Confirming the existing mode is navigation only, even when loaded/busy.
 if(vlink::fresh(s,millis())&&s.mode==modeIds[focusedMode]){page=0;closeModes();return;}
 if(!pending()&&!loadSentMs&&vlink::voltraLink.requestMode(modeIds[focusedMode])){page=0;closeModes();}
}
void closeCable(){
 if(cableRequested&&vlink::voltraLink.state().link==vlink::LinkState::Ready&&!vlink::voltraLink.requestCable(false))return;
 choosingCable=false;cableRequested=false;page=0;lv_obj_add_flag(cablePanel,LV_OBJ_FLAG_HIDDEN);
}
void openCable(){
 const auto s=vlink::voltraLink.state();
 if(s.loaded||s.armed||loadSentMs)return;
 choosingCable=true;loadGesture=false;unloadGesture=false;
 cableSession=s.session;cableBaseline=s.cableOffsetCm;cableInitial=s.cableCm;cableSaveSeq=s.cableSavedSeq;cableMoved=false;cableHomeScreen=s.cableScreenId;cableScreenEntered=false;
 cableRequested=!pending()&&vlink::voltraLink.requestCable(true);cableStarted=millis();
 lv_obj_remove_flag(cablePanel,LV_OBJ_FLAG_HIDDEN);
}
void navigateSwipe(int direction){
 if(splashActive)return;
 loadGesture=false;unloadGesture=false;
 if(choosing){
   if(direction>0&&!toolsPage){toolsPage=true;setHidden(connectionTools,false);}
   else if(direction<0&&toolsPage){toolsPage=false;setHidden(connectionTools,true);}
   return;
 }
 if(choosingCable){if(direction<0)closeCable();return;}
 if(direction>0&&!choosingMode)openCable();
 else if(direction<0&&!choosingMode)openModes();
 else if(direction>0&&choosingMode){const bool fallback=fallbackModeMenu;closeModes();if(fallback)openSelector(nullptr);}
}
void modifierDraw(lv_event_t* event){
 if(displayedMode==7)return;
 const int index=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
 lv_area_t a;lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(event)),&a);
 a.x1+=7;a.x2=a.x1+35;
 const auto ink=color(index==page?theme::modeAccent(displayedMode):theme::Text);
 if(index==0)drawModeIcon(lv_event_get_layer(event),a,displayedMode>0?displayedMode:1,ink);
 else drawModifierIcon(lv_event_get_layer(event),a,index,ink,color(index==page?theme::Selected:theme::Surface));
}
void iconDraw(lv_event_t* event){
 const int index=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
 const int mode=index==-2?displayedMode:modeIds[index<0?focusedMode:index];
 lv_area_t bounds;lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(event)),&bounds);
 drawModeIcon(lv_event_get_layer(event),bounds,mode,color(theme::modeAccent(mode)));
}
}
// Track displacement before LVGL hit-testing so slow swipes and drags that
// cross button boundaries behave identically. Consume the whole gesture:
// its release must never become a load/unload or setting-selection tap.
void filterTouch(lv_indev_t* input,lv_indev_data_t* data){
 static bool down=false,consumed=false,eligible=false;
 static int startX=0,startY=0;
 const bool pressed=data->state==LV_INDEV_STATE_PRESSED;
 if(pressed&&!down){down=true;consumed=false;eligible=!splashActive&&!tvPage&&!managingTrainers&&forgetTarget==ForgetTarget::None;startX=data->point.x;startY=data->point.y;}
 if(pressed&&down&&eligible&&!consumed){
  const int dx=data->point.x-startX,dy=data->point.y-startY;
  if(abs(dx)>=28&&abs(dx)>abs(dy)*3/2){
   consumed=true;pendingSwipe=dx>0?1:-1;loadGesture=false;unloadGesture=false;
   lv_indev_wait_release(input);
  }
 }
 const bool suppress=consumed;
 if(!pressed){down=false;consumed=false;}
 if(suppress)data->state=LV_INDEV_STATE_RELEASED;
}
void begin(){
 screen=lv_screen_active();lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE);setBgColor(screen,color(theme::Background),0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
 arc=lv_scale_create(screen);lv_obj_set_size(arc,344,344);lv_obj_center(arc);
 lv_scale_set_mode(arc,LV_SCALE_MODE_ROUND_INNER);lv_scale_set_label_show(arc,false);
 lv_scale_set_total_tick_count(arc,61);lv_scale_set_major_tick_every(arc,5);
 lv_scale_set_range(arc,0,200);lv_scale_set_angle_range(arc,360);lv_scale_set_rotation(arc,270);
 lv_obj_remove_flag(arc,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(arc,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_pad_all(arc,0,0);setBgOpacity(arc,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(arc,0,0);lv_obj_set_style_arc_opa(arc,LV_OPA_TRANSP,LV_PART_MAIN);
 lv_obj_set_style_line_opa(arc,LV_OPA_TRANSP,LV_PART_MAIN);
 lv_obj_set_style_length(arc,7,LV_PART_ITEMS);lv_obj_set_style_length(arc,12,LV_PART_INDICATOR);
 lv_obj_set_style_line_width(arc,2,LV_PART_ITEMS);lv_obj_set_style_line_width(arc,3,LV_PART_INDICATOR);
 lv_obj_set_style_line_color(arc,color(theme::Track),LV_PART_ITEMS);lv_obj_set_style_line_color(arc,color(theme::Track),LV_PART_INDICATOR);
 lv_style_init(&filledTickStyle);lv_style_set_line_color(&filledTickStyle,color(theme::Muted));
 filledTicks=lv_scale_add_section(arc);lv_scale_section_set_range(filledTicks,0,0);
 lv_scale_section_set_style(filledTicks,LV_PART_ITEMS,&filledTickStyle);lv_scale_section_set_style(filledTicks,LV_PART_INDICATOR,&filledTickStyle);
 trainerButton=button(screen,176,38,0,29,theme::Surface);trainerLabel=label(trainerButton,"Choose Voltra",0,0,&font_inter_16);lv_obj_center(trainerLabel);lv_obj_set_width(trainerLabel,152);lv_label_set_long_mode(trainerLabel,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(trainerLabel,LV_TEXT_ALIGN_CENTER,0);lv_obj_add_event_cb(trainerButton,openSelector,LV_EVENT_CLICKED,nullptr);lv_obj_set_ext_click_area(trainerButton,18);
 status=label(screen,"SEARCHING",0,77,&font_inter_14,theme::Muted);
 title=label(screen,"",0,106,&font_inter_20,theme::Muted);lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);
 number=label(screen,"--",0,106,&font_inter_72);
 action=number;lv_obj_add_flag(action,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_ext_click_area(action,10);lv_obj_add_event_cb(action,actionEvent,LV_EVENT_ALL,nullptr);
 unloadedDot=lv_obj_create(screen);lv_obj_set_size(unloadedDot,7,7);lv_obj_set_style_radius(unloadedDot,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(unloadedDot,0,0);lv_obj_remove_flag(unloadedDot,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(unloadedDot,LV_OBJ_FLAG_SCROLLABLE);
 unit=label(screen,"kg",0,181,&font_inter_20,theme::Muted);
 lv_obj_set_width(unit,26);lv_obj_set_style_text_align(unit,LV_TEXT_ALIGN_CENTER,0);lv_obj_add_flag(unit,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
 twinIcon=lv_obj_create(screen);lv_obj_remove_style_all(twinIcon);lv_obj_set_size(twinIcon,24,20);lv_obj_remove_flag(twinIcon,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(twinIcon,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_event_cb(twinIcon,[](lv_event_t* e){
   lv_area_t a;lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)),&a);
   // Official VOLTRA Twin badge: interlocking links inside a filled circle.
   // Reference: Beyond Power Twin Mode Introduction, 0:24, -MsEybQnnSY.
   auto* layer=lv_event_get_layer(e);lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
   d.bg_opa=LV_OPA_COVER;d.bg_color=color(twinInk);d.radius=LV_RADIUS_CIRCLE;
   lv_area_t circle={a.x1+2,a.y1,a.x1+21,a.y1+19};lv_draw_rect(layer,&d,&circle);
   d.bg_opa=LV_OPA_TRANSP;d.border_opa=LV_OPA_COVER;d.border_color=color(twinCutout);d.border_width=2;d.radius=4;
   lv_area_t upper={a.x1+5,a.y1+4,a.x1+14,a.y1+11},lower={a.x1+10,a.y1+8,a.x1+19,a.y1+15};
   lv_draw_rect(layer,&d,&upper);lv_draw_rect(layer,&d,&lower);

 },LV_EVENT_DRAW_MAIN,nullptr);

 const int tabOrder[]={0,1,3,2};
 for(int slot=0;slot<4;++slot){const int i=tabOrder[slot];
  tabs[i]=button(screen,116,44,slot%2?62:-62,218+(slot/2)*49,theme::Surface);
  lv_obj_set_style_pad_all(tabs[i],0,0);
  tabValues[i]=label(tabs[i],"--",0,0,&font_inter_16);
  isoLabels[i]=label(tabs[i],"",0,0,&font_inter_12);lv_obj_align(isoLabels[i],LV_ALIGN_LEFT_MID,8,0);lv_obj_add_flag(isoLabels[i],LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(tabValues[i],LV_ALIGN_RIGHT_MID,-8,0);
  lv_obj_add_event_cb(tabs[i],modifierDraw,LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  lv_obj_add_event_cb(tabs[i],changePage,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 currentModeIcon=lv_obj_create(screen);lv_obj_set_size(currentModeIcon,44,44);lv_obj_align(currentModeIcon,LV_ALIGN_TOP_MID,0,273);
 setBgOpacity(currentModeIcon,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(currentModeIcon,0,0);lv_obj_set_style_pad_all(currentModeIcon,0,0);
 lv_obj_remove_flag(currentModeIcon,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(currentModeIcon,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(currentModeIcon,iconDraw,LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(-2)));
 details=label(screen,"",0,321,&font_inter_12,theme::Muted);
 damperRoman=label(screen,"",-65,136,&font_inter_20,theme::Muted);lv_obj_align(damperRoman,LV_ALIGN_CENTER,-70,0);lv_obj_add_flag(damperRoman,LV_OBJ_FLAG_HIDDEN);
 bandDetails=label(screen,"",0,251,&font_inter_14,theme::Muted);lv_obj_set_width(bandDetails,256);lv_obj_set_style_text_align(bandDetails,LV_TEXT_ALIGN_CENTER,0);lv_obj_add_flag(bandDetails,LV_OBJ_FLAG_HIDDEN);
 cablePanel=lv_obj_create(screen);lv_obj_set_size(cablePanel,360,360);lv_obj_center(cablePanel);
 setBgColor(cablePanel,color(theme::Background),0);lv_obj_set_style_border_width(cablePanel,0,0);lv_obj_set_style_pad_all(cablePanel,0,0);lv_obj_remove_flag(cablePanel,LV_OBJ_FLAG_SCROLLABLE);
 label(cablePanel,"CABLE LENGTH",0,67,&font_inter_20);
 cableNumber=label(cablePanel,"--",0,122,&font_inter_54,0xFFFFFF);
 cableUnit=label(cablePanel,"",0,184,&font_inter_20,theme::Muted);
 cableNotice=label(cablePanel,"",0,228,&font_inter_14,theme::Muted);lv_obj_set_width(cableNotice,260);lv_obj_set_style_text_align(cableNotice,LV_TEXT_ALIGN_CENTER,0);
 auto* cableBack=button(cablePanel,140,40,0,288,theme::Surface);auto* cb=label(cableBack,"Back",0,0,&font_inter_16);lv_obj_center(cb);lv_obj_add_event_cb(cableBack,[](lv_event_t*){closeCable();},LV_EVENT_CLICKED,nullptr);
 lv_obj_add_flag(cablePanel,LV_OBJ_FLAG_HIDDEN);
 modePanel=lv_obj_create(screen);lv_obj_set_size(modePanel,360,360);lv_obj_center(modePanel);
 setBgColor(modePanel,color(theme::Background),0);lv_obj_set_style_border_width(modePanel,0,0);lv_obj_set_style_pad_all(modePanel,0,0);lv_obj_remove_flag(modePanel,LV_OBJ_FLAG_SCROLLABLE);
 // Leave a clear header below the persistent battery badge. Keep the 64px
 // touch targets inside the round display and centered around the mode label.
 constexpr int modeCenterY=182,modeRadius=112;
 for(int i=0;i<MODE_COUNT;++i){
  const double angle=(-90.0+i*360.0/MODE_COUNT)*3.141592653589793/180.0;
  const int x=static_cast<int>(std::lround(modeRadius*std::cos(angle)));
  const int y=modeCenterY+static_cast<int>(std::lround(modeRadius*std::sin(angle)))-32;
  modeIcons[i]=button(modePanel,64,64,x,y,theme::Surface);lv_obj_set_style_radius(modeIcons[i],LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_pad_all(modeIcons[i],0,0);lv_obj_remove_flag(modeIcons[i],LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(modeIcons[i],iconDraw,LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  lv_obj_add_event_cb(modeIcons[i],selectMode,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 modeCenter=button(modePanel,112,112,0,modeCenterY-56,theme::SurfaceLow);lv_obj_set_style_radius(modeCenter,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(modeCenter,2,0);
 // Center the icon + 4px gap + actual label height as one group. The label's
 // center is 20px below the group's center for either one or two text lines.
 modeName=label(modeCenter,"Weight\nTraining",0,0,&font_inter_14);lv_obj_set_width(modeName,102);lv_obj_set_style_text_align(modeName,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(modeName,LV_ALIGN_CENTER,0,20);
 lv_obj_set_style_pad_all(modeCenter,8,0);
 lv_obj_add_event_cb(modeCenter,[](lv_event_t* e){lv_area_t a,text;lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)),&a);lv_obj_get_coords(modeName,&text);a.y2=text.y1-5;a.y1=a.y2-35;drawModeIcon(lv_event_get_layer(e),a,modeIds[focusedMode],color(theme::modeAccent(modeIds[focusedMode])));},LV_EVENT_DRAW_MAIN,nullptr);
 lv_obj_add_event_cb(modeCenter,selectMode,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
 lv_obj_set_ext_click_area(modeCenter,10);
 refreshModeIcons();
 lv_obj_add_flag(modePanel,LV_OBJ_FLAG_HIDDEN);
 selector=lv_obj_create(screen);lv_obj_set_size(selector,360,360);lv_obj_center(selector);setBgColor(selector,color(theme::Background),0);lv_obj_set_style_border_width(selector,0,0);lv_obj_set_style_pad_all(selector,0,0);lv_obj_remove_flag(selector,LV_OBJ_FLAG_SCROLLABLE);
 label(selector,"SELECT TRAINER",0,44,&font_inter_20);
 selectorNotice=label(selector,"Nearby trainers",0,74,&font_inter_14,theme::Muted);
 lv_obj_set_width(selectorNotice,284);lv_obj_set_style_text_align(selectorNotice,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(selectorNotice,LV_LABEL_LONG_SCROLL_CIRCULAR);
 trainerList=lv_obj_create(selector);lv_obj_set_size(trainerList,272,148);lv_obj_align(trainerList,LV_ALIGN_TOP_MID,0,98);
 lv_obj_set_style_bg_opa(trainerList,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(trainerList,0,0);lv_obj_set_style_pad_all(trainerList,0,0);
 lv_obj_set_scroll_dir(trainerList,LV_DIR_VER);lv_obj_set_scrollbar_mode(trainerList,LV_SCROLLBAR_MODE_AUTO);
 for(int i=0;i<TRAINER_ROWS;++i){
   choiceSlots[i]=-1;choices[i]=button(trainerList,248,62,0,i*70,theme::Surface);
   lv_obj_set_style_bg_color(choices[i],color(theme::Surface),LV_STATE_DISABLED);lv_obj_set_style_color_filter_opa(choices[i],LV_OPA_TRANSP,LV_STATE_DISABLED);
   choiceLabels[i]=label(choices[i],"Searching...",0,0,&font_inter_16);lv_obj_set_width(choiceLabels[i],214);lv_label_set_long_mode(choiceLabels[i],LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(choiceLabels[i],LV_TEXT_ALIGN_CENTER,0);lv_obj_align(choiceLabels[i],LV_ALIGN_CENTER,0,-11);
   choiceDetails[i]=label(choices[i],"",0,0,&font_inter_14,theme::Muted);lv_obj_align(choiceDetails[i],LV_ALIGN_CENTER,0,12);
   lv_obj_add_event_cb(choices[i],choose,LV_EVENT_ALL,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 switchController=button(selector,224,48,0,250,theme::Surface);
 setTextColor(switchController,color(theme::Text),0);setTextColor(switchController,color(theme::Muted),LV_STATE_DISABLED);
 setBgColor(switchController,color(theme::SurfaceLow),LV_STATE_DISABLED);lv_obj_set_style_color_filter_opa(switchController,LV_OPA_TRANSP,LV_STATE_DISABLED);
 switchControllerText=label(switchController,"Create Twin",0,0,&font_inter_16);lv_obj_remove_local_style_prop(switchControllerText,LV_STYLE_TEXT_COLOR,0);lv_obj_center(switchControllerText);lv_obj_add_flag(switchController,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_event_cb(switchController,[](lv_event_t*){
   const auto s=vlink::voltraLink.state();const uint32_t now=millis();
   if(!pending()&&!loadSentMs&&vlink::canChangeTwin(s,now,!s.endpoint.paired()))
     twinRequestRejectedUntil=vlink::voltraLink.requestTwin(!s.endpoint.paired())?0:now+3000;
 },LV_EVENT_CLICKED,nullptr);
 selectorCue=label(selector,"Swipe right for connections",0,310,&font_inter_12,theme::Muted);
 connectionTools=lv_obj_create(selector);lv_obj_set_size(connectionTools,360,360);lv_obj_center(connectionTools);
 setBgColor(connectionTools,color(theme::Background),0);lv_obj_set_style_border_width(connectionTools,0,0);lv_obj_set_style_pad_all(connectionTools,0,0);lv_obj_remove_flag(connectionTools,LV_OBJ_FLAG_SCROLLABLE);setHidden(connectionTools,true);
 label(connectionTools,"CONNECTIONS",0,51,&font_inter_20);
 label(connectionTools,"Swipe left to return",0,81,&font_inter_14,theme::Muted);
 tvButton=connectionButton(connectionTools,"TV link",ConnectionIcon::Tv,108);
 manageButton=connectionButton(connectionTools,"Manage devices",ConnectionIcon::Devices,174);
 discoverButton=connectionButton(connectionTools,"Find nearby trainers",ConnectionIcon::Search,240);auto* discover=discoverButton;
 lv_obj_add_event_cb(discover,[](lv_event_t*){tvbridge::findTrainers();},LV_EVENT_CLICKED,nullptr);
 tvPanel=lv_obj_create(lv_layer_top());lv_obj_set_size(tvPanel,360,360);lv_obj_center(tvPanel);
 setBgColor(tvPanel,color(theme::Background),0);lv_obj_set_style_border_width(tvPanel,0,0);lv_obj_set_style_pad_all(tvPanel,0,0);
 lv_obj_remove_flag(tvPanel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(tvPanel,LV_OBJ_FLAG_HIDDEN);
 label(tvPanel,"TV BLUETOOTH",0,55,&font_inter_20);
 tvStatus=label(tvPanel,"Pair with your TV",0,103,&font_inter_14,theme::Muted);
 tvPin=label(tvPanel,"",0,138,&font_inter_48,theme::Text);
 tvPairButton=button(tvPanel,224,52,0,207,theme::Surface);
 tvButtonText=label(tvPairButton,"Pair TV",0,0,&font_inter_16);lv_obj_center(tvButtonText);
 lv_obj_add_event_cb(tvPairButton,[](lv_event_t*){if(tvbridge::connected()||tvbridge::pairing())tvbridge::disconnect();else tvbridge::openPairing();},LV_EVENT_CLICKED,nullptr);
 tvBackButton=button(tvPanel,164,48,0,271,theme::SurfaceLow);auto* back=tvBackButton;auto* backText=label(back,"Back",0,0,&font_inter_16);lv_obj_center(backText);
 lv_obj_add_event_cb(back,[](lv_event_t*){tvPage=false;lv_obj_add_flag(tvPanel,LV_OBJ_FLAG_HIDDEN);},LV_EVENT_CLICKED,nullptr);
 tvFindButton=button(tvPanel,224,48,0,146,theme::SurfaceLow);
 auto* findText=label(tvFindButton,"Find trainers",0,0,&font_inter_16);lv_obj_center(findText);
 lv_obj_add_event_cb(tvFindButton,[](lv_event_t*){tvbridge::findTrainers();},LV_EVENT_CLICKED,nullptr);
 lv_obj_add_flag(tvFindButton,LV_OBJ_FLAG_HIDDEN);
 tvForgetButton=button(tvPanel,224,48,0,217,theme::SurfaceLow);
 auto* forgetTvText=label(tvForgetButton,"Forget TVs",0,0,&font_inter_16);lv_obj_center(forgetTvText);
 lv_obj_add_event_cb(tvForgetButton,[](lv_event_t*){openForget(ForgetTarget::Tvs);},LV_EVENT_CLICKED,nullptr);
 lv_obj_add_flag(tvForgetButton,LV_OBJ_FLAG_HIDDEN);
 managePanel=lv_obj_create(lv_layer_top());lv_obj_set_size(managePanel,360,360);lv_obj_center(managePanel);
 setBgColor(managePanel,color(theme::Background),0);lv_obj_set_style_border_width(managePanel,0,0);lv_obj_set_style_pad_all(managePanel,0,0);
 lv_obj_remove_flag(managePanel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(managePanel,LV_OBJ_FLAG_HIDDEN);
 label(managePanel,"SAVED TRAINERS",0,50,&font_inter_20);
 label(managePanel,"Choose a device to forget",0,80,&font_inter_14,theme::Muted);
 for(int i=0;i<2;++i){
   manageRows[i]=button(managePanel,248,64,0,111+i*74,theme::Surface);
   manageNames[i]=label(manageRows[i],"Empty",0,0,&font_inter_16);lv_obj_set_width(manageNames[i],218);lv_label_set_long_mode(manageNames[i],LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(manageNames[i],LV_TEXT_ALIGN_CENTER,0);lv_obj_center(manageNames[i]);
   lv_obj_add_event_cb(manageRows[i],[](lv_event_t* e){openForget(ForgetTarget::Trainer,static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 auto* manageBack=button(managePanel,164,48,0,270,theme::SurfaceLow);auto* manageBackText=label(manageBack,"Back",0,0,&font_inter_16);lv_obj_center(manageBackText);
 lv_obj_add_event_cb(manageBack,[](lv_event_t*){managingTrainers=false;setHidden(managePanel,true);},LV_EVENT_CLICKED,nullptr);
 lv_obj_add_event_cb(manageButton,[](lv_event_t*){if(tvCanEdit()){managingTrainers=true;setHidden(managePanel,false);}},LV_EVENT_CLICKED,nullptr);
 forgetPanel=lv_obj_create(lv_layer_top());lv_obj_set_size(forgetPanel,360,360);lv_obj_center(forgetPanel);
 setBgColor(forgetPanel,color(theme::Background),0);lv_obj_set_style_border_width(forgetPanel,0,0);lv_obj_set_style_pad_all(forgetPanel,0,0);
 lv_obj_remove_flag(forgetPanel,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(forgetPanel,LV_OBJ_FLAG_HIDDEN);
 forgetTitle=label(forgetPanel,"Forget device?",0,54,&font_inter_20);
 forgetDescription=label(forgetPanel,"",0,94,&font_inter_14,theme::Muted);lv_obj_set_width(forgetDescription,264);lv_obj_set_style_text_align(forgetDescription,LV_TEXT_ALIGN_CENTER,0);
 forgetStatus=label(forgetPanel,"",0,168,&font_inter_14,theme::Muted);
 forgetConfirm=button(forgetPanel,224,52,0,207,theme::Surface);
 auto* forgetText=label(forgetConfirm,"Forget",0,0,&font_inter_16);lv_obj_center(forgetText);
 lv_obj_add_event_cb(forgetConfirm,confirmForget,LV_EVENT_CLICKED,nullptr);
 auto* cancelForget=button(forgetPanel,164,48,0,271,theme::SurfaceLow);auto* cancelText=label(cancelForget,"Cancel",0,0,&font_inter_16);lv_obj_center(cancelText);
 lv_obj_add_event_cb(cancelForget,[](lv_event_t*){forgetTarget=ForgetTarget::None;setHidden(forgetPanel,true);},LV_EVENT_CLICKED,nullptr);
 lv_obj_add_event_cb(tvButton,[](lv_event_t*){if(tvCanEdit()){tvPage=true;lv_obj_remove_flag(tvPanel,LV_OBJ_FLAG_HIDDEN);}},LV_EVENT_CLICKED,nullptr);
 // Non-blocking startup fade. BLE discovery proceeds underneath; controls stay covered.
 auto* splash=lv_obj_create(lv_layer_top());lv_obj_set_size(splash,360,360);lv_obj_center(splash);
 setBgColor(splash,color(theme::Background),0);lv_obj_set_style_border_width(splash,0,0);
 lv_obj_set_style_radius(splash,0,0);lv_obj_remove_flag(splash,LV_OBJ_FLAG_SCROLLABLE);
 auto* logo=lv_image_create(splash);lv_image_set_src(logo,&polar_bear_logo);lv_obj_center(logo);
 lv_obj_set_style_image_recolor(logo,color(theme::Text),0);lv_obj_set_style_image_recolor_opa(logo,LV_OPA_COVER,0);
 lv_obj_set_style_opa(logo,LV_OPA_TRANSP,0);
 lv_anim_t fade;lv_anim_init(&fade);lv_anim_set_var(&fade,logo);lv_anim_set_values(&fade,0,255);lv_anim_set_duration(&fade,900);
 lv_anim_set_exec_cb(&fade,[](void* object,int32_t opacity){lv_obj_set_style_opa(static_cast<lv_obj_t*>(object),opacity,0);});
 lv_anim_set_path_cb(&fade,lv_anim_path_ease_in_out);lv_anim_start(&fade);
 lv_anim_t reveal;lv_anim_init(&reveal);lv_anim_set_var(&reveal,splash);lv_anim_set_values(&reveal,255,0);lv_anim_set_delay(&reveal,1500);lv_anim_set_duration(&reveal,350);
 lv_anim_set_exec_cb(&reveal,[](void* object,int32_t opacity){lv_obj_set_style_opa(static_cast<lv_obj_t*>(object),opacity,0);});
 lv_anim_set_completed_cb(&reveal,[](lv_anim_t* a){splashActive=false;lv_obj_delete(static_cast<lv_obj_t*>(a->var));});lv_anim_start(&reveal);
 // Top layer keeps the knob battery visible above every page and the splash.
 auto* battery=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(battery);
 lv_obj_set_size(battery,66,18);lv_obj_align(battery,LV_ALIGN_TOP_MID,0,8);
 lv_obj_remove_flag(battery,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(battery,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_bg_color(battery,color(theme::Background),0);lv_obj_set_style_bg_opa(battery,LV_OPA_COVER,0);lv_obj_set_style_radius(battery,5,0);
 auto* batteryText=label(battery,board::usbPowerConnected()?"":"--%",0,0,&font_inter_12,theme::Muted);
 lv_obj_align(batteryText,LV_ALIGN_RIGHT_MID,-3,0);
 lv_obj_add_event_cb(battery,[](lv_event_t* e){
   lv_area_t a;lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)),&a);
   const bool usb=board::usbPowerConnected();
   const int pct=board::batteryPercent();const auto ink=color(!usb&&pct>=0&&pct<=15?0xFF6B60:theme::Muted);
   const int x=a.x1+(usb?21:3);
   lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_TRANSP;d.border_color=ink;d.border_width=1;d.radius=2;
   lv_area_t body={x,a.y1+3,x+21,a.y1+15};lv_draw_rect(lv_event_get_layer(e),&d,&body);
   d.border_width=0;d.bg_color=ink;d.bg_opa=LV_OPA_COVER;d.radius=0;
   lv_area_t cap={x+22,a.y1+7,x+23,a.y1+11};lv_draw_rect(lv_event_get_layer(e),&d,&cap);
   if(usb){
     // A small lightning bolt inside the silhouette replaces the unreliable
     // percentage while USB supplies power; it is not a charge-complete claim.
     lv_draw_line_dsc_t bolt;lv_draw_line_dsc_init(&bolt);bolt.color=ink;bolt.width=2;
     bolt.p1={lv_value_precise_t(x+12),lv_value_precise_t(a.y1+5)};bolt.p2={lv_value_precise_t(x+8),lv_value_precise_t(a.y1+9)};lv_draw_line(lv_event_get_layer(e),&bolt);
     bolt.p1=bolt.p2;bolt.p2={lv_value_precise_t(x+13),lv_value_precise_t(a.y1+9)};lv_draw_line(lv_event_get_layer(e),&bolt);
     bolt.p1=bolt.p2;bolt.p2={lv_value_precise_t(x+9),lv_value_precise_t(a.y1+13)};lv_draw_line(lv_event_get_layer(e),&bolt);
   }else if(pct>0){lv_area_t fill={x+2,a.y1+5,x+2+(16*pct/100),a.y1+13};lv_draw_rect(lv_event_get_layer(e),&d,&fill);}
 },LV_EVENT_DRAW_MAIN,nullptr);
 lv_timer_create([](lv_timer_t* t){
   auto* text=static_cast<lv_obj_t*>(lv_timer_get_user_data(t));const int pct=board::batteryPercent();
   char value[8];if(board::usbPowerConnected())value[0]=0;else if(pct<0)snprintf(value,sizeof(value),"--%%");else snprintf(value,sizeof(value),"%d%%",pct);
   if(strcmp(lv_label_get_text(text),value)){lv_label_set_text(text,value);lv_obj_invalidate(lv_obj_get_parent(text));}
 },1000,batteryText);
}
bool selectingTrainer(){return choosing;}
bool tvCanEdit(){return !pending()&&!loadSentMs&&!choosingCable&&!choosingMode&&!splashActive&&!managingTrainers&&forgetTarget==ForgetTarget::None;}
bool voiceAction(const voice::Event& event){
 static uint32_t releaseAt=0;static bool released=false;
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();
 const bool controlPage=!choosing&&!choosingMode&&!choosingCable&&!splashActive;
 if(!voice::canAct(event,s,now,controlPage,pending(),loadSentMs!=0,released&&now-releaseAt<1500))return false;
 loadGesture=false;unloadGesture=false;
 if(event.kind==voice::EventKind::Release){
  if(!vlink::voltraLink.requestUnload(event.session,vlink::UnloadSource::Voice))return false;
  loadSentMs=0;released=true;releaseAt=now;return true;
 }
 if(!vlink::voltraLink.requestLoad(event.session))return false;
 loadSentMs=now;return true;
}
void loop(){
 if(forgetTarget!=ForgetTarget::None){
   board::RotateEvent ignored;while(board::pollRotate(ignored)){}
   const auto s=vlink::voltraLink.state();
   const bool allowed=(forgetTarget==ForgetTarget::Tvs?vlink::canSwitch(s,millis())&&!s.loaded&&!s.armed&&!s.parameterSelectionPending:vlink::canForgetTrainer(s,forgetSlot,millis()))&&!pending()&&!loadSentMs;
   if(allowed)lv_obj_remove_state(forgetConfirm,LV_STATE_DISABLED);else lv_obj_add_state(forgetConfirm,LV_STATE_DISABLED);
   if(!allowed)setText(forgetStatus,"Unload / wait before forgetting");
   else if(!strcmp(lv_label_get_text(forgetStatus),"Unload / wait before forgetting"))setText(forgetStatus,"");
   return;
 }
 if(managingTrainers){
   board::RotateEvent ignored;while(board::pollRotate(ignored)){}
   const auto s=vlink::voltraLink.state();
   for(int i=0;i<2;++i){
     setText(manageNames[i],s.trainers[i].address[0]?(s.trainers[i].name[0]?s.trainers[i].name:"VOLTRA"):"No saved trainer");
     if(s.trainers[i].address[0])lv_obj_remove_state(manageRows[i],LV_STATE_DISABLED);else lv_obj_add_state(manageRows[i],LV_STATE_DISABLED);
   }
   return;
 }
 if(tvPage){
   board::RotateEvent ignored;while(board::pollRotate(ignored)){}
   const int pin=tvbridge::passkey();char text[16]{};if(pin>=0)snprintf(text,sizeof(text),"%06d",pin);
   const bool canPair=vlink::canSwitch(vlink::voltraLink.state(),millis());
   const bool canAct=tvbridge::connected()||tvbridge::pairing()||canPair;
   if(canAct)lv_obj_remove_state(tvPairButton,LV_STATE_DISABLED);else lv_obj_add_state(tvPairButton,LV_STATE_DISABLED);
   const bool trusted=tvbridge::trusted();
   const bool compact=trusted&&!tvbridge::pairing();
   setHidden(tvFindButton,tvbridge::connected()||tvbridge::pairing()||!canPair);
   setHidden(tvForgetButton,!compact);
   lv_obj_align(tvStatus,LV_ALIGN_TOP_MID,0,compact?86:103);
   lv_obj_set_height(tvPairButton,compact?48:52);lv_obj_align(tvPairButton,LV_ALIGN_TOP_MID,0,compact?107:207);
   lv_obj_align(tvFindButton,LV_ALIGN_TOP_MID,0,compact?162:146);
   lv_obj_align(tvBackButton,LV_ALIGN_TOP_MID,0,compact?277:271);
   setText(tvPin,text);setText(tvStatus,tvbridge::connected()?"Connected to TV":pin>=0?"Enter this code on your TV":tvbridge::pairing()?"Find Voltra Remote on TV":tvbridge::findingTrainers()?"Finding trainers for 30 seconds":trusted?"Ready for trusted TV":!canPair?"Unload VOLTRA to pair":"Pair with your TV");
   setText(tvButtonText,tvbridge::connected()?"Disconnect TV":tvbridge::pairing()?"Cancel pairing":trusted?"Pair new TV":"Pair TV");
   return;
 }
 vlink::voltraLink.setDiscoveryVisible(choosing);
 if(pendingSwipe){const int direction=pendingSwipe;pendingSwipe=0;navigateSwipe(direction);}
 if(toolsPage){
   board::RotateEvent ignored;while(board::pollRotate(ignored)){}
   if(!tvbridge::connected()&&!tvbridge::pairing()&&vlink::canSwitch(vlink::voltraLink.state(),millis()))lv_obj_remove_state(discoverButton,LV_STATE_DISABLED);else lv_obj_add_state(discoverButton,LV_STATE_DISABLED);
   return;
 }
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();
 if(s.session!=session){choosingController=false;twinRequestRejectedUntil=0;initialModeHandled=false;closeModes();session=s.session;dialPulseMs=0;choosingCable=false;cableRequested=false;lv_obj_add_flag(cablePanel,LV_OBJ_FLAG_HIDDEN);for(auto& e:edits)e.reset();bandEdit.reset();damperEdit.reset();resetIso();loadGesture=false;loadSentMs=0;}
 const bool live=vlink::fresh(s,now);
 if(bandSessionMode!=s.mode){bandSessionMode=s.mode;bandEdit.reset();damperEdit.reset();resetIso();}
 if(live&&s.mode==2&&s.modeValueMs[0]&&now-s.modeValueMs[0]<8000)bandEdit.observe(s.modeValues[0]);
 if(choosingMode&&(s.loaded||s.armed||loadSentMs))closeModes();
 const bool mappedState=mappedMode(s.mode)&&(s.fitnessMode==4||voltra::fitnessModeLoaded(s.fitnessMode)||s.armed);
 if(fallbackModeMenu&&live&&mappedState)closeModes();
 if(!choosing&&!splashActive&&live&&!mappedState&&!choosingCable&&!choosingMode){openModes(true);fallbackModeMenu=choosingMode;}
 const int shownMode=live?s.mode:0;if(displayedMode!=shownMode){displayedMode=shownMode;lv_obj_invalidate(currentModeIcon);}
 lv_obj_add_flag(currentModeIcon,LV_OBJ_FLAG_HIDDEN);
 if(s.link==vlink::LinkState::Ready&&!s.parameterSelectionPending){
   if(s.weightLbs>=0){const int before=edits[0].target;edits[0].observe(s.weightLbs);
     if(before!=edits[0].target)board::traceDial("readback",0,0,before,edits[0].target,s.weightLbs,edits[0].pending,edits[0].sent,now);
   }
   if(vlink::chainWeight(s,false)>=0)edits[1].observe(vlink::chainWeight(s,false));
   if(vlink::chainWeight(s,true)>=0)edits[3].observe(vlink::chainWeight(s,true));
   if(s.eccentricLbs>=-195)edits[2].observe(s.eccentricLbs);
 }
 if(live&&s.mode==4&&s.modeValueMs[5]&&now-s.modeValueMs[5]<5000&&s.modeValues[5]>=0&&s.modeValues[5]<=9)damperEdit.observe(s.modeValues[5]);
 for(int i=7;i<=11;++i)if(live&&s.mode==7&&s.modeValueMs[i]&&now-s.modeValueMs[i]<8000&&voltra::validIsokineticSetting(i,s.modeValues[i]))isoEdits[i-7].observe(s.modeValues[i]);
 const bool allowed=!s.parameterSelectionPending&&vlink::canEditParameter(s,now,static_cast<vlink::Parameter>(page))&&!loadSentMs;
 board::RotateEvent ev;
 while(board::pollRotate(ev))if(!choosing&&!splashActive){
  if(choosingCable)continue;
  if(choosingMode){focusedMode=(focusedMode+(ev.direction>0?1:MODE_COUNT-1))%MODE_COUNT;refreshModeIcons();}
  else if(s.mode==7){
   const int index=isoIndex(s,isoPage);auto& e=isoEdits[index-7];
   if(vlink::canEditIsokinetic(s,now,index)&&!loadSentMs&&e.known&&!e.sent&&(!s.controlBusyUntil||static_cast<int32_t>(now-s.controlBusyUntil)>=0)){
    const int before=e.target;
    if(index==8)e.target=ev.direction>0?1:0;
    else if(index==7||index==9)e.target=std::max(index==9?0:100,std::min(2000,e.target+ev.direction*100));
    else if(s.weightUnitRaw==1)e.target=remote::kgStep(e.target,ev.direction,5,index==10?100:200);
    else e.target=std::max(5,std::min(index==10?100:200,e.target+ev.direction));
    e.pending=e.target!=e.confirmed;e.changedMs=ev.ms;e.timedOut=false;if(before!=e.target)dialPulseMs=now;
   }
  }
  else if(s.mode==4){
   if(vlink::canEditDamper(s,now)&&damperEdit.known&&!damperEdit.sent){
    const int before=damperEdit.target;damperEdit.target=std::max(0,std::min(9,damperEdit.target+ev.direction));
    damperEdit.pending=damperEdit.target!=damperEdit.confirmed;damperEdit.changedMs=ev.ms;damperEdit.timedOut=false;
    if(before!=damperEdit.target)dialPulseMs=now;
   }
  }
  else if(s.mode==2){
   if(vlink::canEditBand(s,now)&&bandEdit.known&&!bandEdit.sent){
    const int before=bandEdit.target;
    if(s.weightUnitRaw==1)bandEdit.turn(ev.direction,true,false,ev.ms,15,200);
    else{bandEdit.target=std::max(15,std::min(200,bandEdit.target+ev.direction));bandEdit.pending=bandEdit.target!=bandEdit.confirmed;bandEdit.changedMs=ev.ms;bandEdit.timedOut=false;}
    if(before!=bandEdit.target)dialPulseMs=now;
   }
  }
  else {const int before=edits[page].target;edits[page].turn(ev.direction,allowed,false,ev.ms,page==0?5:page==2?-195:0,page==0?200:page==2?195:100);if(edits[page].target!=before)dialPulseMs=now;
   board::traceDial(allowed?"input":"input-blocked",page,ev.direction,before,edits[page].target,edits[page].confirmed,edits[page].pending,edits[page].sent,ev.ms);
  }
 }
 if(!vlink::canEditBand(s,now)&&bandEdit.pending)bandEdit.cancel();
 if(bandEdit.due(now)){if(vlink::voltraLink.requestBandMaximum(bandEdit.target))bandEdit.submitted(now);else bandEdit.cancel();}
 bandEdit.tick(now);
 if(!vlink::canEditDamper(s,now)&&damperEdit.pending)damperEdit.cancel();
 if(damperEdit.due(now)){if(vlink::voltraLink.requestDamperLevel(damperEdit.target))damperEdit.submitted(now);else damperEdit.cancel();}
 damperEdit.tick(now);
 for(int i=7;i<=11;++i){auto& e=isoEdits[i-7];if((!vlink::canEditIsokinetic(s,now,i)||loadSentMs)&&e.pending)e.cancel();if(e.due(now)){if(vlink::voltraLink.requestIsokinetic(i,e.target))e.submitted(now);else e.cancel();}e.tick(now);}
 for(int i=0;i<4;++i){auto& e=edits[i];
  if((!vlink::canEditParameter(s,now,static_cast<vlink::Parameter>(i))||loadSentMs)&&e.pending){board::traceDial("cancel-state",i,0,e.target,e.target,e.confirmed,e.pending,e.sent,now);e.cancel();}
  if(e.due(now)){if(vlink::voltraLink.requestSetting(static_cast<vlink::Parameter>(i),e.target)){e.submitted(now);board::traceDial("submit",i,0,e.target,e.target,e.confirmed,e.pending,e.sent,now);}
   else{board::traceDial("not-queued",i,0,e.target,e.target,e.confirmed,e.pending,e.sent,now);e.cancel();}}
  const int before=e.target;e.tick(now);if(before!=e.target)board::traceDial("timeout",i,0,before,e.target,e.confirmed,e.pending,e.sent,now);
 }
 if(loadSentMs && ((live&&(s.loaded||s.armed))||now-loadSentMs>5000))loadSentMs=0;
 static int lastView=-1;const int view=(splashActive?1:0)|(choosing?2:0)|(choosingMode?4:0)|(choosingCable?8:0);
 if(view!=lastView){lastView=view;repaintView=true;}
 if(now-lastRender<20)return;lastRender=now;
 const uint32_t accent=theme::modeAccent(live?s.mode:0);
 const uint32_t tint=theme::modeTint(accent);
 const uint32_t ledColor=(s.loaded||s.armed||loadSentMs)?accent:theme::dimColor(accent);
 board::setRingColor(!splashActive&&live&&s.mode>0&&s.mode!=3&&theme::modeAccent(s.mode)!=theme::Muted?ledColor:0);
 static uint32_t lastTickColor=0xffffffff;
 const bool tickColorChanged=lastTickColor!=accent;
 if(tickColorChanged){lastTickColor=accent;lv_style_set_line_color(&filledTickStyle,color(accent));}
 char text[100];
 if(choosingCable){
  const bool cableLive=live&&s.cableMs&&now-s.cableMs<3500;
  const bool knownUnit=s.lengthUnitRaw==0||s.lengthUnitRaw==1;
  if(!cableLive||!knownUnit)snprintf(text,sizeof(text),"--");
  else if(s.lengthUnitRaw==0)snprintf(text,sizeof(text),"%d",s.cableCm);
  else snprintf(text,sizeof(text),"%.1f",s.cableCm/2.54);
  setText(cableNumber,text);setText(cableUnit,knownUnit?(s.lengthUnitRaw==0?"cm":"in"):"");
  if(cableLive&&cableInitial>=0&&abs(s.cableCm-cableInitial)>=2)cableMoved=true;
  if(cableRequested&&s.cableActive&&cableHomeScreen>=0&&s.cableScreenId>=0&&s.cableScreenId!=cableHomeScreen)cableScreenEntered=true;
  const bool saved=cableLive&&cableRequested&&s.cableActive&&now-cableStarted>1000&&
    ((cableBaseline>=0&&s.cableOffsetCm!=cableBaseline)||(cableScreenEntered&&s.cableScreenId==cableHomeScreen));
  if(saved)closeCable();
  setText(cableNotice,!live?"Trainer disconnected":!cableRequested?(s.loaded||s.armed?"Unload, then reopen this page":"Return and try again"):!s.cableActive?"Opening on Voltra...":!knownUnit?"Reading device units...":"Pull cable to desired length\nHold still for Voltra to save");
 }

 if(s.selected>=0){const auto& selected=s.trainers[s.selected];snprintf(text,sizeof(text),"%s",selected.name[0]?selected.name:selected.address+9);setText(trainerLabel,text);}else setText(trainerLabel,"Choose Voltra  v");
 if(!vlink::twinActive(s,now))choosingController=false;
 const bool twinActionVisible=showTwinAction(s,now);
 setHidden(switchController,!twinActionVisible);setText(switchControllerText,s.twinPending?"Updating...":s.endpoint.paired()?"Exit Twin":"Create Twin");
 lv_obj_set_height(trainerList,twinActionVisible?144:200);
 if(tvCanEdit())lv_obj_remove_state(tvButton,LV_STATE_DISABLED);else lv_obj_add_state(tvButton,LV_STATE_DISABLED);
 if(vlink::canChangeTwin(s,now,!s.endpoint.paired())&&!pending()&&!loadSentMs)lv_obj_remove_state(switchController,LV_STATE_DISABLED);else lv_obj_add_state(switchController,LV_STATE_DISABLED);
 setText(selectorNotice,twinNotice(s,now,twinActionVisible));
 int row=0;
 auto renderTrainer=[&](const vlink::Trainer& trainer,int slot){
   const bool saved=slot>=0,paired=saved&&pairedChoice(s,slot,now);
   const bool available=saved?vlink::trainerAvailable(s,slot,now):vlink::nearbyAdvertisement(trainer,now);
   choiceSlots[row]=slot;strlcpy(choiceAddresses[row],trainer.address,sizeof(choiceAddresses[row]));
   setText(choiceLabels[row],trainer.name[0]?trainer.name:"Unnamed VOLTRA");
   const char* subtitle=paired?"Paired":saved&&s.selected==slot&&vlink::twinActive(s,now)?"Control":
     saved&&s.selected==slot&&(s.link==vlink::LinkState::Connecting||s.link==vlink::LinkState::Handshaking)?"Connecting...":
     saved&&s.selected==slot&&s.link==vlink::LinkState::Ready?"Connected":
     !saved?"New · Nearby":vlink::trainerNearby(s,slot,now)?"Saved · Nearby":available?"Saved · Reconnect":"Saved · Not detected";
   setText(choiceDetails[row],subtitle);
   const bool disabled=paired||!available||((!saved||s.selected!=slot)&&(!vlink::canSwitch(s,now)||pending()||loadSentMs));
   if(disabled)lv_obj_add_state(choices[row],LV_STATE_DISABLED);else lv_obj_remove_state(choices[row],LV_STATE_DISABLED);
   setBgColor(choices[row],color(saved&&s.selected==slot?tint:theme::Surface),0);
   setTextColor(choiceLabels[row],color(disabled?theme::Muted:saved&&s.selected==slot?accent:theme::Text),0);
   lv_obj_align(choices[row],LV_ALIGN_TOP_MID,0,row*70);setHidden(choices[row],false);++row;
 };
 for(int slot=0;slot<2;++slot)if(s.trainers[slot].address[0])renderTrainer(s.trainers[slot],slot);
 for(const auto& nearby:s.nearby){
   if(!vlink::nearbyAdvertisement(nearby,now))continue;
   bool saved=false;for(const auto& trainer:s.trainers)if(!strcasecmp(trainer.address,nearby.address))saved=true;
   if(!saved)renderTrainer(nearby,-1);
 }
 if(!row){choiceSlots[0]=-1;choiceAddresses[0][0]=0;setText(choiceLabels[0],"No trainers nearby");setText(choiceDetails[0],s.discoveryPausedForTv?"Open connections to find trainers":"Searching...");lv_obj_add_state(choices[0],LV_STATE_DISABLED);setHidden(choices[0],false);row=1;}
 for(;row<TRAINER_ROWS;++row)setHidden(choices[row],true);
 const char* state=s.link==vlink::LinkState::Scanning?"SEARCHING":s.link==vlink::LinkState::Connecting?"CONNECTING":s.link==vlink::LinkState::Handshaking?"ACCEPT ON VOLTRA":!live?"STATE UNAVAILABLE":s.loaded?(s.mode==3?"RUNNING":"LOADED"):s.armed?"AUTO LOAD ACTIVE":s.mode==0?"IDLE":s.fitnessMode==4?"UNLOADED":"NOT READY";
 setText(status,state);setTextColor(status,color(live&&(s.loaded||s.armed)?accent:theme::Muted),0);
 const bool band=live&&s.mode==2,damper=live&&s.mode==4,iso=live&&s.mode==7;
 auto& edit=edits[page];
 // Settings stay visible at rest and while editing. Only fresh moving-cable force
 // replaces the primary readout; modifier pages keep their own value and units.
 const bool primaryPage=band||damper||(iso?isoPage==0:page==0);
 const auto readout=remote::trainingReadout(s,now,primaryPage,pending()||loadSentMs);
 const bool readoutPounds=s.mode!=1&&s.weightUnitRaw==0;
 const char* readoutUnit=readoutPounds?"lbs":"kg";
 auto formatLiveWeight=[&]{snprintf(text,sizeof(text),"%.1f",readoutPounds?
   readout.forceTenthsLb/10.0:remote::telemetryKilograms(readout.forceTenthsLb));};
 bool numberChanged=false;
 if(!band&&!damper&&!iso){
 if(readout.liveWeight)formatLiveWeight();
 else if(!live||!edit.known||s.mode!=1||(s.twinConnection==2&&!vlink::twinActive(s,now)))snprintf(text,sizeof(text),"--");else snprintf(text,sizeof(text),"%.1f",remote::displayWeightKilograms(edit.target,vlink::twinActive(s,now)));
 const bool dialing=dialPulseMs&&now-dialPulseMs<500;
 const auto* numberFont=dialing?&font_inter_80:&font_inter_72;
 numberChanged=strcmp(lv_label_get_text(number),text)||lv_obj_get_style_text_font(number,0)!=numberFont;
 setFont(number,numberFont,0);
 if(numberChanged)lv_obj_align(number,LV_ALIGN_TOP_MID,0,dialing?100:106);
 setText(number,text);setText(unit,"kg");
 }
 const bool showTwin=vlink::twinActive(s,now);
 if(showTwin==lv_obj_has_flag(twinIcon,LV_OBJ_FLAG_HIDDEN)){if(showTwin)lv_obj_remove_flag(twinIcon,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(twinIcon,LV_OBJ_FLAG_HIDDEN);}
 static int lastTwin=-1;const int twinLayout=(showTwin?1:0)|(damper?2:band?4:iso?8:0);if(lastTwin!=twinLayout){lastTwin=twinLayout;lv_obj_align(unit,LV_ALIGN_TOP_MID,showTwin?-15:0,band?190:damper?222:181);lv_obj_align(twinIcon,LV_ALIGN_TOP_MID,band?48:damper?48:iso?42:17,band?191:damper?224:183);}

 static int lastTickValue=-1;const int tickValue=s.mode==3?0:iso?(isoEdits[0].known?isoEdits[0].target/10:0):damper?(damperEdit.known?(damperEdit.target+1)*20:0):band?(s.modeValueMs[0]&&now-s.modeValueMs[0]<8000?s.modeValues[0]:0):(edit.known?(page==2?abs(edit.target):edit.target):0);
 if(tickColorChanged||tickValue!=lastTickValue){lastTickValue=tickValue;lv_scale_section_set_range(filledTicks,0,tickValue);lv_obj_invalidate(arc);}
 static int lastTabPage=-1;static uint32_t lastTabAccent=0xffffffff;
 const bool tabIconsChanged=lastTabPage!=page||lastTabAccent!=accent;lastTabPage=page;lastTabAccent=accent;
 if(!iso)for(int i=0;i<4;++i){
  setBgColor(tabs[i],color(i==page?theme::Selected:theme::Surface),0);
  setTextColor(tabValues[i],color(i==page?accent:theme::Text),0);
  const auto& value=edits[i];
  if(!live||!value.known||s.mode!=1||(s.twinConnection==2&&!vlink::twinActive(s,now)))snprintf(text,sizeof(text),"--");
  
  else snprintf(text,sizeof(text),"%.1f",remote::displayWeightKilograms(value.target,vlink::twinActive(s,now)));
  setText(tabValues[i],text);if(tabIconsChanged)lv_obj_invalidate(tabs[i]);
 }
 const bool stopping=s.loaded||s.armed||loadSentMs||(!live&&s.link==vlink::LinkState::Ready);
 const bool loadReady=vlink::canLoad(s,now)&&!pending();
 const uint32_t nextTwinInk=band?theme::Background:stopping?accent:theme::dimColor(accent);
 const uint32_t nextTwinCutout=band?(stopping?accent:theme::dimColor(accent)):theme::Background;
 if(twinCutout!=nextTwinCutout){twinCutout=nextTwinCutout;lv_obj_invalidate(twinIcon);}
 if(twinInk!=nextTwinInk){twinInk=nextTwinInk;lv_obj_invalidate(twinIcon);}
 if(!band){setTextColor(number,color(accent),0);
 setTextOpacity(number,stopping?LV_OPA_COVER:LV_OPA_50,0);}
 if(s.parameterSelectionPending)snprintf(text,sizeof(text),"Reading setting from Voltra...");
 else if(edit.timedOut)snprintf(text,sizeof(text),"Not confirmed - try again");
 else if(pending())snprintf(text,sizeof(text),"Waiting for trainer confirmation");
 else if(live&&s.mode==0)snprintf(text,sizeof(text),"Swipe left to choose a mode");
 else if(live&&s.mode!=1)snprintf(text,sizeof(text),"Configure this mode on Voltra");
 else if(live&&s.mode==1&&page==0)snprintf(text,sizeof(text),stopping?"Tap weight to unload":loadReady?"Tap weight to load":"Reading settings...");
 else if(live&&s.loaded&&page!=0)snprintf(text,sizeof(text),"Unload to edit this setting");
 else if(s.link==vlink::LinkState::Ready){if(s.battery>=0)snprintf(text,sizeof(text),"Battery %d%%   |   Reps %d",s.battery,s.reps);else snprintf(text,sizeof(text),"Reps %d",s.reps);}
 else snprintf(text,sizeof(text),"%s",s.message);
 if(!band&&!damper&&!iso)setText(details,text);
 // Band maximum is already the displayed setting: observed 106lb -> 48kg,
 // even with connection=2. Do not apply Weight Training Twin multiplication.
 static int lastBand=-1;const int numberLayout=band?2:damper?4:iso?7:1;
 if(lastBand!=numberLayout){
   lastBand=numberLayout;
   lv_obj_set_style_radius(number,band?LV_RADIUS_CIRCLE:0,0);
   lv_obj_set_style_pad_top(number,band?23:0,0);
   lv_obj_set_style_text_align(number,LV_TEXT_ALIGN_CENTER,0);
   lv_obj_set_size(number,band?154:LV_SIZE_CONTENT,band?154:LV_SIZE_CONTENT);
   lv_obj_set_width(unit,band?100:damper?90:iso?74:26);
   if(!band){lv_obj_align(unit,LV_ALIGN_TOP_MID,showTwin?-15:0,damper?222:181);lv_obj_align(number,damper?LV_ALIGN_CENTER:LV_ALIGN_TOP_MID,0,damper?0:106);}
   else{lv_obj_align(number,LV_ALIGN_TOP_MID,0,94);lv_obj_align(unit,LV_ALIGN_TOP_MID,showTwin?-15:0,190);}
 }
 setHidden(bandDetails,!band);setHidden(damperRoman,!damper||readout.liveWeight);for(auto* tab:tabs)setHidden(tab,band||damper);
 if(band){
   const bool known=s.modeValueMs[0]&&now-s.modeValueMs[0]<8000&&(s.weightUnitRaw==0||s.weightUnitRaw==1);
   if(readout.liveWeight)formatLiveWeight();
   else if(!known)snprintf(text,sizeof(text),"--");
   else if(s.weightUnitRaw==1)snprintf(text,sizeof(text),"%.1f",remote::displayWeightKilograms(bandEdit.target,false));
   else snprintf(text,sizeof(text),"%d",bandEdit.target);
   setFont(number,dialPulseMs&&now-dialPulseMs<500?&font_inter_54:&font_inter_48,0);setText(number,text);
   setBgColor(number,color(accent),0);setBgOpacity(number,stopping?LV_OPA_COVER:LV_OPA_50,0);
   setTextColor(number,color(theme::Background),0);setTextOpacity(number,LV_OPA_COVER,0);
   setText(unit,readout.liveWeight?readoutUnit:s.weightUnitRaw==1?"Max kg":s.weightUnitRaw==0?"Max lbs":"Max");setTextColor(unit,color(theme::Background),0);
   if(s.modeValueMs[1]&&now-s.modeValueMs[1]<8000&&s.lengthUnitRaw==0)snprintf(text,sizeof(text),"Band length: %d cm\nSettings read from Voltra",s.modeValues[1]);
   else if(s.modeValueMs[1]&&now-s.modeValueMs[1]<8000&&s.lengthUnitRaw==1)snprintf(text,sizeof(text),"Band length: %.1f in\nSettings read from Voltra",s.modeValues[1]/2.54);
   else snprintf(text,sizeof(text),"Reading Band settings...");
   setText(bandDetails,text);setText(details,bandEdit.timedOut?"Not confirmed - try again":bandEdit.pending?"Waiting for Voltra":vlink::selectedFollower(s,now)?"Select the Twin controller":stopping?(readout.liveWeight?"Tap weight to unload":"Tap maximum to unload"):loadReady?"Turn to adjust · Tap to load":"Reading settings...");
 }else if(damper){
   const bool known=s.modeValueMs[5]&&now-s.modeValueMs[5]<5000&&s.modeValues[5]>=0&&s.modeValues[5]<=9;
   if(readout.liveWeight)formatLiveWeight();
   else if(known)snprintf(text,sizeof(text),"%d",remote::damperFactor(damperEdit.target));else snprintf(text,sizeof(text),"--");
   setText(number,text);setFont(number,dialPulseMs&&now-dialPulseMs<500?&font_inter_80:&font_inter_72,0);
   setText(unit,readout.liveWeight?readoutUnit:"Factor");setTextColor(unit,color(theme::Muted),0);setBgOpacity(number,LV_OPA_TRANSP,0);
   setText(damperRoman,known?remote::damperRoman(damperEdit.target):"--");setTextColor(damperRoman,color(accent),0);setTextOpacity(damperRoman,stopping?LV_OPA_COVER:LV_OPA_50,0);
   setText(details,damperEdit.timedOut?"Not confirmed - try again":damperEdit.pending?"Waiting for Voltra":stopping?(readout.liveWeight?"Tap weight to unload":"Tap factor to unload"):loadReady?"Turn to adjust · Tap to load":"Reading settings...");
 }else if(iso){
   const int index=isoIndex(s,isoPage);const auto& active=isoEdits[index-7];
   if(readout.liveWeight)formatLiveWeight();else isoText(text,sizeof(text),s,index,active,now);
   const bool autoSpeed=!readout.liveWeight&&index==9&&active.known&&active.target==0;
   if(autoSpeed)text[0]=0;setText(number,text);
   setFont(number,index==8?&font_inter_20:dialPulseMs&&now-dialPulseMs<500?&font_inter_80:&font_inter_72,0);
   setBgOpacity(number,LV_OPA_TRANSP,0);setTextColor(unit,color(theme::Muted),0);
   setText(unit,readout.liveWeight?readoutUnit:index==8||autoSpeed?"":index==7||index==9?"m/s":s.weightUnitRaw==1?"kg":s.weightUnitRaw==0?"lbs":"");
   const char* labels[]={"Speed","Return","Ecc.","Limit"};
   for(int i=0;i<4;++i){const int param=isoIndex(s,i);setText(isoLabels[i],labels[i]);setBgColor(tabs[i],color(i==isoPage?theme::Selected:theme::Surface),0);setTextColor(isoLabels[i],color(i==isoPage?accent:theme::Text),0);setTextColor(tabValues[i],color(i==isoPage?accent:theme::Text),0);isoText(text,sizeof(text),s,param,isoEdits[param-7],now);setText(tabValues[i],text);setFont(tabValues[i],param==8?&font_inter_12:&font_inter_16,0);}
   setText(details,active.timedOut?"Not confirmed - try again":pending()?"Waiting for Voltra":stopping?(isoPage==0?(readout.liveWeight?"Tap weight to unload":"Tap speed to unload"):"Unload to edit settings"):isoPage==0?"Turn to adjust · Tap to load":isoPage==1?"Eccentric return type":isoPage==2?(index==9?"Eccentric speed · 0 = Auto":"Eccentric resistance"):"Maximum eccentric load");
 }else{setBgOpacity(number,LV_OPA_TRANSP,0);setTextColor(unit,color(theme::Muted),0);}
 for(int i=0;i<4;++i){setHidden(isoLabels[i],!iso);if(!iso)setFont(tabValues[i],&font_inter_16,0);}

 // Unloaded is an explicit trainer state, not missing telemetry. Keep the
 // configured number, dim it, and place the same indicator beside every mode.
 static int lastDotX=0,lastDotY=0;
 if(readout.unloaded&&strcmp(lv_label_get_text(number),"--")&&lv_label_get_text(number)[0]){
   lv_obj_update_layout(number);lv_area_t bounds;lv_obj_get_coords(number,&bounds);
   const int x=bounds.x2+8,y=(bounds.y1+bounds.y2)/2;
   if(lv_obj_has_flag(unloadedDot,LV_OBJ_FLAG_HIDDEN)||x!=lastDotX||y!=lastDotY){
     lv_obj_align_to(unloadedDot,number,LV_ALIGN_OUT_RIGHT_MID,8,0);lastDotX=x;lastDotY=y;
   }
   setHidden(unloadedDot,false);setBgColor(unloadedDot,color(accent),0);setBgOpacity(unloadedDot,LV_OPA_50,0);
 }else setHidden(unloadedDot,true);

 if(repaintView){lv_obj_invalidate(screen);repaintView=false;}
}
}
