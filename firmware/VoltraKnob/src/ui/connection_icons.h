#pragma once
#include <lvgl.h>

namespace ui {
enum class ConnectionIcon { Tv, Devices, Search };

// Small outline icons share a 24px canvas and the connection button's text color.
inline void drawConnectionIcon(lv_layer_t* layer,int x,int y,ConnectionIcon icon,lv_color_t ink){
 auto line=[&](int x1,int y1,int x2,int y2){
  lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=ink;d.width=2;d.round_start=d.round_end=1;
  d.p1={lv_value_precise_t(x+x1),lv_value_precise_t(y+y1)};d.p2={lv_value_precise_t(x+x2),lv_value_precise_t(y+y2)};lv_draw_line(layer,&d);
 };
 auto outline=[&](int x1,int y1,int x2,int y2){
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_TRANSP;d.border_color=ink;d.border_width=2;d.radius=3;
  lv_area_t bounds={x+x1,y+y1,x+x2,y+y2};lv_draw_rect(layer,&d,&bounds);
 };
 switch(icon){
  case ConnectionIcon::Tv:
   outline(1,3,22,17);line(12,18,12,21);line(7,22,17,22);break;
  case ConnectionIcon::Devices:
   outline(1,1,14,16);outline(9,7,22,22);line(13,18,18,18);break;
  case ConnectionIcon::Search:{
   lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.color=ink;d.width=2;d.center={x+9,y+9};d.radius=8;d.start_angle=0;d.end_angle=360;lv_draw_arc(layer,&d);
   line(15,15,22,22);break;
  }
 }
}
}
