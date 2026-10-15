#include "minitest.h"
#include "app/remote_model.h"
#include "link/voltra_link.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
using namespace remote;
static void endpoint(vlink::DeviceState& s,uint32_t now,bool paired=false){
 s.endpoint.valid=true;s.endpoint.status=paired?0x12:0;s.endpointMs=now;s.twinMs=now;s.twinConnection=paired?2:0;
 strcpy(s.endpoint.local,"02:00:00:00:00:01");strcpy(s.endpoint.peer,paired?"02:00:00:00:00:02":"00:00:00:00:00:00");
}
int main(){
 vlink::DeviceState discovery;strcpy(discovery.trainers[0].address,"10:20:30:40:50:60");
 CHECK(!vlink::trainerAvailable(discovery,0,500)); // Saved is not nearby.
 CHECK(vlink::canForgetTrainer(discovery,0,500)); // Offline devices can be reset.
 CHECK(!vlink::canForgetTrainer(discovery,1,500));
 CHECK(!vlink::canForgetTrainer(discovery,-1,500));
 discovery.loaded=true;CHECK(!vlink::canForgetTrainer(discovery,0,500));discovery.loaded=false;
 discovery.armed=true;CHECK(!vlink::canForgetTrainer(discovery,0,500));discovery.armed=false;
 discovery.twinPending=true;CHECK(!vlink::canForgetTrainer(discovery,0,500));discovery.twinPending=false;
 discovery.parameterSelectionPending=true;CHECK(!vlink::canForgetTrainer(discovery,0,500));discovery.parameterSelectionPending=false;
 discovery.link=vlink::LinkState::Ready;discovery.statusMs=499;discovery.fitnessMode=4;
 CHECK(vlink::canForgetTrainer(discovery,0,500));
 discovery.fitnessMode=5;CHECK(!vlink::canForgetTrainer(discovery,0,500));discovery.fitnessMode=4;
 CHECK(!vlink::canForgetTrainer(discovery,0,5000));discovery.link=vlink::LinkState::Scanning;
 discovery.discoveryPausedForTv=true;
 CHECK(vlink::trainerAvailable(discovery,0,500)); // Saved identity works after reboot with trusted TV advertising.
 CHECK(!vlink::trainerAvailable(discovery,1,500)); // Empty slots never become selectable.
 discovery.discoveryPausedForTv=false;
 discovery.trainers[0].seen=true;discovery.trainers[0].lastSeenMs=500;
 CHECK(vlink::trainerAvailable(discovery,0,10499));CHECK(!vlink::trainerAvailable(discovery,0,10500));
 discovery.selected=0;discovery.link=vlink::LinkState::Ready;
 CHECK(vlink::trainerAvailable(discovery,0,20000)); // Connected devices may stop advertising.
 discovery.link=vlink::LinkState::Scanning;CHECK(!vlink::trainerAvailable(discovery,0,20000));
 discovery.trainers[0].lastSeenMs=UINT32_MAX-100;
 CHECK(vlink::trainerAvailable(discovery,0,500));CHECK(!vlink::trainerAvailable(discovery,-1,500));CHECK(!vlink::trainerAvailable(discovery,2,500));
 vlink::DeviceState iso;iso.link=vlink::LinkState::Ready;iso.statusMs=1000;iso.mode=7;iso.fitnessMode=4;iso.twinConnection=0;iso.weightUnitRaw=1;endpoint(iso,1000);
 for(int i=7;i<=11;++i){iso.modeValueMs[i]=1000;iso.modeValues[i]=i==7?100:i<10?0:5;}
 CHECK(vlink::canLoad(iso,1001));CHECK(vlink::canEditIsokinetic(iso,1001,7));CHECK(vlink::canEditIsokinetic(iso,1001,9));CHECK(!vlink::canEditIsokinetic(iso,1001,10));
 iso.modeValues[8]=1;CHECK(vlink::canEditIsokinetic(iso,1001,10));CHECK(!vlink::canEditIsokinetic(iso,1001,9));
 iso.loaded=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));CHECK(!vlink::canLoad(iso,1001));iso.loaded=false;
 iso.mode=1;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.mode=7;
 CHECK(!vlink::canEditIsokinetic(iso,1001,6));CHECK(!vlink::canEditIsokinetic(iso,1001,12));CHECK(!vlink::canEditIsokinetic(iso,10000,7));
 iso.cableActive=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.cableActive=false;iso.armed=true;CHECK(!vlink::canLoad(iso,1001));iso.armed=false;
 iso.twinConnection=2;iso.endpointMs=0;CHECK(!vlink::canLoad(iso,1001));endpoint(iso,1000,true);CHECK(vlink::canLoad(iso,1001));

 CHECK_EQ(damperFactor(4),17);CHECK_EQ(damperFactor(9),50);CHECK_EQ(damperFactor(-1),-1);CHECK_EQ(damperFactor(10),-1);CHECK(std::strcmp(damperRoman(4),"V")==0);
 vlink::DeviceState d;d.link=vlink::LinkState::Ready;d.statusMs=1000;d.mode=4;d.fitnessMode=4;d.modeValues[5]=4;d.modeValueMs[5]=1000;d.twinConnection=2;d.twinMs=1000;d.twinRoleMs=1000;d.twinLastRole=1;endpoint(d,1000,true);CHECK(vlink::canEditDamper(d,1001));d.mode=2;CHECK(!vlink::canEditDamper(d,1001));d.mode=4;d.loaded=true;CHECK(!vlink::canEditDamper(d,1001));d.loaded=false;d.modeValues[5]=10;CHECK(!vlink::canEditDamper(d,1001));

 vlink::DeviceState band;band.link=vlink::LinkState::Ready;band.statusMs=1000;band.mode=2;band.fitnessMode=4;band.modeValueMs[0]=1000;band.weightUnitRaw=1;endpoint(band,1000);
 CHECK(vlink::canEditBand(band,1001));band.mode=1;CHECK(!vlink::canEditBand(band,1001));band.mode=2;
 band.loaded=true;CHECK(!vlink::canEditBand(band,1001));band.loaded=false;band.cableActive=true;CHECK(!vlink::canEditBand(band,1001));band.cableActive=false;
 band.weightUnitRaw=-1;CHECK(!vlink::canEditBand(band,1001));band.weightUnitRaw=0;CHECK(!vlink::canEditBand(band,10000));

  d.modeValues[5]=4;d.twinMs=1000;CHECK(vlink::canLoad(d,1001));d.modeValueMs[5]=0;CHECK(!vlink::canLoad(d,1001));d.modeValueMs[5]=1000;d.armed=true;CHECK(!vlink::canLoad(d,1001));d.armed=false;d.endpointMs=0;CHECK(!vlink::canLoad(d,1001));
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
  vlink::DeviceState s;CHECK(!vlink::canEdit(s,100));s.link=vlink::LinkState::Ready;s.mode=1;s.fitnessMode=4;s.statusMs=100;endpoint(s,100);
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
  uint8_t notification[28]={0x55,28,8,0,0,0,0,0,0,0,0x0f,0,3,0,0x86,0x3e,20,0,0x87,0x3e,10,0,0x88,0x3e,0xfb,0xff,0,0};
  auto decoded=voltra::decode(notification,sizeof(notification));CHECK_EQ(decoded.weightLbs,20);CHECK_EQ(decoded.chainsLbs,10);CHECK_EQ(decoded.eccentricLbs,-5);
  // Dashboard resistance follows the device target and explicit load state.
  // Residual cable tension/runtime load must never become ACTIVE WEIGHT.
  vlink::DeviceState reading;reading.link=vlink::LinkState::Ready;reading.statusMs=1000;
  reading.mode=1;reading.weightLbs=59;endpoint(reading,1000);
  CHECK_EQ(vlink::activeWeightReading(reading,1100).tenthsLb,-1);
  reading.runtimeWeightLbs=1;reading.runtimeWeightMs=1000;
  reading.activeWeightTenths=275;reading.forceMs=1000;
  CHECK_EQ(vlink::activeWeightReading(reading,1100).tenthsLb,-1);
  reading.fitnessMode=5;reading.loaded=true;
  CHECK_EQ(vlink::activeWeightReading(reading,1100).tenthsLb,-1); // measured is not target
  voltra::Event targetReport;targetReport.type=voltra::EventType::Telemetry;targetReport.activeWeightTenths=12;
  targetReport.hasTargetForceReport=true;targetReport.targetForceTenths=60;
  vlink::updateWeightTelemetry(reading,targetReport,1000);
  CHECK_EQ(reading.activeWeightTenths,12);CHECK_EQ(reading.targetForceTenths,60);
  CHECK_EQ(vlink::activeWeightReading(reading,1100).tenthsLb,60);
  CHECK(vlink::activeWeightReading(reading,1100).source==vlink::ActiveWeightSource::TargetForce);
  CHECK_EQ(vlink::activeWeightReading(reading,2999).tenthsLb,60);
  reading.runtimeWeightMs=3000;reading.forceMs=3000;
  CHECK_EQ(vlink::activeWeightReading(reading,3000).tenthsLb,-1); // no fallback after target expiry
  targetReport.targetForceTenths=-1;vlink::updateWeightTelemetry(reading,targetReport,1100);
  CHECK_EQ(reading.targetForceTenths,-1);CHECK_EQ(reading.targetForceMs,0u); // absent/sentinel clears cache
  targetReport.targetForceTenths=60;vlink::updateWeightTelemetry(reading,targetReport,1200);
  targetReport.targetForceTenths=10001;vlink::updateWeightTelemetry(reading,targetReport,1300);
  CHECK_EQ(reading.targetForceMs,0u);CHECK_EQ(vlink::activeWeightReading(reading,1301).tenthsLb,-1);
  targetReport.targetForceTenths=0;vlink::updateWeightTelemetry(reading,targetReport,1400);
  CHECK_EQ(vlink::activeWeightReading(reading,1401).tenthsLb,0); // zero is a valid loaded target
  targetReport.targetForceTenths=60;vlink::updateWeightTelemetry(reading,targetReport,1500);
  voltra::Event runtime;runtime.hasRuntimeWeight=true;runtime.runtimeWeightLbs=1;
  vlink::updateWeightTelemetry(reading,runtime,1600);CHECK_EQ(reading.targetForceMs,1500u);
  CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,60); // diagnostic polls cannot overwrite target
  reading.fitnessMode=4;reading.loaded=false;
  CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,0); // residual target/force ignored unloaded
  CHECK(vlink::activeWeightReading(reading,1601).source==vlink::ActiveWeightSource::Unloaded);
  for(int fitness:{-1,0,0x26,0x27,8}){
    reading.fitnessMode=fitness;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);
  }
  reading.fitnessMode=5;reading.loaded=false;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);
  reading.loaded=true;reading.armed=true;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);reading.armed=false;
  reading.fitnessMode=21;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,60);
  endpoint(reading,1500,true);CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);
  vlink::clearWeightTelemetry(reading);vlink::updateWeightTelemetry(reading,targetReport,1600);
  CHECK_EQ(reading.targetForceMs,0u); // paired streams cannot restore independent readings
  endpoint(reading,1500);vlink::updateWeightTelemetry(reading,targetReport,1600);
  reading.twinPending=true;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);reading.twinPending=false;
  reading.mode=8;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);reading.mode=1;
  reading.endpointMs=0;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);endpoint(reading,1500);
  reading.statusMs=1;CHECK_EQ(vlink::activeWeightReading(reading,3501).tenthsLb,-1);reading.statusMs=1500;
  reading.link=vlink::LinkState::Scanning;CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);
  reading.link=vlink::LinkState::Ready;
  vlink::clearWeightTelemetry(reading); // session, mode, load and topology invalidation share this path
  CHECK_EQ(reading.targetForceTenths,-1);CHECK_EQ(reading.targetForceMs,0u);
  CHECK_EQ(reading.activeWeightTenths,-1);CHECK_EQ(reading.forceMs,0u);
  CHECK_EQ(reading.runtimeWeightLbs,-1);CHECK_EQ(reading.runtimeWeightMs,0u);
  CHECK_EQ(vlink::activeWeightReading(reading,1601).tenthsLb,-1);
  // The display switches to measured force only while a fresh B4 proves motion.
  // These are presentation gates; target resistance and load controls are unchanged.
  vlink::DeviceState moving;moving.link=vlink::LinkState::Ready;
  moving.statusMs=1000;moving.loaded=true;moving.fitnessMode=5;endpoint(moving,1000);
  voltra::Event motion;motion.type=voltra::EventType::Telemetry;
  motion.activeWeightTenths=83;motion.hasMotionTelemetry=true;
  motion.cableLengthMm=500;motion.cableVelocityMmPerSecond=300;
  for(int mode:{1,2,4,7}){
    moving.mode=mode;vlink::updateWeightTelemetry(moving,motion,1000);
    CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,83);
    CHECK(vlink::movingWeightReading(moving,1001).source==vlink::ActiveWeightSource::CableForce);
  }
  motion.cableVelocityMmPerSecond=-300;vlink::updateWeightTelemetry(moving,motion,1000);
  CHECK_EQ(vlink::movingWeightReading(moving,1499).tenthsLb,83);
  CHECK_EQ(vlink::movingWeightReading(moving,1500).tenthsLb,-1);
  for(int speed:{-9,0,9}){
    motion.cableVelocityMmPerSecond=speed;vlink::updateWeightTelemetry(moving,motion,1000);
    CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,-1); // tension/extension is not movement
  }
  motion.cableVelocityMmPerSecond=10;vlink::updateWeightTelemetry(moving,motion,1000);
  CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,83);
  moving.loaded=false;moving.fitnessMode=4;CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,-1);
  moving.loaded=true;moving.fitnessMode=21;CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,83);
  moving.armed=true;CHECK_EQ(vlink::movingWeightReading(moving,1001).tenthsLb,-1);moving.armed=false;
  motion.hasMotionTelemetry=false;vlink::updateWeightTelemetry(moving,motion,1001);
  CHECK_EQ(moving.motionMs,0u);CHECK_EQ(vlink::movingWeightReading(moving,1002).tenthsLb,-1);
  motion.hasMotionTelemetry=true;vlink::updateWeightTelemetry(moving,motion,1002);
  endpoint(moving,1000,true);CHECK_EQ(vlink::movingWeightReading(moving,1003).tenthsLb,-1);
  endpoint(moving,1000);moving.twinPending=true;CHECK_EQ(vlink::movingWeightReading(moving,1003).tenthsLb,-1);moving.twinPending=false;
  moving.mode=8;CHECK_EQ(vlink::movingWeightReading(moving,1003).tenthsLb,-1);moving.mode=1;
  moving.statusMs=1;CHECK_EQ(vlink::movingWeightReading(moving,3501).tenthsLb,-1);moving.statusMs=1000;
  moving.link=vlink::LinkState::Scanning;CHECK_EQ(vlink::movingWeightReading(moving,1003).tenthsLb,-1);
  moving.link=vlink::LinkState::Ready;vlink::clearWeightTelemetry(moving);
  CHECK_EQ(moving.motionMs,0u);CHECK_EQ(moving.cableLengthMm,-1);
  CHECK_EQ(vlink::movingWeightReading(moving,1003).tenthsLb,-1);
  // Discovery is separate from both saved slots, deduplicated, and bounded.
  vlink::DeviceState discovered;strcpy(discovered.trainers[0].address,"saved-left");strcpy(discovered.trainers[1].address,"saved-right");
  CHECK_EQ(vlink::nearbySlot(discovered,"new-trainer",1000),0);
  for(int i=0;i<vlink::NEARBY_TRAINER_LIMIT;++i){auto& t=discovered.nearby[i];snprintf(t.address,sizeof(t.address),"device-%d",i);t.seen=true;t.lastSeenMs=1000;}
  CHECK_EQ(vlink::nearbySlot(discovered,"DEVICE-4",1001),4);
  CHECK_EQ(vlink::nearbySlot(discovered,"extra",1001),-1); // don't evict a fresh visible row
  discovered.nearby[2].lastSeenMs=1;CHECK_EQ(vlink::nearbySlot(discovered,"extra",10002),2);
  discovered.discoveryPausedForTv=true;CHECK(!vlink::trainerNearby(discovered,0,1000));
  CHECK(vlink::trainerAvailable(discovered,0,1000)); // saved reconnect is not Nearby
  discovered.selected=0;discovered.link=vlink::LinkState::Ready;discovered.statusMs=1000;
  CHECK(vlink::trainerNearby(discovered,0,1100));CHECK(!vlink::trainerNearby(discovered,0,5000));
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
  vlink::DeviceState twin;twin.link=vlink::LinkState::Ready;twin.statusMs=100;twin.twinMs=100;twin.twinConnection=2;twin.selected=0;twin.trainers[1].address[0]='a';twin.trainers[0].address[0]='b';twin.twinLastRole=1;twin.twinRoleMs=100;endpoint(twin,100,true);strcpy(twin.trainers[1].address,twin.endpoint.peer);
  CHECK(vlink::twinActive(twin,200));
  for(int role:{-1,0,1,2}){twin.twinLastRole=role;CHECK(vlink::twinControlReady(twin,200));CHECK(!vlink::pairedFollower(twin,0,200));CHECK(vlink::pairedFollower(twin,1,200));}
  CHECK(!vlink::twinControlReady(twin,4000));endpoint(twin,100);CHECK(vlink::twinControlReady(twin,200));
  twin.endpointMs=0;CHECK(!vlink::twinControlReady(twin,200));
  endpoint(twin,100,true);twin.twinPending=true;CHECK(!vlink::twinControlReady(twin,200));
  std::printf("%d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
