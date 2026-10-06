#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace voice {
// Remove the measured microphone DC bias before amplifying quiet speech.
// 0.98 at 16 kHz gives a ~51 Hz high-pass corner. Per-block peak protection
// reduces gain immediately on loud input, then restores it gradually.
class AudioInput {
 public:
  void process(int16_t* samples,size_t count){
   float peak=0;
   for(size_t i=0;i<count;++i){
    const float x=samples[i];
    if(!started_){previous_=x;started_=true;}
    filtered_=x-previous_+0.98f*filtered_;previous_=x;
    samples[i]=static_cast<int16_t>(std::max(-32768.0f,std::min(32767.0f,filtered_)));
    peak=std::max(peak,std::abs(float(samples[i])));
   }
   const float target=peak>7000?28000.0f/peak:4.0f;
   gain_=std::min(target,gain_+0.05f);
   for(size_t i=0;i<count;++i)samples[i]=static_cast<int16_t>(std::lround(samples[i]*gain_));
  }
  float gain()const{return gain_;}
 private:
  bool started_=false;
  float previous_=0,filtered_=0,gain_=4;
};
}
