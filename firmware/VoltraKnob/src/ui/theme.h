#pragma once
#include <lvgl.h>
LV_FONT_DECLARE(font_inter_12);
LV_FONT_DECLARE(font_inter_14);
LV_FONT_DECLARE(font_inter_16);
LV_FONT_DECLARE(font_inter_20);
LV_FONT_DECLARE(font_inter_48);
LV_FONT_DECLARE(font_inter_54);
LV_FONT_DECLARE(font_inter_60);
LV_FONT_DECLARE(font_inter_72);
LV_FONT_DECLARE(font_inter_80);
namespace ui { namespace theme {
// Visually matched to Beyond Power's published VOLTRA UI reference.
constexpr uint32_t Background=0x000000;
constexpr uint32_t Surface=0x222222;
constexpr uint32_t SurfaceLow=0x171717;
constexpr uint32_t Selected=0x474747;
constexpr uint32_t Track=0x262626;
constexpr uint32_t Text=0xF5F5F5;
constexpr uint32_t Muted=0x969696;
constexpr uint32_t Lime=0xD6F568;
// Mode IDs come from the trainer protocol. Unverified/idle modes stay neutral.
constexpr uint32_t modeAccent(int mode) {
 switch(mode) {
  case 1: return Lime;
  case 2: return 0x60DDD0;
  case 4: return 0x89FFA3;
  case 6: return 0x42F875;
  case 7: return 0xF4E755;
  case 8: return 0x95EDEB;
  default: return Muted;
 }
}
constexpr uint32_t modeTint(uint32_t accent) {
 return (((accent>>16)&255)/5<<16)|(((accent>>8)&255)/5<<8)|((accent&255)/5);
}
constexpr uint32_t dimColor(uint32_t rgb) {
 return (((rgb>>16)&255)/2<<16)|(((rgb>>8)&255)/2<<8)|((rgb&255)/2);
}
constexpr uint32_t Pending=0xF4BF66;
constexpr uint32_t Unload=0xA33242;
}}
