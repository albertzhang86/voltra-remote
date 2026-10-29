#include "minitest.h"
#include "link/background_poll.h"
#include "link/poll_write_policy.h"
#include <cstring>
#include <initializer_list>
using namespace vlink;
static DeviceState ready(uint32_t now,int mode=1){
 DeviceState s;s.link=LinkState::Ready;s.mode=mode;s.fitnessMode=5;s.loaded=true;
 s.statusMs=s.endpointMs=now;s.endpoint.valid=true;s.endpoint.status=0;
 strcpy(s.endpoint.local,"02:00:00:00:00:01");strcpy(s.endpoint.peer,"00:00:00:00:00:00");
 return s;
}
int main(){
 // Production scheduling runs a ten-minute stream in every mode, both with
 // and without classified force notifications. Slow ACKs are accounted for
 // after completion; no background burst is allowed between foreground work.
 for(int mode:{1,2,4,7})for(bool streaming:{false,true}){
   uint32_t now=100,lastEnd=0,lastRequest=0;unsigned count[9]{};
   uint32_t stamps[8]{},parameterStamps[voltra::MODE_PARAMETER_COUNT]{};
   BackgroundPollSchedule schedule;auto state=ready(now,mode);
   bool noBurst=true,statusTimely=true,endpointTimely=true,cableTimely=true,parametersTimely=true;
   while(now<600100){
     if(streaming){state.forceMs=state.motionMs=now;state.activeWeightTenths=50;state.cableVelocityMmPerSecond=0;}
     const auto next=schedule.next(state,now,false);
     if(next!=BackgroundRead::None){
       if(lastRequest)noBurst&=now-lastEnd>=BackgroundPollSchedule::SLOT_MS;
       lastRequest=now;const unsigned index=static_cast<unsigned>(next);++count[index];
       if(stamps[index]){
         const auto age=now-stamps[index];
         if(next==BackgroundRead::Settings)statusTimely&=age<1500;
         if(next==BackgroundRead::Endpoint)endpointTimely&=age<1500;
         if(next==BackgroundRead::Cable)cableTimely&=age<1500;
       }
       stamps[index]=now;
       if(next==BackgroundRead::Settings)state.statusMs=now;
       if(next==BackgroundRead::Endpoint)state.endpointMs=now;
       if(next==BackgroundRead::ModeParameter){
         const int p=schedule.nextModeParameter(mode);CHECK(p>=0);CHECK_EQ(voltra::MODE_PARAMETERS[p].mode,mode);
         if(parameterStamps[p])parametersTimely&=now-parameterStamps[p]<8000;
         parameterStamps[p]=now;
       }
       now+=40;lastEnd=now;schedule.writeCompleted(now);
       // Calling again immediately, or before the slot ends, cannot send a
       // catch-up read even when multiple fields are overdue.
       CHECK_EQ(schedule.next(state,now,false),BackgroundRead::None);
       CHECK_EQ(schedule.next(state,now+99,false),BackgroundRead::None);
     }
     now+=20;
   }
   CHECK(noBurst);CHECK(statusTimely);CHECK(endpointTimely);CHECK(cableTimely);CHECK(parametersTimely);
   CHECK(count[unsigned(BackgroundRead::Settings)]>500);
   CHECK(count[unsigned(BackgroundRead::Endpoint)]>500);
   CHECK(count[unsigned(BackgroundRead::Cable)]>500);
   CHECK(count[unsigned(BackgroundRead::WeightUnit)]>=110);
   CHECK(count[unsigned(BackgroundRead::LengthUnit)]>=110);
   CHECK_EQ(count[unsigned(BackgroundRead::RuntimeWeight)]==0,streaming);
   for(int p=0;p<voltra::MODE_PARAMETER_COUNT;++p)
     if(voltra::MODE_PARAMETERS[p].mode==mode)CHECK(parameterStamps[p]!=0);
 }
 // Mode-switch and slow-ACK regression: fixed priorities previously starved
 // units indefinitely at 200 ms/write. Verify fair reads without relaxing any
 // native freshness guard; the wire register is only a spare-capacity diagnostic.
 for(bool constantSlow:{false,true})for(bool streaming:{false,true}){
   BackgroundPollSchedule polls;auto live=ready(100);live.loaded=false;live.fitnessMode=4;
   uint32_t clock=100,firstFieldMax=0,catalogueMax=0,refreshMax=0,criticalMax=0;
   uint32_t previous[9]{},fields[voltra::MODE_PARAMETER_COUNT]{};unsigned count[9]{},writes=0;
   const int modes[]={1,2,4,7,2,7,4,1};
   for(int cycle=0;cycle<16;++cycle){
     live.mode=modes[cycle%8];live.statusMs=clock;
     for(auto& stamp:fields)stamp=0;
     // A confirmed mode change invalidates that mode's prior field readbacks.
     // Its foreground writes defer the next background slot as in production.
     polls.writeCompleted(clock);const uint32_t changedAt=clock;
     bool first=live.mode==1,complete=first;
     while(clock-changedAt<20000){
       if(streaming){live.forceMs=live.motionMs=clock;live.activeWeightTenths=20;}
       const auto read=polls.next(live,clock,false);const unsigned index=unsigned(read);
       if(read!=BackgroundRead::None){
         const uint32_t ack=constantSlow?200:(writes*37)%201;++writes;clock+=ack;
         if(previous[index]){
           const auto elapsed=clock-previous[index];
           if(index<=3){criticalMax=criticalMax>elapsed?criticalMax:elapsed;CHECK(elapsed<3500);}
           if(index>=6)CHECK(elapsed<7000);
         }
         previous[index]=clock;++count[index];
         if(read==BackgroundRead::Settings)live.statusMs=clock;
         if(read==BackgroundRead::Endpoint)live.endpointMs=clock;
         if(read==BackgroundRead::ModeParameter){
           const int field=polls.nextModeParameter(live.mode);
           CHECK(field>=0);CHECK_EQ(voltra::MODE_PARAMETERS[field].mode,live.mode);
           if(!first){
             CHECK_EQ(field,live.mode==2?0:live.mode==4?5:7);
             first=true;const auto delay=clock-changedAt;firstFieldMax=firstFieldMax>delay?firstFieldMax:delay;
             CHECK(delay<1500);
           }
           if(fields[field]){const auto age=clock-fields[field];refreshMax=refreshMax>age?refreshMax:age;CHECK(age<8000);}
           fields[field]=clock;
           bool all=true;for(int f=0;f<voltra::MODE_PARAMETER_COUNT;++f)
             if(voltra::MODE_PARAMETERS[f].mode==live.mode)all&=fields[f]!=0;
           if(all&&!complete){complete=true;const auto elapsed=clock-changedAt;
             catalogueMax=catalogueMax>elapsed?catalogueMax:elapsed;CHECK(elapsed<8000);}
         }
         polls.writeCompleted(clock);
         CHECK_EQ(polls.next(live,clock,false),BackgroundRead::None);
       }
       clock+=20;
     }
     CHECK(first);CHECK(complete);
   }
   CHECK(count[unsigned(BackgroundRead::WeightUnit)]>=55);
   CHECK(count[unsigned(BackgroundRead::LengthUnit)]>=55);
   std::printf("poll transition slow=%d stream=%d first=%u catalogue=%u refresh=%u critical=%u ms\n",
     constantSlow,streaming,firstFieldMax,catalogueMax,refreshMax,criticalMax);
 }
 // Queue/STOP already present at selection does not consume a due poll.
 // A later arrival skips dispatch; freshness still expires normally.
 BackgroundPollSchedule schedule;auto state=ready(100);
 CHECK_EQ(schedule.next(state,100,true),BackgroundRead::None);
 CHECK_EQ(schedule.next(state,100,false),BackgroundRead::Settings);
 schedule.writeCompleted(30100); // Reproduce the captured blocking ATT timeout.
 CHECK_EQ(schedule.next(state,30100,false),BackgroundRead::None);
 CHECK_EQ(schedule.next(state,30199,false),BackgroundRead::None);
 CHECK_EQ(schedule.next(state,30200,true),BackgroundRead::None);
 CHECK_EQ(schedule.next(state,30200,false),BackgroundRead::Endpoint); // Oldest overdue read; still only one.
 CHECK_EQ(schedule.next(state,30200,false),BackgroundRead::None);
 // Native source/freshness validation, not old reps or stationary weight,
 // determines whether the independent wire-force diagnostic query is redundant.
 state=ready(1000);state.forceMs=state.motionMs=1000;state.activeWeightTenths=0;
 CHECK(freshIndependentForceStream(state,1499));CHECK(!freshIndependentForceStream(state,1500));
 state.repMs=1500;CHECK(!freshIndependentForceStream(state,1500));
 state.motionMs=0;CHECK(!freshIndependentForceStream(state,1001));
 state.motionMs=999;CHECK(!freshIndependentForceStream(state,1001));
 state.motionMs=1000;state.activeWeightTenths=-1;CHECK(!freshIndependentForceStream(state,1001));
 state.activeWeightTenths=1;state.endpoint.status=0x12;CHECK(!freshIndependentForceStream(state,1001));
 // Millis wrap neither creates a burst nor suppresses scheduling indefinitely.
 schedule={};state=ready(UINT32_MAX-50);
 CHECK_EQ(schedule.next(state,UINT32_MAX-50,false),BackgroundRead::Settings);
 schedule.writeCompleted(UINT32_MAX-20);
 CHECK_EQ(schedule.next(state,78,false),BackgroundRead::None);
 CHECK_EQ(schedule.next(state,79,false),BackgroundRead::Endpoint);
 std::printf("%d background poll checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
