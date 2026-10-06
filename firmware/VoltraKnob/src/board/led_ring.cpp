#include "board.h"
#include <Arduino.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

namespace board {
// JC3636K718: 13 WS2812 pixels, GRB order, GPIO0. Initialized only after boot.
void setRingColor(uint32_t rgb) {
 static rmt_channel_handle_t channel=nullptr;
 static rmt_encoder_handle_t encoder=nullptr;
 static bool attempted=false;
 static uint32_t previous=0xffffffff;
 static rmt_symbol_word_t symbols[13*24+1];
 if(!attempted) {
  attempted=true;
  rmt_tx_channel_config_t config={};
  config.gpio_num=GPIO_NUM_0;config.clk_src=RMT_CLK_SRC_DEFAULT;
  config.resolution_hz=10000000;config.mem_block_symbols=128;
  config.trans_queue_depth=1;config.flags.with_dma=true;
  rmt_copy_encoder_config_t copy={};
  esp_err_t result=rmt_new_tx_channel(&config,&channel);
  if(result==ESP_OK)result=rmt_new_copy_encoder(&copy,&encoder);
  if(result==ESP_OK)result=rmt_enable(channel);
  if(result!=ESP_OK) {
   Serial.printf("[ring] initialization failed: %s\n",esp_err_to_name(result));
   if(encoder)rmt_del_encoder(encoder);
   if(channel)rmt_del_channel(channel);
   channel=nullptr;encoder=nullptr;return;
  }
  Serial.println("[ring] 13 WS2812 pixels ready (GPIO0, GRB, DMA)");
 }
 if(!channel || rgb==previous)return;
 // Modest brightness preserves the palette without an overly bright 13-pixel ring.
 const uint8_t grb[]={uint8_t(((rgb>>8)&255)*40/255),uint8_t(((rgb>>16)&255)*40/255),uint8_t((rgb&255)*40/255)};
 unsigned index=0;
 for(unsigned pixel=0;pixel<13;++pixel)for(uint8_t byte:grb)for(int bit=7;bit>=0;--bit) {
  auto& symbol=symbols[index++];const bool one=byte&(1<<bit);
  symbol.level0=1;symbol.duration0=one?8:4;
  symbol.level1=0;symbol.duration1=one?5:9;
 }
 symbols[index].level0=0;symbols[index].duration0=1500;
 symbols[index].level1=0;symbols[index].duration1=1500;
 rmt_transmit_config_t transmit={};
 esp_err_t result=rmt_transmit(channel,encoder,symbols,sizeof(symbols),&transmit);
 if(result==ESP_OK)result=rmt_tx_wait_all_done(channel,20);
 if(result==ESP_OK){previous=rgb;Serial.printf("[ring] color=%06lx\n",static_cast<unsigned long>(rgb));}
 else {
  Serial.printf("[ring] disabled after update failure: %s\n",esp_err_to_name(result));
  rmt_disable(channel);rmt_del_encoder(encoder);rmt_del_channel(channel);
  channel=nullptr;encoder=nullptr;
 }
}
}
