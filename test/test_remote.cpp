#include "minitest.h"
#include "app/remote_model.h"
#include "link/voltra_link.h"
#include "link/poll_write_policy.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
using namespace remote;
static void endpoint(vlink::DeviceState& s,uint32_t now,bool paired=false){
 s.endpoint.valid=true;s.endpoint.status=paired?0x12:0;s.endpointMs=now;s.twinMs=now;s.twinConnection=paired?2:0;
 strcpy(s.endpoint.local,"02:00:00:00:00:01");strcpy(s.endpoint.peer,paired?"02:00:00:00:00:02":"00:00:00:00:00:00");
}
int main(){
 for(int maximum:{100,200,229,230,250}){
   vlink::DeviceState s;s.maximumForceLbs=maximum;s.eccentricLbs=0;s.weightLbs=maximum;
   CHECK_EQ(vlink::maximumWeight(s),maximum);
   CHECK(vlink::validWeightSetting(s,vlink::Parameter::Weight,maximum));
   CHECK(!vlink::validWeightSetting(s,vlink::Parameter::Weight,maximum+1));
   CHECK(vlink::validWeightSetting(s,vlink::Parameter::Eccentric,5-maximum));
   CHECK(!vlink::validWeightSetting(s,vlink::Parameter::Eccentric,1));
 }
 {vlink::DeviceState s;CHECK_EQ(vlink::maximumWeight(s),200);s.maximumForceLbs=65535;CHECK_EQ(vlink::maximumWeight(s),200);}
 {Editor pounds;pounds.observe(61);pounds.turn(1,true,false,100,5,229,true,true);CHECK_EQ(pounds.target,62);}

 // Authenticated motor requests share native controller/cable guards across
 // all four modes, including confirmed Twin. No cached cable authorizes GO.
 for(bool paired:{false,true})for(int mode:{1,2,4,7})for(int ready:{0,4,0x0104}){
   using vlink::MotorFailure;
   vlink::DeviceState s;s.session=12;s.selected=0;s.link=vlink::LinkState::Ready;
   s.mode=mode;s.fitnessMode=ready;s.modeMs=s.statusMs=s.settingsMs=1000;s.weightLbs=5;s.chainsLbs=s.eccentricLbs=0;s.weightUnitRaw=1;
   s.inverseChainsEnabled=0;for(auto& stamp:s.weightSettingMs)stamp=1000;
   s.modeValues[0]=15;s.modeValueMs[0]=1000;s.modeValues[5]=0;s.modeValueMs[5]=1000;
   s.modeValues[7]=100;s.modeValueMs[7]=1000;
   for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(voltra::MODE_PARAMETERS[i].mode==mode)s.modeValueMs[i]=1000;
   s.cableCm=s.cableOffsetCm=0;s.cableMs=s.cableOffsetMs=1000;
   endpoint(s,1000,paired);strcpy(s.trainers[0].address,s.endpoint.local);
   const char* peer=paired?"02:00:00:00:00:02":"";
   CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::None);
   CHECK(vlink::tvMotorFailure(s,false,1001)==MotorFailure::None);
   const auto expected=vlink::motorConfiguration(s,1001);CHECK(expected.valid);
   CHECK(vlink::motorConfigurationFailure(s,expected,1001)==MotorFailure::None);
   CHECK(vlink::motorConfigurationFailure(s,expected,1001,1001)==MotorFailure::ConfigurationUnknown);
   for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(vlink::motorConfigurationField(mode,i)){
     int* values[]={&s.weightLbs,&s.chainsLbs,&s.inverseChainsEnabled,&s.eccentricLbs};
     int& changed=mode==1?*values[i]:s.modeValues[i];
     const int previous=changed;changed=previous+1;
     CHECK(vlink::motorConfigurationFailure(s,expected,1001)==MotorFailure::ConfigurationChanged);
     changed=previous;
     uint32_t& stamp=mode==1?s.weightSettingMs[i]:s.modeValueMs[i];stamp=0;
     CHECK(vlink::motorConfigurationFailure(s,expected,1001)==MotorFailure::ConfigurationUnknown);
     CHECK(vlink::tvMotorFailure(s,true,1001)!=MotorFailure::None);
     CHECK(vlink::tvMotorFailure(s,false,1001)==MotorFailure::None);
     stamp=1000;
   }
   for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(vlink::motorConfigurationField(mode,i))
     (mode==1?s.weightSettingMs[i]:s.modeValueMs[i])=1100;
   CHECK(vlink::motorConfigurationFailure(s,expected,1101,1001)==MotorFailure::None);
   for(auto& stamp:s.weightSettingMs)stamp=1000;
   for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(voltra::MODE_PARAMETERS[i].mode==mode)s.modeValueMs[i]=1000;
   CHECK(vlink::motorEndpointMatches(s,12,mode,peer,1001));
   CHECK(!vlink::motorEndpointMatches(s,13,mode,peer,1001));
   CHECK(!vlink::motorEndpointMatches(s,12,0,peer,1001));
   CHECK(!vlink::motorEndpointMatches(s,12,mode,"02:00:00:00:00:03",1001));
   s.motorPending=true;CHECK(!vlink::canLoad(s,1001));CHECK(!vlink::canSwitch(s,1001));
   CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::None); // Owning worker still rechecks safety.
   s.motorPending=false;
   s.cableCm=1;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::None);
   s.cableCm=2;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::CableNotZero);
   CHECK(vlink::tvMotorFailure(s,false,1001)==MotorFailure::None); // Unload never needs cable zero.
   s.cableOffsetCm=10;s.cableCm=11;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::None);
   s.cableCm=12;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::CableNotZero);
   s.cableCm=s.cableOffsetCm=0;s.cableMs=0;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::CableUnknown);
   s.cableMs=1000;s.cableOffsetMs=0;CHECK(vlink::tvMotorFailure(s,true,1001)==MotorFailure::CableUnknown);
   s.cableOffsetMs=1000;CHECK(vlink::tvMotorFailure(s,true,2500)==MotorFailure::CableUnknown);
   CHECK(!vlink::motorSetupReadback(s,1000,1000,1001));
   s.cableMs=1010;CHECK(!vlink::motorSetupReadback(s,1000,1000,1001));
   s.cableOffsetMs=1010;CHECK(vlink::motorSetupReadback(s,1000,1000,1001));
   CHECK(!vlink::motorSetupReadback(s,999,999,1011));
   s.fitnessMode=5;s.loaded=true;
   CHECK(vlink::tvMotorFailure(s,true,1011)==MotorFailure::State);
   CHECK(vlink::tvMotorFailure(s,false,1011)==MotorFailure::None);
   CHECK(!vlink::motorConfirmed(s,true,1000,1001,1011)); // Pre-command status is not confirmation.
   s.statusMs=1011;CHECK(vlink::motorConfirmed(s,true,1000,1001,1012));
   CHECK(!vlink::motorConfirmed(s,false,1000,1001,1012));
   CHECK(!vlink::motorConfirmed(s,true,1000,1012,1013));
   s.fitnessMode=ready;s.loaded=false;CHECK(vlink::motorConfirmed(s,false,1000,1001,1012));
   s.fitnessMode=0x26;s.armed=true;CHECK(!vlink::motorConfirmed(s,false,1000,1001,1012));
   CHECK(!vlink::motorConfirmed(s,true,1000,1001,1012));
   s.fitnessMode=0x0204;s.armed=false;CHECK(vlink::tvMotorFailure(s,false,1012)==MotorFailure::State);
   s.fitnessMode=ready;s.endpointMs=0;CHECK(vlink::tvMotorFailure(s,true,1012)==MotorFailure::State);

   // A Twin transition may have no cable-zero cache, or a slow background
   // read may be older than 1.5 s. Both may enter the authenticated read-only
   // preparation phase. Only fresh SETUP replies can pass the GO commit gate.
   for(bool missing:{false,true}){
     s.modeMs=s.statusMs=s.settingsMs=3000;endpoint(s,3000,paired);
     s.cableCm=s.cableOffsetCm=0;s.cableMs=s.cableOffsetMs=missing?0:1000;
     for(auto& stamp:s.weightSettingMs)stamp=3000;
     for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)
       if(voltra::MODE_PARAMETERS[i].mode==mode)s.modeValueMs[i]=3000;
     CHECK(vlink::tvMotorPreparationFailure(s,true,3001)==MotorFailure::None);
     CHECK(vlink::tvMotorFailure(s,true,3001)==MotorFailure::CableUnknown);
     const auto config=vlink::motorConfiguration(s,3001);
     const auto priorCable=s.cableMs,priorOffset=s.cableOffsetMs;
     const uint32_t queriedAt=3002;
     auto commit=[&](uint32_t now){return vlink::motorLoadCommitFailure(s,12,mode,peer,config,
       priorCable,priorOffset,queriedAt,now);};
     CHECK(commit(3003)==MotorFailure::CableUnknown); // No SETUP response, no GO.
     s.cableMs=3010;CHECK(commit(3011)==MotorFailure::CableUnknown);
     s.cableOffsetMs=3010;CHECK(commit(3011)==MotorFailure::ConfigurationUnknown);
     for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(vlink::motorConfigurationField(mode,i))
       (mode==1?s.weightSettingMs[i]:s.modeValueMs[i])=3010;
     CHECK(commit(3011)==MotorFailure::None);
     s.cableCm=2;CHECK(commit(3011)==MotorFailure::CableNotZero);
     // Extended cable does not prevent a read-only query, but still blocks GO.
     CHECK(vlink::tvMotorPreparationFailure(s,true,3011)==MotorFailure::None);
     s.cableCm=0;s.session=13;CHECK(commit(3011)==MotorFailure::EndpointChanged);s.session=12;
     s.mode=0;CHECK(commit(3011)==MotorFailure::EndpointChanged);s.mode=mode;
     s.endpoint.valid=false;CHECK(commit(3011)==MotorFailure::EndpointChanged);endpoint(s,3000,paired);
     if(paired){strcpy(s.endpoint.peer,"02:00:00:00:00:03");
       CHECK(commit(3011)==MotorFailure::EndpointChanged);endpoint(s,3000,true);}
     int* weightValues[]={&s.weightLbs,&s.chainsLbs,&s.inverseChainsEnabled,&s.eccentricLbs};
     for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i)if(vlink::motorConfigurationField(mode,i)){
       int& actual=mode==1?*weightValues[i]:s.modeValues[i];const int before=actual;
       actual=before+1;CHECK(commit(3011)!=MotorFailure::None);actual=before;
     }
     s.cableMs=s.cableOffsetMs=1000;CHECK(commit(3011)==MotorFailure::CableUnknown);
   }
 }
 // A failed poll during streaming must not manufacture a trainer disconnect.
 // Use the same request classifier and failure budget as the BLE worker.
 {
   using vlink::trainerReadRequest;
   for(auto read:{voltra::readSettingsFrame(),voltra::readRuntimeWireWeightFrame(),
       voltra::readWeightUnitFrame(),voltra::readLengthUnitFrame(),voltra::readCableFrame(),
       voltra::readTelemetryRateFrame(),voltra::readTelemetrySubscriptionFrame(),
       voltra::readGuidedLoadStatusFrame(),voltra::readTwinFrame()})CHECK(trainerReadRequest(read));
   uint8_t endpointBytes[13];CHECK(trainerReadRequest(voltra::endpointFrame(endpointBytes)));
   for(int i=0;i<voltra::MODE_PARAMETER_COUNT;++i){
     uint8_t bytes[17];voltra::Frame read;CHECK(voltra::readModeParameterFrame(i,bytes,read));
     CHECK(trainerReadRequest(read));
   }
   for(auto control:{voltra::authFrame(),voltra::initFrame(0),voltra::initFrame(1),
       voltra::telemetrySubscribeAllFrame(),voltra::setupFrame(),voltra::goFrame(),
       voltra::stopFrame(),voltra::modeIdleFrame(),voltra::modeWeightTrainingFrame(),
       voltra::cableFrame(true),voltra::cableFrame(false)})CHECK(!trainerReadRequest(control));
   voltra::Frame weight;CHECK(voltra::weightFrame(5,weight));CHECK(!trainerReadRequest(weight));
   uint8_t twinBytes[20];voltra::Frame twin;
   CHECK(voltra::twinFrame(1,"02:00:00:00:00:02",1,twinBytes,twin));CHECK(!trainerReadRequest(twin));
   CHECK(!trainerReadRequest({nullptr,0}));CHECK(!trainerReadRequest({endpointBytes,12}));
   uint8_t malformed[13];std::memcpy(malformed,endpointBytes,sizeof(malformed));
   malformed[10]=0x0f;voltra::sealTwinFrame(malformed,sizeof(malformed));
   CHECK(!trainerReadRequest({malformed,sizeof(malformed)}));

   vlink::PollWritePolicy policy;
   CHECK(policy.mayAttempt(true,100));
   CHECK(!policy.completed(true,false,true,100));CHECK_EQ(policy.failures,1u);
   CHECK(!policy.mayAttempt(true,349));CHECK(policy.mayAttempt(true,350));
   CHECK(policy.mayAttempt(false,101)); // Unload/control is never delayed by poll recovery.
   CHECK_EQ(policy.failures,1u); // Skipped burst reads cannot spend the error budget.
   CHECK(!policy.completed(true,true,true,350));CHECK_EQ(policy.failures,0u);
   CHECK(policy.mayAttempt(true,351));

   const uint32_t attempts[]={1000,1250,1750,2750,3750};
   for(unsigned i=0;i<vlink::PollWritePolicy::FAILURE_LIMIT;++i){
     CHECK(policy.mayAttempt(true,attempts[i]));
     CHECK_EQ(policy.completed(true,false,true,attempts[i]),i==4);
     CHECK_EQ(policy.failures,i+1);
     CHECK(!policy.mayAttempt(true,attempts[i]+1));
   }
   policy={};CHECK(policy.completed(false,false,true,100));CHECK_EQ(policy.failures,0u);
   // A true BLE loss immediately wins, even after a successful write callback.
   policy={};CHECK(policy.completed(true,false,false,100));
   policy={};CHECK(policy.completed(true,true,false,100));
   policy={};CHECK(!policy.completed(true,false,true,UINT32_MAX-100));
   CHECK(!policy.mayAttempt(true,148));CHECK(policy.mayAttempt(true,149));
   policy={};CHECK(policy.mayAttempt(true,0));CHECK_EQ(policy.failures,0u); // New trainer session.

   // Use no-response only for an explicitly supported, MTU-sized read poll.
   // Controls, setup queries and unsupported/oversized polls keep ATT response.
   const auto endpointRead=voltra::endpointFrame(endpointBytes);
   CHECK(!vlink::trainerWriteResponse(endpointRead,true,true,23));
   CHECK(vlink::trainerWriteResponse(endpointRead,true,false,23));
   CHECK(vlink::trainerWriteResponse(endpointRead,true,true,15));
   CHECK(vlink::trainerWriteResponse(endpointRead,true,true,2));
   CHECK(!vlink::trainerWriteResponse(endpointRead,false,true,23));
   CHECK(vlink::trainerReadHostQueued(endpointRead,true,23));
   CHECK(!vlink::trainerReadHostQueued(endpointRead,false,23));
   CHECK(!vlink::trainerReadHostQueued(endpointRead,true,15));
   CHECK(!vlink::trainerReadHostQueued(endpointRead,true,2));
   for(auto control:{voltra::authFrame(),voltra::setupFrame(),voltra::goFrame(),voltra::stopFrame(),weight,twin})
     CHECK(!vlink::trainerReadHostQueued(control,true,247));
   for(auto control:{voltra::authFrame(),voltra::setupFrame(),voltra::goFrame(),voltra::stopFrame(),weight,twin})
     CHECK(vlink::trainerWriteResponse(control,true,true,247));
   // Reproduce the captured ATT timeout: response waits 30 seconds despite
   // notifications continuing. The production wrapper avoids that transaction
   // for capable read polls, but never changes a control write into a poll.
   for(bool noResponse:{false,true})for(bool control:{false,true}){
     uint32_t clock=100;unsigned attempts=0;bool chosenResponse=false;policy={};
     const auto frame=control?voltra::stopFrame():endpointRead;
     const auto result=vlink::writeTrainerRequest(policy,frame,true,noResponse,247,[&]{return clock;},[]{return true;},
       [&](bool response){++attempts;chosenResponse=response;if(response)clock+=30000;return !response;});
     CHECK_EQ(attempts,1u);CHECK_EQ(chosenResponse,control||!noResponse);
     CHECK_EQ(result.response,chosenResponse);CHECK_EQ(result.elapsedMs,chosenResponse?30000u:0u);
     CHECK_EQ(result.accepted,!chosenResponse);CHECK_EQ(result.reconnect,control);
   }
   policy={};
   // Deferring polls never refreshes measurements or grants stale controls.
   vlink::DeviceState live;live.link=vlink::LinkState::Ready;live.statusMs=1000;
   live.mode=1;live.fitnessMode=5;live.loaded=true;endpoint(live,1000);
   live.motionMs=live.forceMs=1000;live.activeWeightTenths=60;live.cableVelocityMmPerSecond=100;
   CHECK_EQ(vlink::movingWeightReading(live,1001).tenthsLb,60);
   CHECK_EQ(vlink::movingWeightReading(live,1500).tenthsLb,-1);
   CHECK(!vlink::fresh(live,4500));CHECK(!vlink::canLoad(live,4500));

   // Fault injection drives the very same sender wrapper used by send().
   uint32_t now=100;unsigned writes=0;bool connected=true,writeSucceeds=false,loseDuringWrite=false;
   policy={};
   const auto send=[&](voltra::Frame frame){
     return vlink::writeTrainerRequest(policy,frame,true,true,247,[&]{return now;},[&]{return connected;},[&](bool){
       ++writes;if(loseDuringWrite)connected=false;return writeSucceeds;
     });
   };
   auto result=send(voltra::readSettingsFrame());
   CHECK(result.attempted);CHECK(!result.accepted);CHECK(!result.reconnect);CHECK_EQ(writes,1u);
   for(auto read:{voltra::readWeightUnitFrame(),voltra::readLengthUnitFrame(),voltra::readCableFrame()}){
     result=send(read);CHECK(!result.attempted);CHECK(!result.accepted);CHECK(!result.reconnect);
   }
   CHECK_EQ(writes,1u);CHECK_EQ(policy.failures,1u);
   now=350;writeSucceeds=true;result=send(voltra::readSettingsFrame());
   CHECK(result.accepted);CHECK_EQ(writes,2u);CHECK_EQ(policy.failures,0u);
   writeSucceeds=false;result=send(voltra::readSettingsFrame());CHECK(!result.reconnect);
   const auto beforeControl=writes;result=send(voltra::stopFrame());
   CHECK(result.attempted);CHECK(!result.accepted);CHECK(result.reconnect);CHECK_EQ(writes,beforeControl+1);
   connected=false;const auto beforeDisconnected=writes;result=send(voltra::readSettingsFrame());
   CHECK(!result.attempted);CHECK(result.reconnect);CHECK_EQ(writes,beforeDisconnected);
   policy={};connected=true;writeSucceeds=true;loseDuringWrite=true;
   result=send(voltra::readSettingsFrame());CHECK(result.attempted);CHECK(!result.accepted);CHECK(result.reconnect);
 }
 // Revoking a queued TV selection must not leave all subsequent controls busy.
 vlink::DeviceState discarded;discarded.session=7;discarded.parameterSelectionPending=true;discarded.controlBusyUntil=5000;
 vlink::discardTvCommand(discarded,6,true,false,true);
 CHECK(discarded.parameterSelectionPending);CHECK_EQ(discarded.controlBusyUntil,5000u);
 vlink::discardTvCommand(discarded,7,true,false,false);
 CHECK(!discarded.parameterSelectionPending);CHECK_EQ(discarded.controlBusyUntil,5000u); // Following local command keeps busy ownership.
 discarded.parameterSelectionPending=true;
 vlink::discardTvCommand(discarded,7,true,false,true);
 CHECK(!discarded.parameterSelectionPending);CHECK_EQ(discarded.controlBusyUntil,0u);
 discarded.twinPending=true;discarded.twinSucceeded=true;discarded.controlBusyUntil=5000;
 vlink::discardTvCommand(discarded,7,false,true,true);
 CHECK(!discarded.twinPending);CHECK(!discarded.twinSucceeded);CHECK_EQ(discarded.twinCompleted,1u);CHECK_EQ(discarded.controlBusyUntil,0u);
 // P0 admission matrix: both captured ready words work for every training
 // mode, independently and in Twin. Unknown/loaded/armed/stale states stay shut.
 for(bool paired:{false,true})for(int mode:{1,2,4,7})for(int ready:{0,4,0x0104}){
   vlink::DeviceState state;state.link=vlink::LinkState::Ready;state.modeMs=state.statusMs=state.settingsMs=1000;
   state.mode=mode;state.fitnessMode=ready;state.weightLbs=5;state.chainsLbs=state.eccentricLbs=0;
   state.weightUnitRaw=1;endpoint(state,1000,paired);
   state.modeValues[0]=15;state.modeValueMs[0]=1000;
   state.modeValues[5]=0;state.modeValueMs[5]=1000;
   state.modeValues[7]=100;state.modeValueMs[7]=1000;
   CHECK(vlink::canSwitch(state,1001));CHECK(vlink::canLoad(state,1001));
   if(mode==1)CHECK(vlink::canEdit(state,1001));
   if(mode==2)CHECK(vlink::canEditBand(state,1001));
   if(mode==4)CHECK(vlink::canEditDamper(state,1001));
   if(mode==7)CHECK(vlink::canEditIsokinetic(state,1001,7));
   // Firmware 2.0 unloaded report permits preparation only with a fresh mode.
   state.fitnessMode=0;
   CHECK(vlink::canLoad(state,1001));
   state.modeMs=0;CHECK(!vlink::canLoad(state,1001));state.modeMs=1000;
   if(mode==1)CHECK(vlink::canEdit(state,1001));
   if(mode==2)CHECK(vlink::canEditBand(state,1001));
   if(mode==4)CHECK(vlink::canEditDamper(state,1001));
   if(mode==7)CHECK(vlink::canEditIsokinetic(state,1001,7));
   for(int blocked:{-1,1,5,21,0x26,0x27,0x0204,0xffff}){
     state.fitnessMode=blocked;CHECK(!vlink::canSwitch(state,1001));CHECK(!vlink::canLoad(state,1001));
   }
   state.fitnessMode=ready;state.loaded=true;CHECK(!vlink::canSwitch(state,1001));CHECK(!vlink::canLoad(state,1001));state.loaded=false;
   state.armed=true;CHECK(!vlink::canSwitch(state,1001));CHECK(!vlink::canLoad(state,1001));state.armed=false;
   state.twinPending=true;CHECK(!vlink::canSwitch(state,1001));CHECK(!vlink::canLoad(state,1001));state.twinPending=false;
   state.cableActive=true;CHECK(!vlink::canSwitch(state,1001));CHECK(!vlink::canLoad(state,1001));state.cableActive=false;
   state.controlBusyUntil=2000;CHECK(!vlink::canSwitch(state,1001));state.controlBusyUntil=0;
   CHECK(!vlink::canSwitch(state,4500));CHECK(!vlink::canLoad(state,4500));
   state.endpointMs=0;CHECK(!vlink::canLoad(state,1001));CHECK(!vlink::twinControlReady(state,1001));
 }
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
 vlink::DeviceState iso;iso.link=vlink::LinkState::Ready;iso.modeMs=iso.statusMs=1000;iso.mode=7;iso.fitnessMode=4;iso.twinConnection=0;iso.weightUnitRaw=1;endpoint(iso,1000);
 for(int i=7;i<=11;++i){iso.modeValueMs[i]=1000;iso.modeValues[i]=i==7?100:i<10?0:5;}
 CHECK(vlink::canLoad(iso,1001));CHECK(vlink::canEditIsokinetic(iso,1001,7));CHECK(vlink::canEditIsokinetic(iso,1001,9));CHECK(!vlink::canEditIsokinetic(iso,1001,10));
 iso.modeValues[8]=1;CHECK(vlink::canEditIsokinetic(iso,1001,10));CHECK(!vlink::canEditIsokinetic(iso,1001,9));
 iso.loaded=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));CHECK(!vlink::canLoad(iso,1001));iso.loaded=false;
 iso.mode=1;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.mode=7;
 CHECK(!vlink::canEditIsokinetic(iso,1001,6));CHECK(!vlink::canEditIsokinetic(iso,1001,12));CHECK(!vlink::canEditIsokinetic(iso,10000,7));
 iso.cableActive=true;CHECK(!vlink::canEditIsokinetic(iso,1001,7));iso.cableActive=false;iso.armed=true;CHECK(!vlink::canLoad(iso,1001));iso.armed=false;
 iso.twinConnection=2;iso.endpointMs=0;CHECK(!vlink::canLoad(iso,1001));endpoint(iso,1000,true);CHECK(vlink::canLoad(iso,1001));

 CHECK_EQ(damperFactor(4),17);CHECK_EQ(damperFactor(9),50);CHECK_EQ(damperFactor(-1),-1);CHECK_EQ(damperFactor(10),-1);CHECK(std::strcmp(damperRoman(4),"V")==0);
 vlink::DeviceState d;d.link=vlink::LinkState::Ready;d.modeMs=d.statusMs=1000;d.mode=4;d.fitnessMode=4;d.modeValues[5]=4;d.modeValueMs[5]=1000;d.twinConnection=2;d.twinMs=1000;d.twinRoleMs=1000;d.twinLastRole=1;endpoint(d,1000,true);CHECK(vlink::canEditDamper(d,1001));d.mode=2;CHECK(!vlink::canEditDamper(d,1001));d.mode=4;d.loaded=true;CHECK(!vlink::canEditDamper(d,1001));d.loaded=false;d.modeValues[5]=10;CHECK(!vlink::canEditDamper(d,1001));

 vlink::DeviceState band;band.link=vlink::LinkState::Ready;band.modeMs=band.statusMs=1000;band.mode=2;band.fitnessMode=4;band.modeValueMs[0]=1000;band.weightUnitRaw=1;endpoint(band,1000);
 CHECK(vlink::canEditBand(band,1001));band.mode=1;CHECK(!vlink::canEditBand(band,1001));band.mode=2;
 band.loaded=true;CHECK(!vlink::canEditBand(band,1001));band.loaded=false;band.cableActive=true;CHECK(!vlink::canEditBand(band,1001));band.cableActive=false;
 band.weightUnitRaw=-1;CHECK(!vlink::canEditBand(band,1001));band.weightUnitRaw=0;CHECK(!vlink::canEditBand(band,10000));

  d.modeValues[5]=4;d.twinMs=1000;CHECK(vlink::canLoad(d,1001));d.modeValueMs[5]=0;CHECK(!vlink::canLoad(d,1001));d.modeValueMs[5]=1000;d.armed=true;CHECK(!vlink::canLoad(d,1001));d.armed=false;d.endpointMs=0;CHECK(!vlink::canLoad(d,1001));
  CHECK(std::abs(kilograms(100)-45.359237)<0.000001);
  // The captured minimum is 5 protocol lb but 2 kg per trainer, not 2.5 kg.
  CHECK_EQ(displayBaseWeightKilograms(5,false),2.0);CHECK_EQ(displayBaseWeightKilograms(5,true),4.0);
  CHECK_EQ(displayBaseWeightKilograms(6,false),2.5);CHECK_EQ(displayBaseWeightKilograms(6,true),5.0);
  CHECK_EQ(displayWeightKilograms(5,false),2.5);CHECK_EQ(displayWeightKilograms(5,true),5.0); // Modifiers unchanged.
  CHECK_EQ(baseWeightKgStep(5,-1,5,200),5);CHECK_EQ(baseWeightKgStep(5,1,5,200),6);
  CHECK_EQ(baseWeightKgStep(6,-1,5,200),5);CHECK_EQ(baseWeightKgStep(6,1,5,200),7);
  Editor minimumWeight;minimumWeight.observe(5);minimumWeight.turn(1,true,false,100,5,200,true);
  CHECK_EQ(minimumWeight.target,6);CHECK(minimumWeight.pending);
  minimumWeight.cancel();minimumWeight.turn(-1,true,false,200,5,200,true);CHECK(!minimumWeight.pending);
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
  CHECK(!vlink::canLoad(s,100));s.weightLbs=20;s.chainsLbs=0;s.eccentricLbs=0;s.modeMs=s.settingsMs=100;CHECK(vlink::canLoad(s,100));CHECK(!vlink::canLoad(s,4000));
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
  for(int fitness:{-1,2,0x26,0x27,8}){
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
  // Twin force must contain both synchronized channels. A local stream and
  // delayed independent packet cannot overwrite or be doubled into pair force.
  {
  vlink::DeviceState pair=moving;endpoint(pair,1000,true);
  pair.loaded=true;pair.fitnessMode=5;pair.mode=1;
  voltra::Event twin;twin.type=voltra::EventType::Telemetry;twin.twinTelemetry=true;
  twin.hasTwinPairMotion=true;twin.activeWeightTenths=40;twin.peerForceTenths=60;
  twin.cableVelocityMmPerSecond=100;twin.peerCableVelocityMmPerSecond=0;
  for(int mode:{1,2,4,7}){
    pair.mode=mode;vlink::updateWeightTelemetry(pair,twin,1000);
    CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,100);
  }
  CHECK_EQ(vlink::movingWeightReading(pair,1500).tenthsLb,-1);
  twin.cableVelocityMmPerSecond=0;twin.peerCableVelocityMmPerSecond=-100;
  vlink::updateWeightTelemetry(pair,twin,1000);CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,100);
  twin.peerCableVelocityMmPerSecond=0;vlink::updateWeightTelemetry(pair,twin,1000);
  CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,-1);
  twin.peerCableVelocityMmPerSecond=-100;twin.peerForceTenths=-1;
  vlink::updateWeightTelemetry(pair,twin,1000);CHECK_EQ(pair.pairMotionMs,0u);
  twin.peerForceTenths=10001;vlink::updateWeightTelemetry(pair,twin,1000);CHECK_EQ(pair.pairMotionMs,0u);
  twin.peerForceTenths=60;vlink::updateWeightTelemetry(pair,twin,1000);
  vlink::updateWeightTelemetry(pair,motion,1001);CHECK_EQ(pair.pairMotionMs,1000u);
  twin.hasTwinPairMotion=false;vlink::updateWeightTelemetry(pair,twin,1001);CHECK_EQ(pair.pairMotionMs,1000u);
  pair.loaded=false;pair.fitnessMode=4;CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,-1);
  pair.loaded=true;pair.fitnessMode=5;pair.twinPending=true;CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,-1);
  pair.twinPending=false;pair.endpointMs=0;CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,-1);
  endpoint(pair,1000,true);twin.repCount=7;vlink::updateRepetitionTelemetry(pair,twin,1001);
  CHECK_EQ(vlink::repetitionReading(pair,1002),7);
  motion.repCount=99;vlink::updateRepetitionTelemetry(pair,motion,1002);CHECK_EQ(pair.reps,7);
  twin.repCount=0;vlink::updateRepetitionTelemetry(pair,twin,1002);CHECK_EQ(pair.reps,0);
  pair.twinPending=true;CHECK_EQ(vlink::repetitionReading(pair,1002),-1);pair.twinPending=false;
  pair.endpointMs=0;CHECK_EQ(vlink::repetitionReading(pair,1002),-1);endpoint(pair,1000);
  vlink::clearWeightTelemetry(pair);vlink::updateWeightTelemetry(pair,twin,1003);CHECK_EQ(pair.motionMs,0u);CHECK_EQ(pair.pairMotionMs,0u);
  twin.repCount=20;vlink::updateRepetitionTelemetry(pair,twin,1003);CHECK_EQ(pair.reps,0);
  vlink::updateRepetitionTelemetry(pair,motion,1003);CHECK_EQ(pair.reps,99);
  }

  // Replay real Twin B4 data through the display gate. Resting preload stays
  // hidden; unequal moving forces are summed once, never target or local * 2.
  {
    auto packet=[](const char* hex){
      uint8_t bytes[40]{};const size_t size=std::strlen(hex)/2;
      for(size_t i=0;i<size;++i){unsigned value;std::sscanf(hex+2*i,"%2x",&value);bytes[i]=value;}
      return voltra::decode(bytes,size);
    };
    auto rest=packet("551b047510aa1e9e1000b40600020000003c00080002000000456f");
    auto returning=packet("551b047510aaa6b11000b46700640091fe67006b00630085fe657c");
    auto counted=packet("551f044e10aa0ea91200b43d00f20085fe030001003c003f00ed0032fe55c3");
    vlink::DeviceState pair;pair.link=vlink::LinkState::Ready;pair.statusMs=1000;
    pair.mode=1;pair.fitnessMode=5;pair.loaded=true;endpoint(pair,1000,true);
    vlink::updateWeightTelemetry(pair,rest,1000);
    CHECK_EQ(pair.pairForceTenths,14);CHECK_EQ(vlink::movingWeightReading(pair,1001).tenthsLb,-1);
    vlink::updateWeightTelemetry(pair,returning,1010);
    CHECK_EQ(vlink::movingWeightReading(pair,1011).tenthsLb,210);
    CHECK_EQ(vlink::movingWeightReading(pair,1510).tenthsLb,-1);
    vlink::updateWeightTelemetry(pair,counted,1520);vlink::updateRepetitionTelemetry(pair,counted,1520);
    CHECK_EQ(vlink::movingWeightReading(pair,1521).tenthsLb,124);CHECK_EQ(vlink::repetitionReading(pair,1521),1);
    vlink::updateWeightTelemetry(pair,rest,1530);CHECK_EQ(vlink::movingWeightReading(pair,1531).tenthsLb,-1);
    // Sentinel/invalid force clears the pair, even if its other cable is moving.
    returning.peerForceTenths=0xffff;vlink::updateWeightTelemetry(pair,returning,1540);
    CHECK_EQ(pair.pairMotionMs,0u);CHECK_EQ(vlink::movingWeightReading(pair,1541).tenthsLb,-1);
    returning.peerForceTenths=107;returning.activeWeightTenths=0xffff;
    vlink::updateWeightTelemetry(pair,returning,1550);CHECK_EQ(pair.pairMotionMs,0u);
    endpoint(pair,1560);vlink::clearWeightTelemetry(pair);
    vlink::updateWeightTelemetry(pair,counted,1561);CHECK_EQ(pair.motionMs,0u);
  }

  // Discovery is separate from both saved slots, deduplicated, and bounded.
  vlink::DeviceState discovered;strcpy(discovered.trainers[0].address,"saved-left");strcpy(discovered.trainers[1].address,"saved-right");
  CHECK_EQ(vlink::nearbySlot(discovered,"new-trainer",1000),0);
  for(int i=0;i<vlink::NEARBY_TRAINER_LIMIT;++i){auto& t=discovered.nearby[i];snprintf(t.address,sizeof(t.address),"device-%d",i);t.seen=true;t.lastSeenMs=1000;}
  CHECK_EQ(vlink::nearbySlot(discovered,"DEVICE-4",1001),4);
  CHECK_EQ(vlink::nearbySlot(discovered,"extra",1001),-1); // don't evict a fresh visible row
  discovered.nearby[2].lastSeenMs=1;CHECK_EQ(vlink::nearbySlot(discovered,"extra",10002),2);
  discovered.discoveryPausedForTv=true;CHECK(!vlink::trainerNearby(discovered,0,1000));
  CHECK(vlink::trainerAvailable(discovered,0,1000)); // saved reconnect is not Nearby
  discovered.selected=0;discovered.link=vlink::LinkState::Ready;discovered.modeMs=discovered.statusMs=1000;
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
