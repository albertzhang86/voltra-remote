#include "minitest.h"
#include "app/remote_model.h"
#include "link/voltra_link.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
using namespace remote;
int main(){
 vlink::DeviceState discovery;strcpy(discovery.trainers[0].address,"10:20:30:40:50:60");
 CHECK(!vlink::trainerAvailable(discovery,0,500)); // Saved is not nearby.
 discovery.trainers[0].seen=true;discovery.trainers[0].lastSeenMs=500;
 CHECK(vlink::trainerAvailable(discovery,0,10499));CHECK(!vlink::trainerAvailable(discovery,0,10500));
 discovery.selected=0;discovery.link=vlink::LinkState::Ready;
 CHECK(vlink::trainerAvailable(discovery,0,20000)); // Connected devices may stop advertising.
 discovery.link=vlink::LinkState::Scanning;CHECK(!vlink::trainerAvailable(discovery,0,20000));
 discovery.trainers[0].lastSeenMs=UINT32_MAX-100;
 CHECK(vlink::trainerAvailable(discovery,0,500));CHECK(!vlink::trainerAvailable(discovery,-1,500));CHECK(!vlink::trainerAvailable(discovery,2,500));
 vlink::DeviceState iso;iso.link=vlink::LinkState::Ready;iso.statusMs=1000;iso.mode=7;iso.fitnessMode=4;iso.twinConnection=0;iso.weightUnitRaw=1;
 for(int i=7;i<=11;++i){iso.modeValueMs[i]=1000;iso.modeValues[i]=i==7?100:i<10?0:5;}
 CHECK(vlink::canLoad(iso,1001));CHECK(vlink::canEditIsokinetic(iso,1001,7));CHECK(vlink::canEditIsokinetic(iso,1001,9));CHECK(!vlink::canEditIsokinetic(iso,1001,10));
 iso.modeValues[8]=1;CHECK(vlink::canEditIsokinetic(iso,1001,10));CHECK(!vlink::canEditIsokinetic(iso,1001,9));
 iso.loaded=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));CHECK(!vlink::canLoad(iso,1001));iso.loaded=false;
 iso.mode=1;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.mode=7;
 CHECK(!vlink::canEditIsokinetic(iso,1001,6));CHECK(!vlink::canEditIsokinetic(iso,1001,12));CHECK(!vlink::canEditIsokinetic(iso,10000,7));
 iso.cableActive=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.cableActive=false;iso.armed=true;CHECK(!vlink::canLoad(iso,1001));iso.armed=false;
 iso.twinConnection=2;CHECK(!vlink::canLoad(iso,1001));iso.twinMs=1000;CHECK(vlink::canLoad(iso,1001));

 CHECK_EQ(damperFactor(4),17);CHECK_EQ(damperFactor(9),50);CHECK_EQ(damperFactor(-1),-1);CHECK_EQ(damperFactor(10),-1);CHECK(std::strcmp(damperRoman(4),"V")==0);
 vlink::DeviceState d;d.link=vlink::LinkState::Ready;d.statusMs=1000;d.mode=4;d.fitnessMode=4;d.modeValues[5]=4;d.modeValueMs[5]=1000;d.twinConnection=2;d.twinMs=1000;d.twinRoleMs=1000;d.twinLastRole=1;CHECK(vlink::canEditDamper(d,1001));d.mode=2;CHECK(!vlink::canEditDamper(d,1001));d.mode=4;d.loaded=true;CHECK(!vlink::canEditDamper(d,1001));d.loaded=false;d.modeValues[5]=10;CHECK(!vlink::canEditDamper(d,1001));

 vlink::DeviceState band;band.link=vlink::LinkState::Ready;band.statusMs=1000;band.mode=2;band.fitnessMode=4;band.modeValueMs[0]=1000;band.weightUnitRaw=1;
 CHECK(vlink::canEditBand(band,1001));band.mode=1;CHECK(!vlink::canEditBand(band,1001));band.mode=2;
 band.loaded=true;CHECK(!vlink::canEditBand(band,1001));band.loaded=false;band.cableActive=true;CHECK(!vlink::canEditBand(band,1001));band.cableActive=false;
 band.weightUnitRaw=-1;CHECK(!vlink::canEditBand(band,1001));band.weightUnitRaw=0;CHECK(!vlink::canEditBand(band,10000));

  d.modeValues[5]=4;d.twinMs=1000;CHECK(vlink::canLoad(d,1001));d.modeValueMs[5]=0;CHECK(!vlink::canLoad(d,1001));d.modeValueMs[5]=1000;d.armed=true;CHECK(!vlink::canLoad(d,1001));d.armed=false;d.twinMs=0;CHECK(!vlink::canLoad(d,1001));
  CHECK(std::abs(kilograms(100)-45.359237)<0.000001);
  for(int v=5;v<=200;++v){CHECK(kgStep(v,1,5,200)>=v);CHECK(kgStep(v,-1,5,200)<=v);CHECK(kgStep(v,1,5,200)<=200);CHECK(kgStep(v,-1,5,200)>=5);}
  CHECK_EQ(displayKilograms(93),42.5);
  CHECK_EQ(displayKilograms(kgStep(93,1,5,200)),43.0);
  CHECK_EQ(displayKilograms(kgStep(93,-1,5,200)),42.0);
  CHECK_EQ(displayKilograms(82),37.5);
  CHECK_EQ(displayKilograms(83),37.5); // Same displayed step; do not send duplicate kg steps.
  CHECK_EQ(displayKilograms(kgStep(82,1,5,200)),38.0);
  CHECK_EQ(displayKilograms(kgStep(83,-1,5,200)),37.0);
  for(int half=5;half<=182;++half){
    const int encoded=(half*11+5)/10;
    CHECK_EQ(displayHalfKilograms(encoded),half);
    if(half<182)CHECK_EQ(displayHalfKilograms(kgStep(encoded,1,5,200)),half+1);
    if(half>5)CHECK_EQ(displayHalfKilograms(kgStep(encoded,-1,5,200)),half-1);
  }
  CHECK_EQ(displayKilograms(61),27.5);CHECK_EQ(displayKilograms(62),28.0);
  CHECK_EQ(displayKilograms(kgStep(61,1,5,200)),28.0);
  CHECK_EQ(displayKilograms(kgStep(61,-1,5,200)),27.0);
  Editor e;e.turn(1,true,false,100,5,200);CHECK(!e.known);CHECK(!e.pending);
  e.observe(20);e.turn(1,false,false,100,5,200);CHECK_EQ(e.target,20);
  e.turn(1,true,false,100,5,200);CHECK(e.pending);int target=e.target;
  CHECK(!e.due(349));CHECK(e.due(350));e.submitted(350);
  e.observe(20);CHECK_EQ(e.target,target);CHECK(e.pending);
  e.turn(1,true,false,400,5,200);CHECK_EQ(e.target,target); // in-flight command cannot be overwritten
  e.observe(target);CHECK(!e.pending);CHECK(!e.sent);
  e.turn(1,true,false,500,5,200);e.submitted(750);e.tick(5749);CHECK(e.pending);e.tick(5750);CHECK(!e.pending);CHECK(e.timedOut);CHECK_EQ(e.target,target);
  e.reset();CHECK(!e.known);CHECK(!e.pending);CHECK(!e.sent); // trainer/session boundary
  e.observe(-190);e.turn(-1,true,true,100, -195,195);CHECK_EQ(e.target,-195);e.turn(-1,true,true,101,-195,195);CHECK_EQ(e.target,-195);
  e.reset();e.observe(200);e.turn(1,true,false,100,5,200);CHECK(!e.pending);
  vlink::DeviceState s;CHECK(!vlink::canEdit(s,100));s.link=vlink::LinkState::Ready;s.mode=1;s.fitnessMode=4;s.statusMs=100;
  CHECK(vlink::canEdit(s,100));CHECK(!vlink::canEdit(s,3600));s.loaded=true;CHECK(!vlink::canEdit(s,100));s.loaded=false;s.armed=true;CHECK(!vlink::canEdit(s,100));s.armed=false;s.mode=2;CHECK(!vlink::canEdit(s,100));s.mode=1;s.fitnessMode=-1;CHECK(!vlink::canEdit(s,100));
  s.fitnessMode=4;s.mode=1;s.loaded=false;s.armed=false;s.statusMs=100;
  CHECK(vlink::canSwitch(s,100));s.controlBusyUntil=500;CHECK(!vlink::canSwitch(s,100));CHECK(vlink::canSwitch(s,501));s.controlBusyUntil=0;
  s.fitnessMode=5;CHECK(!vlink::canSwitch(s,100));s.fitnessMode=0x27;CHECK(!vlink::canSwitch(s,100));s.fitnessMode=4;CHECK(!vlink::canSwitch(s,4000));
  CHECK(!vlink::canLoad(s,100));s.weightLbs=20;s.chainsLbs=0;s.eccentricLbs=0;s.settingsMs=100;CHECK(vlink::canLoad(s,100));CHECK(!vlink::canLoad(s,4000));
  s.fitnessMode=5;s.loaded=true;
  CHECK(vlink::canEditParameter(s,100,vlink::Parameter::Weight));
  CHECK(!vlink::canEditParameter(s,100,vlink::Parameter::Chains));CHECK(!vlink::canLoad(s,100));
  CHECK(!vlink::canEditParameter(s,4000,vlink::Parameter::Weight));
  s.armed=true;CHECK(!vlink::canEditParameter(s,100,vlink::Parameter::Weight));
  voltra::Frame f;
  for(int mode:{0,1,2,3,4,6,7,8}){CHECK(voltra::modeFrame(mode,f));CHECK(voltra::validFrame(f.data,f.len));CHECK_EQ(f.data[15],mode);}
  CHECK(!voltra::modeFrame(5,f));
  for(int v=5;v<=200;++v){CHECK(voltra::weightFrame(v,f));CHECK(voltra::validFrame(f.data,f.len));}
  for(int v=0;v<=100;++v){CHECK(voltra::chainsFrame(v,f));CHECK(voltra::validFrame(f.data,f.len));CHECK_EQ(f.data[15]|(f.data[16]<<8),v);}
  for(int v=-195;v<=195;++v){CHECK(voltra::eccentricFrame(v,f));CHECK(voltra::validFrame(f.data,f.len));CHECK_EQ(static_cast<int16_t>(f.data[15]|(f.data[16]<<8)),v);}
  CHECK(!voltra::chainsFrame(-1,f));CHECK(!voltra::chainsFrame(101,f));CHECK(!voltra::eccentricFrame(-196,f));CHECK(!voltra::eccentricFrame(196,f));
  f=voltra::readSettingsFrame();CHECK(voltra::validFrame(f.data,f.len));
  uint8_t bytes[255];std::memcpy(bytes,f.data,f.len);bytes[15]^=1;CHECK(!voltra::validFrame(bytes,f.len));CHECK(!voltra::validFrame(f.data,f.len-1));CHECK(!voltra::validFrame(nullptr,0));
  // Device notification: weight, chains, signed eccentric. Unknown/short fields do not authorize commands.
  uint8_t notification[27]={0x55,27,8,0,0,0,0,0,0,0,0x0f,0,3,0,0x86,0x3e,20,0,0x87,0x3e,10,0,0x88,0x3e,0xfb,0xff,0};
  auto decoded=voltra::decode(notification,sizeof(notification));CHECK_EQ(decoded.weightLbs,20);CHECK_EQ(decoded.chainsLbs,10);CHECK_EQ(decoded.eccentricLbs,-5);
  // Shared chain weight is attributed only to the direction selected by the trainer.
  vlink::DeviceState chains;chains.chainsLbs=2;chains.inverseChainsEnabled=1;
  CHECK_EQ(vlink::chainWeight(chains,false),0);CHECK_EQ(vlink::chainWeight(chains,true),2);
  CHECK_EQ(remote::displayKilograms(vlink::chainWeight(chains,true)),1.0);
  chains.inverseChainsEnabled=0;CHECK_EQ(vlink::chainWeight(chains,false),2);CHECK_EQ(vlink::chainWeight(chains,true),0);
  chains.inverseChainsEnabled=-1;CHECK_EQ(vlink::chainWeight(chains,true),-1);
  for(int pounds=0;pounds<=195;++pounds)CHECK_EQ(remote::displayKilograms(-pounds),-remote::displayKilograms(pounds));
  CHECK_EQ(remote::kgStep(0,-1,-195,195),-1);CHECK_EQ(remote::kgStep(-1,1,-195,195),0);
  CHECK_EQ(remote::displayWeightKilograms(179,true),163.0);
  CHECK_EQ(remote::displayWeightKilograms(179,false),81.5);
  vlink::DeviceState twin;twin.link=vlink::LinkState::Ready;twin.statusMs=100;twin.twinMs=100;twin.twinConnection=2;twin.selected=0;twin.trainers[1].address[0]='a';twin.trainers[0].address[0]='b';twin.twinLastRole=1;twin.twinRoleMs=100;
  CHECK(vlink::twinActive(twin,200));
  for(int role:{-1,0,1,2}){twin.twinLastRole=role;CHECK(vlink::twinControlReady(twin,200));CHECK(!vlink::pairedFollower(twin,0,200));CHECK(!vlink::pairedFollower(twin,1,200));}
  CHECK(!vlink::twinControlReady(twin,4000));twin.twinConnection=0;CHECK(vlink::twinControlReady(twin,200));
  std::printf("%d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
