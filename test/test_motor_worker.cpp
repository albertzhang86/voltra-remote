#include "minitest.h"
#include "host/Arduino.h" // Shared platform shims, including strlcpy on Ubuntu.
#include "link/voltra_link.h"
#include <atomic>
#include <vector>

// Captured from VTR-161230 on 2026-10-17 while the user confirmed the cable
// fully returned. Both ordinary read and explicit SETUP report position=2 cm,
// saved zero=0 cm. Originals have valid CRCs and are never rewritten below.
static constexpr char CABLE_CAPTURE[]="551b08d610aa002020000f000300823e02006a5000001150008a5b";
static constexpr char SETUP_CAPTURE[]="5518088310aa150020000f0002006a500000823e02000946";
static std::vector<uint8_t> packet(const char* hex){
 std::vector<uint8_t> result(std::strlen(hex)/2);
 for(size_t i=0;i<result.size();++i){unsigned value=0;std::sscanf(hex+2*i,"%2x",&value);result[i]=value;}
 CHECK(voltra::validFrame(result.data(),result.size()));return result;
}
namespace vlink {
static DeviceState current;
static uint32_t clockMs=1000,workerSession=12,controllerLastFrameMs=0;
static std::atomic<uint32_t> tvEpoch{1};
static bool disconnected=false,stopRequested=false,settingsDirty=false,twinAcknowledged=false;
static uint8_t diagnosticReplies=0,expectedTwinOperation=0;
static uint16_t expectedTwinSequence=0;
static int queue=0;
struct Lock {Lock(){}~Lock(){}};
struct SerialStub {
 int availableForWrite()const{return 0;}
 template<class... Args>void printf(const char*,Args...){}
 void println(){}
} Serial;
static uint32_t millis(){return clockMs;}
static unsigned pdMS_TO_TICKS(unsigned value){return value;}
static void vTaskDelay(unsigned value){clockMs+=value;}
static unsigned uxQueueMessagesWaiting(int){return 0;}
static bool sameSession(){return current.session==workerSession;}
static void captureDiagnostic(const uint8_t*,size_t,bool){}
VoltraLink voltraLink;
DeviceState VoltraLink::state(){return current;}
static bool send(voltra::Frame frame);
#include "production_motor_worker.cpp"

static std::vector<std::vector<uint8_t>> sent;
static bool returnZero=false,returnSetup=true,confirmLoad=true,firmware2=false;
static void receive(std::vector<uint8_t> frame){
 CHECK(voltra::validFrame(frame.data(),frame.size()));
 applyControllerFrame(frame.data(),frame.size());
}
static bool equal(voltra::Frame frame,voltra::Frame expected){
 return frame.len==expected.len&&!std::memcmp(frame.data,expected.data,frame.len);
}
static std::vector<uint8_t> settingsReply(bool loaded){
 // Synthetic device reply, kept separate from the captured cable fixtures.
 std::vector<uint8_t> reply={0x55,0,8,0,0x10,0xaa,0,0,0x20,0,0x0f,0,6,0,
   0x86,0x3e,5,0,0x87,0x3e,0,0,0x88,0x3e,0,0,
   0x89,0x3e,uint8_t(loaded?(firmware2?1:5):(firmware2?0:4)),0,0xb0,0x4f,1,0xb0,0x53,0,0,0};
 voltra::sealTwinFrame(reply.data(),reply.size());return reply;
}
static bool loadIssued=false;
static bool send(voltra::Frame frame){
 CHECK(voltra::validFrame(frame.data,frame.len));
 sent.emplace_back(frame.data,frame.data+frame.len);++clockMs;
 if(equal(frame,voltra::setupFrame())&&returnSetup){
   auto reply=packet(SETUP_CAPTURE);
   if(returnZero){
     // Synthetic zero case differs only in current position; preserve saved
     // offset 0. Recompute CRC instead of altering parser or calibration.
     reply[20]=0;voltra::sealTwinFrame(reply.data(),reply.size());
   }
   receive(reply);
 }
 if(frame.len==19&&frame.data[10]==0x11&&frame.data[13]==0x89&&frame.data[14]==0x3e)
   loadIssued=frame.data[15]==1||frame.data[15]==5;
 if(equal(frame,voltra::readSettingsFrame()))receive(settingsReply(loadIssued&&confirmLoad));
 return true;
}
static void initialize(bool twin,bool zero,bool setup=true,bool confirms=true){
 current={};clockMs=1000;workerSession=12;tvEpoch=1;
 disconnected=stopRequested=false;sent.clear();loadIssued=false;
 returnZero=zero;returnSetup=setup;confirmLoad=confirms;
 current.session=12;current.selected=0;current.link=LinkState::Ready;
 current.endpoint.valid=true;current.endpoint.status=twin?0x12:0;
 std::strcpy(current.endpoint.local,"02:00:00:00:00:01");
 std::strcpy(current.endpoint.peer,twin?"02:00:00:00:00:02":"00:00:00:00:00:00");
 std::strcpy(current.trainers[0].address,current.endpoint.local);
 current.endpointMs=current.twinMs=1000;current.twinConnection=twin?2:0;
 current.weightUnitRaw=1;
 receive(settingsReply(false));receive(packet(CABLE_CAPTURE));
 CHECK_EQ(current.cableCm,2);CHECK_EQ(current.cableOffsetCm,0);
 CHECK_EQ(current.cableScreenId,0);CHECK_EQ(current.cableMs,1000u);
 CHECK_EQ(current.cableOffsetMs,1000u);
 CHECK(tvMotorPreparationFailure(current,true,1001)==MotorFailure::None);
 CHECK(tvMotorFailure(current,true,1001)==MotorFailure::CableNotZero);
 clockMs=1001;current.motorPending=true;
}
static void run(bool local=false){
 Command command{};command.type=CommandType::Motor;command.parameter=Parameter::Weight;
 command.value=1;command.session=12;command.tvEpoch=local?0:1;command.deadline=clockMs+6000;
 command.expectedMode=1;command.motorConfiguration=motorConfiguration(current,clockMs);
 if(current.endpoint.paired())std::strcpy(command.expectedPeer,current.endpoint.peer);
 performMotor(command);
}
static unsigned count(voltra::Frame expected){
 unsigned result=0;for(const auto& frame:sent)if(equal({frame.data(),frame.size()},expected))++result;return result;
}
static void checkWrites(unsigned go){
 uint8_t storage[19];voltra::Frame expected;
 CHECK(voltra::motorFrame(firmware2?0:4,true,storage,expected));
 CHECK_EQ(count(voltra::setupFrame()),1u);CHECK_EQ(count(expected),go);
 CHECK_EQ(count(voltra::stopFrame()),0u);CHECK_EQ(count(voltra::guidedLoadTriggerFrame()),0u);
 CHECK_EQ(count(voltra::cableFrame(true)),0u);CHECK_EQ(count(voltra::cableFrame(false)),0u);
 for(const auto& bytes:sent){
   const voltra::Frame frame{bytes.data(),bytes.size()};
   // No vendor fallback, parameter/calibration write, repeated load or other
   // action can hide behind a successful or failed preflight in this worker.
   CHECK(equal(frame,voltra::setupFrame())||equal(frame,voltra::readSettingsFrame())||equal(frame,expected));
 }
 CHECK_EQ(current.motorCompleted,1u);CHECK(!current.motorPending);
}
}
int main(){
 using namespace vlink;
 // Exact Firmware 2.0 report: mode is confirmed even while the independent
 // motor field remains zero. This runs the production receive handler.
 initialize(false,true);
 receive(packet("552e04a710aa0d002000100900863e2d00873e1a00883e1300893e0000025109035108b04f01e14e01245100ff35"));
 CHECK_EQ(current.mode,1);CHECK_EQ(current.fitnessMode,0);
 CHECK(modeConfirmed(current,clockMs));CHECK(!canLoad(current,clockMs));
 CHECK(!current.loaded);CHECK(!current.armed);
 current.motorPending=false;
 CHECK(canEdit(current,clockMs));CHECK(canLoad(current,clockMs));
 // Exact CRC-valid native load/unload cycle, independent of our command code.
 receive(packet("5513040310aa92012000100100893e010042fc"));
 CHECK(current.loaded);CHECK(!current.armed);CHECK_EQ(current.fitnessMode,1);
 CHECK(!canLoad(current,clockMs));
 receive(packet("5513040310aaa7012000100100893e0000917d"));
 CHECK(!current.loaded);CHECK_EQ(current.fitnessMode,0);CHECK(canLoad(current,clockMs));
 uint8_t bytes[19];voltra::Frame frame;
 CHECK(voltra::motorFrame(0,true,bytes,frame));CHECK(voltra::validFrame(frame.data,frame.len));
 CHECK_EQ(frame.data[15],1);CHECK_EQ(frame.data[16],0);
 CHECK(voltra::motorFrame(1,false,bytes,frame));CHECK_EQ(frame.data[15],0);
 for(int unknown:{-1,2,3,0x100,0x101,0x204,0x105,0xffff}){
   CHECK(!voltra::motorFrame(unknown,true,bytes,frame));CHECK(!voltra::motorFrame(unknown,false,bytes,frame));
 }
 const auto revision=current.modeRevision;
 const auto modeStamp=current.modeMs;
 auto motorOnly=packet("551604fc10aa0c1c2000100200893e0401b04f007153");
 // A motor-only notification must not renew the mode's revision or freshness.
 std::vector<uint8_t> report={0x55,0,4,0,0x10,0xaa,0,0,0x20,0,0x10,1,0,0x89,0x3e,4,0,0,0};
 voltra::sealTwinFrame(report.data(),report.size());++clockMs;receive(report);
 CHECK_EQ(current.modeRevision,revision);CHECK_EQ(current.modeMs,modeStamp);
 CHECK(modeConfirmed(current,clockMs));CHECK(!modeConfirmed(current,modeStamp+3500));
 receive(motorOnly);CHECK_EQ(current.mode,0);CHECK(!modeConfirmed(current,clockMs));
 for(bool fw2:{false,true})for(bool local:{false,true})for(bool twin:{false,true}){
   firmware2=fw2;
   initialize(twin,false);run(local);
   CHECK(current.motorFailure==MotorFailure::CableNotZero);CHECK(!current.motorSucceeded);
   CHECK_EQ(current.cableCm,2);CHECK_EQ(current.cableOffsetCm,0);
   CHECK(current.cableMs>1000);CHECK(current.cableOffsetMs>1000);checkWrites(0);
   initialize(twin,true);run(local);
   CHECK(current.motorFailure==MotorFailure::None);CHECK(current.motorSucceeded);
   CHECK(current.loaded);CHECK_EQ(current.fitnessMode,fw2?1:5);checkWrites(1);
   initialize(twin,true,false);run(local);
   CHECK(current.motorFailure==MotorFailure::CableUnknown);CHECK(!current.motorSucceeded);checkWrites(0);
   initialize(twin,true,true,false);run(local);
   CHECK(current.motorFailure==MotorFailure::NotConfirmed);CHECK(!current.motorSucceeded);checkWrites(1);
 }
 std::printf("motor worker: %d checks, %d failures\n",g_checks,g_failures);return g_failures?1:0;
}
