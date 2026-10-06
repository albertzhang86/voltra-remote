#include "minitest.h"
#include "voice/audio_input.h"
#include <array>
#include <cmath>
int main(){
 voice::AudioInput filter;std::array<int16_t,512> audio;
 audio.fill(1365);filter.process(audio.data(),audio.size());
 for(auto value:audio)CHECK_EQ(value,0); // Recorded steady offset must not amplify.
 double sum=0,power=0;int samples=0;
 for(int block=0;block<20;++block){
  for(size_t i=0;i<audio.size();++i)audio[i]=1365+std::lround(400*std::sin(2*3.141592653589793*500*(block*512+i)/16000));
  filter.process(audio.data(),audio.size());
  if(block>4)for(auto value:audio){sum+=value;power+=double(value)*value;++samples;}
 }
 CHECK(std::abs(sum/samples)<2);CHECK(std::sqrt(power/samples)>1000);CHECK(std::sqrt(power/samples)<1200);
 for(int block=0;block<4;++block){
  for(size_t i=0;i<audio.size();++i)audio[i]=std::lround(30000*std::sin(2*3.141592653589793*500*i/16000));
  filter.process(audio.data(),audio.size());
  for(auto value:audio)CHECK(std::abs(int(value))<=28001);
 }
 CHECK(filter.gain()<1.1f);
 for(int block=0;block<80;++block){audio.fill(1365);filter.process(audio.data(),audio.size());}
 CHECK(filter.gain()>3.9f);for(auto value:audio)CHECK(std::abs(int(value))<=1);
 printf("%d audio checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
