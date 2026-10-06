#include "../voice/control.h"
#include "ui.h"
#include "theme.h"
#include "mode_icons.h"
#include <cmath>
#include <cstring>
#include <Arduino.h>
#include <lvgl.h>
#include "../board/board.h"
#include "../link/voltra_link.h"
#include "../app/remote_model.h"
LV_IMAGE_DECLARE(polar_bear_logo);
namespace ui {
namespace {
lv_obj_t *screen,*trainerButton,*trainerLabel,*title,*number,*unit,*status,*details,*action,*unloadedDot,*selector,*choices[2],*choiceLabels[2],*choiceDetails[2],*arc,*selectorNotice;
lv_scale_section_t* filledTicks;
lv_style_t filledTickStyle;
lv_obj_t *tabs[4],*tabValues[4],*isoLabels[4],*modePanel;
lv_obj_t *bandDetails,*damperRoman,*switchController,*switchControllerText;
bool choosingController=false;
bool pairedChoice(const vlink::DeviceState& s,int slot,uint32_t now){return !choosingController&&vlink::twinActive(s,now)&&s.selected>=0&&slot!=s.selected&&s.trainers[slot].address[0];}
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
lv_obj_t* label(lv_obj_t* parent,const char* text,int x,int y,const lv_font_t* font,uint32_t ink=theme::Text){
 auto* l=lv_label_create(parent);setText(l,text);setFont(l,font,0);setTextColor(l,color(ink),0);lv_obj_align(l,LV_ALIGN_TOP_MID,x,y);return l;
}
lv_obj_t* button(lv_obj_t* parent,int width,int height,int x,int y,uint32_t fill){
 auto* b=lv_button_create(parent);lv_obj_set_size(b,width,height);lv_obj_align(b,LV_ALIGN_TOP_MID,x,y);setBgColor(b,color(fill),0);lv_obj_set_style_radius(b,16,0);lv_obj_set_style_shadow_width(b,0,0);return b;
}
void choose(lv_event_t* event){
 int slot=static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
 if(!vlink::trainerAvailable(vlink::voltraLink.state(),slot,millis()))return;
 if(pairedChoice(vlink::voltraLink.state(),slot,millis()))return;
 if(vlink::voltraLink.selectTrainer(slot)){choosingController=false;choosing=false;lv_obj_add_flag(selector,LV_OBJ_FLAG_HIDDEN);for(auto& e:edits)e.reset();bandEdit.reset();damperEdit.reset();resetIso();loadGesture=false;loadSentMs=0;}
}
void openSelector(lv_event_t*){choosing=true;loadGesture=false;lv_obj_remove_flag(selector,LV_OBJ_FLAG_HIDDEN);}
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
  if(unload){vlink::voltraLink.requestUnload();loadSentMs=0;}
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
 if(choosing||splashActive)return;
 loadGesture=false;unloadGesture=false;
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
 if(pressed&&!down){down=true;consumed=false;eligible=!choosing&&!splashActive;startX=data->point.x;startY=data->point.y;}
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
 for(int i=0;i<MODE_COUNT;++i){
  const double angle=(-90.0+i*360.0/MODE_COUNT)*3.141592653589793/180.0;
  const int x=static_cast<int>(std::lround(122*std::cos(angle)));
  const int y=174+static_cast<int>(std::lround(122*std::sin(angle)))-32;
  modeIcons[i]=button(modePanel,64,64,x,y,theme::Surface);lv_obj_set_style_radius(modeIcons[i],LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_pad_all(modeIcons[i],0,0);lv_obj_remove_flag(modeIcons[i],LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(modeIcons[i],iconDraw,LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  lv_obj_add_event_cb(modeIcons[i],selectMode,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));
 }
 modeCenter=button(modePanel,112,112,0,118,theme::SurfaceLow);lv_obj_set_style_radius(modeCenter,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(modeCenter,2,0);
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
 label(selector,"SELECT TRAINER",0,49,&font_inter_20);
 selectorNotice=label(selector,"One Voltra at a time",0,79,&font_inter_14,theme::Muted);
 for(int i=0;i<2;++i){choices[i]=button(selector,248,66,0,112+i*79,theme::Surface);lv_obj_set_style_bg_color(choices[i],color(theme::Surface),LV_STATE_DISABLED);lv_obj_set_style_color_filter_opa(choices[i],LV_OPA_TRANSP,LV_STATE_DISABLED);choiceLabels[i]=label(choices[i],"Searching...",0,0,&font_inter_16);lv_obj_set_width(choiceLabels[i],214);lv_label_set_long_mode(choiceLabels[i],LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(choiceLabels[i],LV_TEXT_ALIGN_CENTER,0);lv_obj_align(choiceLabels[i],LV_ALIGN_CENTER,0,-11);choiceDetails[i]=label(choices[i],"",0,0,&font_inter_14,theme::Muted);lv_obj_align(choiceDetails[i],LV_ALIGN_CENTER,0,12);lv_obj_add_event_cb(choices[i],choose,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<intptr_t>(i)));}
 switchController=button(selector,194,34,0,286,theme::SurfaceLow);switchControllerText=label(switchController,"Switch controller",0,0,&font_inter_14);lv_obj_center(switchControllerText);lv_obj_add_flag(switchController,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_event_cb(switchController,[](lv_event_t*){const auto s=vlink::voltraLink.state();if(vlink::canSwitch(s,millis())&&!pending()&&!loadSentMs)choosingController=!choosingController;},LV_EVENT_CLICKED,nullptr);
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


}
bool selectingTrainer(){return choosing;}
bool voiceAction(const voice::Event& event){
 static uint32_t releaseAt=0;static bool released=false;
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();
 const bool controlPage=!choosing&&!choosingMode&&!choosingCable&&!splashActive;
 if(!voice::canAct(event,s,now,controlPage,pending(),loadSentMs!=0,released&&now-releaseAt<1500))return false;
 loadGesture=false;unloadGesture=false;
 if(event.kind==voice::EventKind::Release){
  if(!vlink::voltraLink.requestUnload(event.session))return false;
  loadSentMs=0;released=true;releaseAt=now;return true;
 }
 if(!vlink::voltraLink.requestLoad(event.session))return false;
 loadSentMs=now;return true;
}
void loop(){
 vlink::voltraLink.setDiscoveryVisible(choosing);
 if(pendingSwipe){const int direction=pendingSwipe;pendingSwipe=0;navigateSwipe(direction);}
 const uint32_t now=millis();const auto s=vlink::voltraLink.state();
 if(s.session!=session){choosingController=false;initialModeHandled=false;closeModes();session=s.session;dialPulseMs=0;choosingCable=false;cableRequested=false;lv_obj_add_flag(cablePanel,LV_OBJ_FLAG_HIDDEN);for(auto& e:edits)e.reset();bandEdit.reset();damperEdit.reset();resetIso();loadGesture=false;loadSentMs=0;}
 const bool live=vlink::fresh(s,now);
 if(bandSessionMode!=s.mode){bandSessionMode=s.mode;bandEdit.reset();damperEdit.reset();resetIso();}
 if(live&&s.mode==2&&s.modeValueMs[0]&&now-s.modeValueMs[0]<8000)bandEdit.observe(s.modeValues[0]);
 if(choosingMode&&(s.loaded||s.armed||loadSentMs))closeModes();
 const bool mappedState=mappedMode(s.mode)&&(s.fitnessMode==4||s.fitnessMode==5||s.armed);
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
 setHidden(switchController,!vlink::twinActive(s,now));setText(switchControllerText,choosingController?"Cancel":"Switch controller");
 setText(selectorNotice,choosingController?"Select the Twin controller":vlink::twinActive(s,now)?"Twin control connection":vlink::canSwitch(s,now)?"One Voltra at a time":pending()?"Confirming settings...":"Unload / wait before switching");
 for(int i=0;i<2;++i){
   const auto& t=s.trainers[i];
   setText(choiceLabels[i],t.name[0]?t.name:t.address[0]?"Unnamed Voltra":"Searching...");
   const bool paired=pairedChoice(s,i,now);
   const bool available=vlink::trainerAvailable(s,i,now);
   if(paired)snprintf(text,sizeof(text),"Paired");
   else if(!choosingController&&vlink::twinActive(s,now)&&i==s.selected)snprintf(text,sizeof(text),"Control");
   else if(s.selected==i&&(s.link==vlink::LinkState::Connecting||s.link==vlink::LinkState::Handshaking))snprintf(text,sizeof(text),"Connecting...");
   else snprintf(text,sizeof(text),"%s",!t.address[0]?"Waiting for trainer":s.selected==i&&s.link==vlink::LinkState::Ready?"Connected":available?"Nearby":"Not detected");
   setText(choiceDetails[i],text);
   const bool disabled=paired||!available;
   if(disabled!=lv_obj_has_state(choices[i],LV_STATE_DISABLED)){if(disabled)lv_obj_add_state(choices[i],LV_STATE_DISABLED);else lv_obj_remove_state(choices[i],LV_STATE_DISABLED);}

   setBgColor(choices[i],color(s.selected==i?tint:theme::Surface),0);
   setTextColor(choiceLabels[i],color(disabled?theme::Muted:s.selected==i?accent:theme::Text),0);
 }
 const char* state=s.link==vlink::LinkState::Scanning?"SEARCHING":s.link==vlink::LinkState::Connecting?"CONNECTING":s.link==vlink::LinkState::Handshaking?"ACCEPT ON VOLTRA":!live?"STATE UNAVAILABLE":s.loaded?(s.mode==3?"RUNNING":"LOADED"):s.armed?"AUTO LOAD ACTIVE":s.mode==0?"IDLE":s.fitnessMode==4?"UNLOADED":"NOT READY";
 setText(status,state);setTextColor(status,color(live&&(s.loaded||s.armed)?accent:theme::Muted),0);
 const bool band=live&&s.mode==2,damper=live&&s.mode==4,iso=live&&s.mode==7;
 auto& edit=edits[page];
 bool numberChanged=false;
 if(!band&&!damper&&!iso){
 if(!live||!edit.known||s.mode!=1||(s.twinConnection==2&&!vlink::twinActive(s,now)))snprintf(text,sizeof(text),"--");else snprintf(text,sizeof(text),"%.1f",remote::displayWeightKilograms(edit.target,vlink::twinActive(s,now)));
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
 const bool showDot=page==0&&live&&s.mode==1&&!stopping;
 if(showDot){const bool dotMoved=numberChanged||lv_obj_has_flag(unloadedDot,LV_OBJ_FLAG_HIDDEN);if(lv_obj_has_flag(unloadedDot,LV_OBJ_FLAG_HIDDEN))lv_obj_remove_flag(unloadedDot,LV_OBJ_FLAG_HIDDEN);if(dotMoved){lv_obj_update_layout(number);lv_obj_align_to(unloadedDot,number,LV_ALIGN_OUT_RIGHT_MID,8,0);}setBgColor(unloadedDot,color(accent),0);setBgOpacity(unloadedDot,LV_OPA_50,0);}
 else lv_obj_add_flag(unloadedDot,LV_OBJ_FLAG_HIDDEN);
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
 setHidden(bandDetails,!band);setHidden(damperRoman,!damper);for(auto* tab:tabs)setHidden(tab,band||damper);
 if(band){
   const bool known=s.modeValueMs[0]&&now-s.modeValueMs[0]<8000&&(s.weightUnitRaw==0||s.weightUnitRaw==1);
   if(!known)snprintf(text,sizeof(text),"--");
   else if(s.weightUnitRaw==1)snprintf(text,sizeof(text),"%.1f",remote::displayWeightKilograms(bandEdit.target,false));
   else snprintf(text,sizeof(text),"%d",bandEdit.target);
   setFont(number,dialPulseMs&&now-dialPulseMs<500?&font_inter_54:&font_inter_48,0);setText(number,text);
   setBgColor(number,color(accent),0);setBgOpacity(number,stopping?LV_OPA_COVER:LV_OPA_50,0);
   setTextColor(number,color(theme::Background),0);setTextOpacity(number,LV_OPA_COVER,0);
   setText(unit,s.weightUnitRaw==1?"Max kg":s.weightUnitRaw==0?"Max lbs":"Max");setTextColor(unit,color(theme::Background),0);
   setHidden(unloadedDot,true);
   if(s.modeValueMs[1]&&now-s.modeValueMs[1]<8000&&s.lengthUnitRaw==0)snprintf(text,sizeof(text),"Band length: %d cm\nSettings read from Voltra",s.modeValues[1]);
   else if(s.modeValueMs[1]&&now-s.modeValueMs[1]<8000&&s.lengthUnitRaw==1)snprintf(text,sizeof(text),"Band length: %.1f in\nSettings read from Voltra",s.modeValues[1]/2.54);
   else snprintf(text,sizeof(text),"Reading Band settings...");
   setText(bandDetails,text);setText(details,bandEdit.timedOut?"Not confirmed - try again":bandEdit.pending?"Waiting for Voltra":vlink::selectedFollower(s,now)?"Select the Twin controller":stopping?"Tap maximum to unload":loadReady?"Turn to adjust · Tap to load":"Reading settings...");
 }else if(damper){
   const bool known=s.modeValueMs[5]&&now-s.modeValueMs[5]<5000&&s.modeValues[5]>=0&&s.modeValues[5]<=9;
   if(known)snprintf(text,sizeof(text),"%d",remote::damperFactor(damperEdit.target));else snprintf(text,sizeof(text),"--");
   setText(number,text);setFont(number,dialPulseMs&&now-dialPulseMs<500?&font_inter_80:&font_inter_72,0);
   setText(unit,"Factor");setTextColor(unit,color(theme::Muted),0);setBgOpacity(number,LV_OPA_TRANSP,0);
   setText(damperRoman,known?remote::damperRoman(damperEdit.target):"--");setTextColor(damperRoman,color(accent),0);setTextOpacity(damperRoman,stopping?LV_OPA_COVER:LV_OPA_50,0);
   setText(details,damperEdit.timedOut?"Not confirmed - try again":damperEdit.pending?"Waiting for Voltra":stopping?"Tap factor to unload":loadReady?"Turn to adjust · Tap to load":"Reading settings...");
 }else if(iso){
   const int index=isoIndex(s,isoPage);const auto& active=isoEdits[index-7];
   isoText(text,sizeof(text),s,index,active,now);
   const bool autoSpeed=index==9&&active.known&&active.target==0;
   if(autoSpeed)text[0]=0;setText(number,text);
   setFont(number,index==8?&font_inter_20:dialPulseMs&&now-dialPulseMs<500?&font_inter_80:&font_inter_72,0);
   setBgOpacity(number,LV_OPA_TRANSP,0);setTextColor(unit,color(theme::Muted),0);
   setText(unit,index==8||autoSpeed?"":index==7||index==9?"m/s":s.weightUnitRaw==1?"kg":s.weightUnitRaw==0?"lbs":"");
   const char* labels[]={"Speed","Return","Ecc.","Limit"};
   for(int i=0;i<4;++i){const int param=isoIndex(s,i);setText(isoLabels[i],labels[i]);setBgColor(tabs[i],color(i==isoPage?theme::Selected:theme::Surface),0);setTextColor(isoLabels[i],color(i==isoPage?accent:theme::Text),0);setTextColor(tabValues[i],color(i==isoPage?accent:theme::Text),0);isoText(text,sizeof(text),s,param,isoEdits[param-7],now);setText(tabValues[i],text);setFont(tabValues[i],param==8?&font_inter_12:&font_inter_16,0);}
   setText(details,active.timedOut?"Not confirmed - try again":pending()?"Waiting for Voltra":stopping?(isoPage==0?"Tap speed to unload":"Unload to edit settings"):isoPage==0?"Turn to adjust · Tap to load":isoPage==1?"Eccentric return type":isoPage==2?(index==9?"Eccentric speed · 0 = Auto":"Eccentric resistance"):"Maximum eccentric load");
 }else{setBgOpacity(number,LV_OPA_TRANSP,0);setTextColor(unit,color(theme::Muted),0);}
 for(int i=0;i<4;++i){setHidden(isoLabels[i],!iso);if(!iso)setFont(tabValues[i],&font_inter_16,0);}

 if(repaintView){lv_obj_invalidate(screen);repaintView=false;}
}
}
