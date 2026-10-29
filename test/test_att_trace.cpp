#include "link/att_trace_buffer.h"
#include <cassert>
#include <iostream>

int main(){
  vlink::AttTraceBuffer trace;
  vlink::AttTraceEvent event;
  trace.watch(7);
  trace.record({1,0,8,4,0x13,5}); // Other link must not contaminate diagnosis.
  trace.record({2,0,7,4,0x12,2});
  for(unsigned i=0;i<500;++i)trace.record({3+i,0,7,4,0x1b,5});
  trace.record({30002,0,7,4,3,8}); // Timeout freezes the event trail.
  trace.record({30003,0,7,4,0x13,5});
  trace.watch(9); // Reconnect must not discard evidence before it is printed.
  assert(trace.notifications()==500&&trace.lastNotification()==502);
  assert(trace.pop(event)&&event.at==2&&event.kind==2);
  assert(trace.pop(event)&&event.at==30002&&event.kind==8);
  assert(!trace.pop(event));

  trace.watch(9);
  assert(trace.notifications()==0);
  for(unsigned i=0;i<100;++i)trace.record({i,0,9,4,0x12,2});
  trace.freeze();
  for(unsigned i=36;i<100;++i){assert(trace.pop(event));assert(event.at==i);}
  assert(!trace.pop(event));

  // A matched response remains distinguishable from an orphan response.
  trace.record({100,0,9,4,0x13,5});
  trace.record({101,0,9,4,0,6});
  trace.freeze();
  assert(trace.pop(event)&&event.kind==5);
  assert(trace.pop(event)&&event.kind==6&&event.detail==0);
  assert(!trace.pop(event));
  std::cout<<"ATT trace: link filtering, notification pressure, freeze, wrap and response evidence passed\n";
}
