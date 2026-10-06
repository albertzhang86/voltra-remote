#pragma once
#include <lvgl.h>
#include "../voice/recognition.h"
namespace ui {bool voiceAction(const voice::Event& event);void begin();bool selectingTrainer();void loop();void filterTouch(lv_indev_t* input,lv_indev_data_t* data);}
