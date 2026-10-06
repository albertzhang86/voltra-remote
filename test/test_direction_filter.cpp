#include "minitest.h"
#include "board/direction_filter.h"
#include <fstream>
#include <vector>
struct Sample {uint32_t us;int right,left;};
int main(){
 // Replay the actual failing clockwise/counterclockwise capture at every
 // sampling phase. Release noise must not become a turn in the other direction.
 std::ifstream input("test/fixtures/encoder-release-spikes.csv");
 std::vector<Sample> capture;Sample s;char comma;
 while(input>>s.us>>comma>>s.right>>comma>>s.left)capture.push_back(s);
 CHECK(!capture.empty());if(capture.empty())return 1;
 for(uint32_t phase=0;phase<500;phase+=10){
  board::DirectionFilter right,left;size_t next=0;int r=1,l=1,cw=0,ccw=0;
  for(uint32_t us=phase;us<capture.back().us+5000;us+=500){
   while(next<capture.size()&&capture[next].us<=us){r=capture[next].right;l=capture[next].left;++next;}
   cw+=right.sample(r==0,us);ccw+=left.sample(l==0,us);
  }
  CHECK_EQ(cw,5);CHECK_EQ(ccw,8);
 }
 // Rapid alternating real pulses remain responsive, without a direction lock.
 board::DirectionFilter right,left;int cw=0,ccw=0;
 for(uint32_t us=0;us<100000;us+=500){
  cw+=right.sample(us%10000<5000,us);
  ccw+=left.sample(us%10000>=5000,us);
 }
 CHECK_EQ(cw,10);CHECK_EQ(ccw,10);
 // Holding a contact does not repeat. Brief release bounce does not rearm it.
 board::DirectionFilter held;int count=0;
 for(uint32_t us=0;us<100000;us+=100)count+=held.sample(us!=5000,us);
 CHECK_EQ(count,1);
 // Micros wraps approximately every 71 minutes.
 board::DirectionFilter wrap;
 CHECK(!wrap.sample(true,UINT32_MAX-499));CHECK(wrap.sample(true,500));
 std::printf("%d encoder checks, %d failures\n",g_checks,g_failures);
 return g_failures?1:0;
}
