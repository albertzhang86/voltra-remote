#pragma once
#include <lvgl.h>
#include <cmath>
LV_IMAGE_DECLARE(mode_weight);
LV_IMAGE_DECLARE(mode_band);
LV_IMAGE_DECLARE(mode_damper);
LV_IMAGE_DECLARE(mode_isokinetic);
namespace ui {
inline void drawModeIcon(lv_layer_t* layer,const lv_area_t& area,int mode,lv_color_t ink){
 const lv_image_dsc_t* asset=nullptr;
 switch(mode){case 1:asset=&mode_weight;break;case 2:asset=&mode_band;break;
 case 4:asset=&mode_damper;break;case 7:asset=&mode_isokinetic;break;default:return;}
 const int x=(area.x1+area.x2)/2-18,y=(area.y1+area.y2)/2-18;
 lv_area_t bounds={x,y,x+35,y+35};
 lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=asset;d.recolor=ink;d.recolor_opa=LV_OPA_COVER;
 lv_draw_image(layer,&d,&bounds);
}
// Modifier silhouettes follow Beyond Power's on-device UI reference:
// https://www.beyond-power.com/blogs/news/how-to-use-inverse-chains-with-voltra-i
inline void drawModifierIcon(lv_layer_t* layer,const lv_area_t& area,int parameter,lv_color_t ink,lv_color_t background){
 const int x=(area.x1+area.x2)/2,y=(area.y1+area.y2)/2;
 auto line=[&](int a,int b,int c,int e){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=ink;d.width=2;d.round_start=d.round_end=1;d.p1={lv_value_precise_t(x+a),lv_value_precise_t(y+b)};d.p2={lv_value_precise_t(x+c),lv_value_precise_t(y+e)};lv_draw_line(layer,&d);};
 auto arc=[&](int cx,int cy,int r,int from,int to){lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.color=ink;d.width=2;d.center={x+cx,y+cy};d.radius=r;d.start_angle=from;d.end_angle=to;d.rounded=1;lv_draw_arc(layer,&d);};
 if(parameter==2){
  // Three nested chevrons widen downwards, as on the trainer.
  line(-3,-9,0,-6);line(0,-6,3,-9);
  line(-6,-5,0,1);line(0,1,6,-5);
  line(-10,0,0,10);line(0,10,10,0);
 } else if(parameter==1||parameter==3){
  if(parameter==3){
   lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=ink;d.bg_opa=LV_OPA_COVER;d.radius=LV_RADIUS_CIRCLE;
   lv_area_t bounds={x-13,y-13,x+13,y+13};lv_draw_rect(layer,&d,&bounds);ink=background;
  }
  // Open upper/lower links joined by a short central vertical stroke.
  arc(0,-5,5,180,360);line(-5,-5,-5,-2);line(5,-5,5,-2);
  arc(0,5,5,0,180);line(-5,2,-5,5);line(5,2,5,5);
  line(0,-3,0,3);
 }
}

}
