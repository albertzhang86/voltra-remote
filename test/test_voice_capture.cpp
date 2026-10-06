#include "minitest.h"
#include "voice/capture_buffer.h"
using namespace voice;
int main(){
 CaptureBuffer capture;
 int16_t source[]={-32768,-1,0,1,32767},target[7]={};
 capture.append(source,5);CHECK_EQ(capture.size(),size_t(0));
 capture.arm(target,7);capture.append(source,5);CHECK_EQ(capture.size(),size_t(5));
 for(int i=0;i<5;++i)CHECK_EQ(target[i],source[i]);
 capture.append(source,5);CHECK_EQ(capture.size(),size_t(7));
 CHECK_EQ(target[5],source[0]);CHECK_EQ(target[6],source[1]);
 capture.append(source,5);CHECK_EQ(capture.size(),size_t(7));CHECK_EQ(target[6],source[1]);
 printf("%d capture checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
