#include "board.h"
#include "../ui/ui.h"
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "direction_filter.h"
#include "guition_init.h"
namespace board {
namespace {
Arduino_ESP32QSPI bus(12, 11, 13, 14, 15, 16);
Arduino_ST77916 display(&bus, 17, 0, true, 360, 360, 0, 0, 0, 0, guitionInit, sizeof(guitionInit));
QueueHandle_t rotations;
DirectionFilter leftFilter, rightFilter;
esp_timer_handle_t encoderTimer;
uint32_t touchReads=0,touchErrors=0,touchPresses=0,rotateEvents=0,rotateLeft=0,rotateRight=0;
int touchX=-1,touchY=-1;
bool wasTouched=false;
void sampleEncoder(void*) {
  const uint32_t us = micros();
  const bool right = rightFilter.sample(gpio_get_level(GPIO_NUM_1)==0,us);
  const bool left = leftFilter.sample(gpio_get_level(GPIO_NUM_2)==0,us);
  if(right){RotateEvent ev{1,millis()};xQueueSend(rotations,&ev,0);}
  if(left){RotateEvent ev{-1,millis()};xQueueSend(rotations,&ev,0);}
}
void flush(lv_display_t* d, const lv_area_t* a, uint8_t* pixels) {
  display.draw16bitRGBBitmap(a->x1,a->y1,reinterpret_cast<uint16_t*>(pixels),a->x2-a->x1+1,a->y2-a->y1+1);
  lv_display_flush_ready(d);
}
// CST816 register reads, on the main/UI task only. Failure means released.
void touchRead(lv_indev_t* input, lv_indev_data_t* data) {
  data->state=LV_INDEV_STATE_RELEASED;
  ++touchReads;
  Wire.beginTransmission(0x15); Wire.write(0x02);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(0x15,5)!=5){++touchErrors;ui::filterTouch(input,data);return;}
  uint8_t b[5]; for(auto& v:b)v=Wire.read();
  if((b[0]&15)==1) {
    int x=((b[1]&15)<<8)|b[2], y=((b[3]&15)<<8)|b[4];
    if(x<360 && y<360) { data->point.x=x;data->point.y=y;data->state=LV_INDEV_STATE_PRESSED;touchX=x;touchY=y;if(!wasTouched)++touchPresses; }
  }
  wasTouched=data->state==LV_INDEV_STATE_PRESSED;
  ui::filterTouch(input,data);
}
}
bool begin() {
  // Keep the audio amplifier muted. LED data is initialized after boot.
  pinMode(46,OUTPUT);digitalWrite(46,LOW);
  pinMode(21,OUTPUT);digitalWrite(21,LOW);
  if(!display.begin(40000000)) return false;
  display.fillScreen(0);
  Wire.begin(9,10);Wire.setClock(400000);Wire.setTimeOut(20);
  pinMode(8,OUTPUT);digitalWrite(8,LOW);delay(10);digitalWrite(8,HIGH);delay(100);
  // Disable touch auto-sleep so taps remain responsive.
  Wire.beginTransmission(0x15);Wire.write(0xFE);Wire.write(0xFF);Wire.endTransmission();
  lv_init();lv_tick_set_cb([]()->uint32_t{return millis();});
  auto* d=lv_display_create(360,360);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
  constexpr size_t bytes=360*30*2;
  void* buffer=heap_caps_malloc(bytes,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  if(!buffer)return false;
  lv_display_set_buffers(d,buffer,nullptr,bytes,LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d,flush);
  auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,touchRead);lv_timer_set_period(lv_indev_get_read_timer(input),10);
  rotations=xQueueCreate(32,sizeof(RotateEvent));if(!rotations)return false;
  pinMode(2,INPUT_PULLUP);pinMode(1,INPUT_PULLUP);
  // Sample independently of UI rendering/BLE work, and reject sub-millisecond
  // opposite-pin glitches instead of treating every falling edge as a turn.
  esp_timer_create_args_t timerArgs{};
  timerArgs.callback=sampleEncoder;timerArgs.name="dial-filter";
  timerArgs.skip_unhandled_events=true;
  if(esp_timer_create(&timerArgs,&encoderTimer)!=ESP_OK)return false;
  if(esp_timer_start_periodic(encoderTimer,500)!=ESP_OK)return false;
  setRingColor(0);setBacklight(200);return true;
}
bool pollRotate(RotateEvent& out){bool got=xQueueReceive(rotations,&out,0)==pdTRUE;if(got){++rotateEvents;if(out.direction<0)++rotateLeft;else ++rotateRight;}return got;}
void traceDial(const char* reason,int field,int direction,int before,int target,int actual,bool pending,bool sent,uint32_t eventMs){
 if(Serial.availableForWrite()<190)return;
 Serial.printf("[dial] %s field=%d dir=%d before=%d target=%d actual=%d pending=%d sent=%d event=%lu now=%lu\n",reason,field,direction,before,target,actual,pending,sent,(unsigned long)eventMs,(unsigned long)millis());
}
void logInputs(){Serial.printf("[input] reads=%lu errors=%lu presses=%lu xy=%d,%d turns=%lu left=%lu right=%lu\n",(unsigned long)touchReads,(unsigned long)touchErrors,(unsigned long)touchPresses,touchX,touchY,(unsigned long)rotateEvents,(unsigned long)rotateLeft,(unsigned long)rotateRight);}
void setBacklight(uint8_t level){analogWrite(21,level);}
}
