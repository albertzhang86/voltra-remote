#include "capture.h"
#include "capture_buffer.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
namespace {
constexpr size_t samples=16000*8, block=128;
voice::CaptureBuffer rawBuffer,processedBuffer;
int16_t *rawData=nullptr,*processedData=nullptr;
bool recording=false,dumping=false,header=false,armRequested=false;
size_t cursor=0;
char input[32];size_t inputLength=0;
}
extern "C" void sr_audio_observe(const int16_t* data,size_t count){processedBuffer.append(data,count);}
namespace voice_capture {
bool armed(){return armRequested;}
bool started(){return recording;}
bool complete(){return recording&&rawBuffer.size()==samples&&processedBuffer.size()==samples;}
bool start(){
 if(!armRequested||recording)return false;
 rawData=static_cast<int16_t*>(heap_caps_malloc(samples*2,MALLOC_CAP_SPIRAM));
 processedData=static_cast<int16_t*>(heap_caps_malloc(samples*2,MALLOC_CAP_SPIRAM));
 if(!rawData||!processedData){free(rawData);free(processedData);rawData=processedData=nullptr;return false;}
 recording=true;rawBuffer.arm(rawData,samples);processedBuffer.arm(processedData,samples);return true;
}
void raw(const int16_t* data,size_t count){rawBuffer.append(data,count);}
void loop(){
 // Bounded USB requests. Capture is started only by an explicit screen tap.
 for(int n=0;n<32&&Serial.available();++n){
  char c=Serial.read();
  if(c=='\n'){
   input[inputLength]=0;
   if(!strcmp(input,"VOICE_ARM_CAPTURE")&&!recording)armRequested=true;
   if(!strcmp(input,"VOICE_DUMP")&&complete()){dumping=true;header=false;cursor=0;}
   inputLength=0;
  }else if(c!='\r'){
   if(inputLength<sizeof(input)-1)input[inputLength++]=c;
   else inputLength=0;
  }
 }
 if(!dumping||Serial.availableForWrite()<640)return;
 if(!header){Serial.printf("[audio-begin] rate=16000 samples=%u\n",unsigned(samples));header=true;return;}
 if(cursor==samples*2){Serial.println("[audio-end]");dumping=false;return;}
 const bool processed=cursor>=samples;const size_t offset=cursor%samples;
 const int16_t* data=(processed?processedData:rawData)+offset;
 char line[600];int pos=snprintf(line,sizeof(line),"[audio-data] %c %u ",processed?'P':'R',unsigned(offset));
 const char* hex="0123456789abcdef";
 for(size_t i=0;i<block;++i){uint16_t v=uint16_t(data[i]);for(int shift: {4,0,12,8})line[pos++]=hex[(v>>shift)&15];}
 line[pos++]='\n';Serial.write(reinterpret_cast<const uint8_t*>(line),pos);cursor+=block;
 }
}
