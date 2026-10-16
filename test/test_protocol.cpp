#include "minitest.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
#include <initializer_list>
#include <utility>
using namespace voltra;

static bool hexEq(const Frame& f, const char* hex) {
  if (std::strlen(hex) != f.len * 2) return false;
  for (size_t i = 0; i < f.len; ++i) {
    unsigned v; std::sscanf(hex + 2 * i, "%2x", &v);
    if (f.data[i] != v) return false;
  }
  return true;
}

static void testFrames() {
 for(int mode:{1,2,4,7})CHECK(supportedMode(mode));
 for(int mode:{0,3,6,8,99})CHECK(!supportedMode(mode));
 CHECK(fitnessModeLoaded(21));CHECK(!fitnessModeUnloaded(21));
 uint8_t isoStorage[21];Frame iso;
 for(auto entry:{std::pair<int,int>{7,100},{7,500},{7,2000},{8,0},{8,1},{9,0},{9,1000},{10,5},{10,100},{11,200}}){
  CHECK(isokineticFrame(entry.first,entry.second,isoStorage,iso));CHECK(validFrame(iso.data,iso.len));
  CHECK_EQ(iso.data[10],0x11);CHECK_EQ(iso.data[13]|(iso.data[14]<<8),MODE_PARAMETERS[entry.first].id);
  unsigned value=0;for(size_t i=15;i<iso.len-2;++i)value|=unsigned(iso.data[i])<<((i-15)*8);CHECK_EQ(value,unsigned(entry.second));
  CHECK_EQ(iso.len,entry.first==7?21u:entry.first==8?18u:19u);
 }
 for(auto entry:{std::pair<int,int>{6,100},{7,0},{7,99},{7,2001},{8,2},{9,1},{9,-1},{10,101},{11,201},{12,5}})CHECK(!isokineticFrame(entry.first,entry.second,isoStorage,iso));
 CHECK(!isokineticFrame(7,100,nullptr,iso));

 uint8_t damperBytes[18];Frame damper;CHECK(!damperLevelFrame(-1,damperBytes,damper));CHECK(!damperLevelFrame(10,damperBytes,damper));CHECK(!damperLevelFrame(0,nullptr,damper));
 for(int i=0;i<10;++i){CHECK(damperLevelFrame(i,damperBytes,damper));CHECK(validFrame(damper.data,damper.len));CHECK_EQ(damper.len,18u);CHECK_EQ(damper.data[13]|(damper.data[14]<<8),0x5103);CHECK_EQ(damper.data[15],i);}

 uint8_t bandStorage[19];Frame band;
 CHECK(!bandMaximumFrame(14,bandStorage,band));CHECK(!bandMaximumFrame(201,bandStorage,band));CHECK(!bandMaximumFrame(106,nullptr,band));
 for(int value=15;value<=200;++value){CHECK(bandMaximumFrame(value,bandStorage,band));CHECK(validFrame(band.data,band.len));CHECK_EQ(band.data[10],0x11);CHECK_EQ(band.data[13]|(band.data[14]<<8),0x5362);CHECK_EQ(band.data[15]|(band.data[16]<<8),value);}

  CHECK_EQ(authFrameFallback().len, 41u);
  CHECK(hexEq(authFrameFallback(), "552904c90110000020004f6950686f6e6500000000000000000000000000000084ab1a5f29200172d8"));
  CHECK_EQ(authFrame().len, 41u);
  CHECK(std::memcmp(authFrame().data, authFrameFallback().data, 11) == 0);      // header + cmd identical
  CHECK(std::memcmp(authFrame().data + 11, "Voltra Knob", 11) == 0);            // custom name in the name field
  CHECK(std::memcmp(authFrame().data + 32, authFrameFallback().data + 32, 7) == 0);   // trailer identical
  CHECK(hexEq(telemetrySubscribeAllFrame(), "551504a9aa10012020001101008351ffffffff35eb"));
  CHECK_EQ(initFrame(0).len, 15u);
  CHECK(hexEq(initFrame(0), "550f0801aad200002000ff00aa0419"));
  CHECK_EQ(initFrame(1).len, 31u);
  CHECK(hexEq(setupFrame(), "55130403aa10150020000f02006a50823e8f2f"));
  CHECK(hexEq(goFrame(),    "55130403aa1016002000110100893e05008173"));
  CHECK(hexEq(stopFrame(),  "55130403aa101b002000110100893e040037dd"));
  CHECK(hexEq(modeWeightTrainingFrame(), "55130403aa1000202000110100b04f0100f353"));
  Frame f;
  CHECK(weightFrame(5, f));   CHECK(hexEq(f, "55130403aa1000202000110100863e0500ffe9"));
  CHECK(weightFrame(20, f));  CHECK(hexEq(f, "55130403aa100f202000110100863e1400fa79"));
  CHECK(weightFrame(200, f)); CHECK_EQ(f.len, 19u);
  CHECK(f.data[15] == 200 && f.data[16] == 0);  // uint16 LE weight payload
  CHECK(!weightFrame(4, f));
  CHECK(!weightFrame(201, f));
  // Every weight frame carries its own value at bytes 15..16 and the correct length byte.
  for (int w = WEIGHT_MIN; w <= WEIGHT_MAX; ++w) {
    CHECK(weightFrame(w, f));
    CHECK_EQ(f.len, 19u);
    CHECK_EQ((int)f.data[1], 0x13);
    CHECK_EQ(f.data[15] | (f.data[16] << 8), w);
  }
}

static size_t fromHex(const char* hex, uint8_t* out, size_t cap) {
  size_t n = std::strlen(hex) / 2;
  for (size_t i = 0; i < n && i < cap; ++i) { unsigned v; std::sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  return n;
}

// Build a synthetic frame: header bytes then zero-fill to `len`, then apply (offset,value) patches.
static size_t synth(uint8_t* out, size_t len, const char* headerHex, std::initializer_list<std::pair<int,int>> patches) {
  std::memset(out, 0, len);
  fromHex(headerHex, out, len);
  out[1] = (uint8_t)len;
  for (auto& p : patches) out[p.first] = (uint8_t)p.second;
  return len;
}

static void testDecode() {
  uint8_t b[160];
  // Settings update (cmd 0x10 at byte 10): two params, base weight 863e=0x0032 (50 lb), mode b04f=1.
  size_t n = synth(b, 46, "552e04", {{10,0x10},{11,2},{13,0x86},{14,0x3e},{15,0x32},{16,0x00},{17,0xb0},{18,0x4f},{19,1}});
  Event e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 50); CHECK_EQ(e.mode, 1);

  // Settings with a 1-byte param first, then weight: verifies width handling.
  n = synth(b, 46, "551604", {{10,0x10},{11,2},{13,0xb0},{14,0x4f},{15,1},{16,0x86},{17,0x3e},{18,0xc8},{19,0x00}});
  e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 200); CHECK_EQ(e.mode, 1);

  // Length alone cannot identify a mode report.
  n = synth(b, 18, "551204", {{15, 4}});
  e = decode(b, n); CHECK(e.type == EventType::Unknown); CHECK_EQ(e.mode, -1);

  // Device init battery at 11.
  n = synth(b, 35, "552304", {{11, 87}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 87);

  // Status battery at 12.
  n = synth(b, 52, "553404", {{12, 63}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 63);

  // State dump aliases the 5534 header: must NOT yield a battery reading.
  n = synth(b, 52, "553404ac", {{10,0xaa},{11,0x80},{12,0x25}});
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Telemetry stream, phase eccentric.
  n = synth(b, 58, "553a0470", {{10,0xaa},{11,0x81},{12,0x2b},{13, 3}});   // real stream frames are 0x3a = 58 bytes
  e = decode(b, n); CHECK(e.type == EventType::Telemetry); CHECK_EQ(e.phase, 3);
  b[22]=123;b[23]=1;e=decode(b,n);CHECK_EQ(e.activeWeightTenths,379);
  b[10]=0x10;e=decode(b,n);CHECK_EQ(e.activeWeightTenths,-1);
  n = synth(b, 20, "553a0470", {{1, 0x3a}, {13, 3}});   // too short (keep the 0x3a header byte)
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Real independent runtime captures from the user's trainer. The trailing
  // 0x003c is the 6 lb TARGET; force is the first payload word (1.0..1.2 lb).
  for(auto captured:{
    std::pair<const char*,int>{"551904e410aacb6e0200b40b0003000000030000003c0045a8",11},
    {"551904e410aa2f6f0100b40a0003000000000000003c007142",10},
    {"551504a910aa936f0000b40c00030000003c00d8db",12},
    {"551504a910aaf76f0000b40b00030000003c009387",11}
  }){
    n=fromHex(captured.first,b,sizeof(b));CHECK(validFrame(b,n));e=decode(b,n);
    CHECK(e.type==EventType::Telemetry);CHECK_EQ(e.activeWeightTenths,captured.second);
    CHECK(e.hasMotionTelemetry);CHECK_EQ(e.cableLengthMm,3);CHECK_EQ(e.cableVelocityMmPerSecond,0);
    CHECK(e.hasTargetForceReport);CHECK_EQ(e.targetForceTenths,60);
    CHECK_EQ(e.weightLbs,-1);CHECK_EQ(e.mode,-1);CHECK_EQ(e.fitnessMode,-1);
  }
  for(int source:{0,1,2}){
    const size_t base=source==0?19:23;
    for(size_t length:{base,base+2}){
      n=synth(b,length,"551304",{{8,source},{10,0xb4},{11,0},{12,0}});
      e=decode(b,n);CHECK_EQ(e.activeWeightTenths,0); // zero is a measurement
      b[13]=0x34;b[14]=0x12;b[15]=0x2c;b[16]=1;e=decode(b,n);
      CHECK(e.hasMotionTelemetry);CHECK_EQ(e.cableLengthMm,0x1234);CHECK_EQ(e.cableVelocityMmPerSecond,300);
      b[15]=0xd4;b[16]=0xfe;e=decode(b,n);CHECK_EQ(e.cableVelocityMmPerSecond,-300);
      CHECK(e.hasTargetForceReport);CHECK_EQ(e.targetForceTenths,length==base?-1:0);
      if(length==base+2){
        b[base-2]=0x7b;b[base-1]=1;e=decode(b,n);CHECK_EQ(e.targetForceTenths,379);
        b[base-2]=0xff;b[base-1]=0xff;e=decode(b,n);CHECK(e.hasTargetForceReport);CHECK_EQ(e.targetForceTenths,-1);
      }
      b[11]=0x7b;b[12]=1;e=decode(b,n);CHECK_EQ(e.activeWeightTenths,379);
      b[9]=1;e=decode(b,n);CHECK(e.type==EventType::Unknown);b[9]=0;
      b[8]=0x10+source;e=decode(b,n);CHECK(e.type==EventType::Unknown);b[8]=source;
      b[2]=8;e=decode(b,n);CHECK(e.type==EventType::Unknown);b[2]=4;
      for(size_t shortLength=13;shortLength<base;++shortLength){e=decode(b,shortLength);CHECK(e.type==EventType::Unknown);}
    }
    n=synth(b,base+1,"551304",{{8,source},{10,0xb4}});e=decode(b,n);CHECK(e.type==EventType::Unknown);
    n=synth(b,base+3,"551304",{{8,source},{10,0xb4}});e=decode(b,n);CHECK(e.type==EventType::Unknown);
  }
  // Another packet family with the same 58-byte envelope is not cable force.
  n=synth(b,58,"553a0470",{{10,7},{22,60}});e=decode(b,n);CHECK_EQ(e.activeWeightTenths,-1);
  // Pull carries the prior count; return already includes the current rep.
  n = synth(b, 74, "554a04", {{10,0xaa},{11,0x82},{12,0x3b},{13,1},{15,2},{17,7}});
  e = decode(b, n); CHECK(e.type == EventType::PerRep); CHECK_EQ(e.repCount, 8); CHECK_EQ(e.setCounter, 2);
  b[16]=1;b[18]=1;e=decode(b,n);CHECK_EQ(e.repCount,264);CHECK_EQ(e.setCounter,258);
  for(int phase:{1,2})for(int index:{0,3,65533,65534,65535}){
    b[13]=phase;b[17]=index&255;b[18]=index>>8;
    e=decode(b,n);CHECK(e.type==EventType::PerRep);CHECK_EQ(e.repCount,index+(phase==1?1:0));
  }
  for(int completed:{1,2,8}){
    b[13]=1;b[17]=completed-1;b[18]=0;e=decode(b,n);CHECK_EQ(e.repCount,completed);
    b[13]=2;b[17]=completed;e=decode(b,n);CHECK_EQ(e.repCount,completed);
  }
  for(int phase:{0,3,255}){
    b[13]=phase;e=decode(b,n);CHECK(e.type==EventType::Unknown);CHECK_EQ(e.repCount,-1);
  }

  // Same-set real captures from HJewkes/voltra-node-sdk vendor-decoders.test.ts:
  // first pull index 0 at 50 lb, followed by a final summary total of 1.
  n=fromHex("554a04c610aa4a072000aa823b011a03000000f4010000000000005203810282038102cb00000075000000f40100004a060000010000520375000000bc0000004a060000000000006f1f",b,sizeof(b));
  CHECK(validFrame(b,n));e=decode(b,n);CHECK_EQ(e.setCounter,3);CHECK_EQ(e.repCount,1);
  n=fromHex("556e043c10aa8b072000aa855f010203f40100000000000003000100f9018203cb000000f501110275000000f8017b0156000000f50120013f00000052030000e8030000f40100004d170000d6ac0400bc000000b8000000bd000000b90000004a060000000000006d0b000034a5",b,sizeof(b));
  CHECK(validFrame(b,n));e=decode(b,n);CHECK(e.type==EventType::SetSummary);CHECK_EQ(e.repCount,1);

  // Vendor set summary: 110 bytes, aa 85 5f, repCount u16 at 26.
  n = synth(b, 110, "556e04", {{10,0xaa},{11,0x85},{12,0x5f},{26,12},{27,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 12);
  b[26]=8;e=decode(b,n);CHECK_EQ(e.repCount,8); // Captured eight-rep set stays eight.

  // Vendor summary: 140 bytes, aa 86 7d, repCount u16 at 16.
  n = synth(b, 140, "558c04", {{10,0xaa},{11,0x86},{12,0x7d},{16,9},{17,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 9);
  b[16]=1;b[17]=0;e=decode(b,n);CHECK_EQ(e.repCount,1); // Already a count, not an index.

  // Settings with fitness mode 893e = 5 (motor engaged) alongside the weight.
  n = synth(b, 46, "552e04", {{10,0x10},{11,2},{13,0x86},{14,0x3e},{15,0x0a},{16,0x00},{17,0x89},{18,0x3e},{19,0x05},{20,0x00}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 10); CHECK_EQ(e.fitnessMode, 5);
  CHECK(fitnessModeLoaded(5)); CHECK(fitnessModeUnloaded(4)); CHECK(!fitnessModeLoaded(4));

  // Mode confirmation carrying 893e reports fitnessMode, not training mode.
  n = synth(b, 19, "551304", {{10,0x10},{11,1},{13,0x89},{14,0x3e},{15,4}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.fitnessMode, 4); CHECK_EQ(e.mode, -1);
  n = synth(b, 18, "551204", {{10,0x10},{11,1},{13,0xb0},{14,0x4f},{15,1}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.mode, 1); CHECK_EQ(e.fitnessMode, -1);
  // Unrelated single-register notifications and write ACKs cannot corrupt mode.
  n = synth(b,18,"551204",{{10,0x10},{11,1},{13,3},{14,0x51},{15,9}});
  e=decode(b,n);CHECK_EQ(e.mode,-1);CHECK_EQ(e.fitnessMode,-1);
  n = synth(b,18,"551208",{{10,0x11},{11,0},{13,0xb0},{14,0x4f},{15,255}});
  e=decode(b,n);CHECK(e.type==EventType::Unknown);CHECK_EQ(e.mode,-1);CHECK_EQ(e.fitnessMode,-1);
  n=fromHex("551204c7aa100a20200011010082510a0f36",b,sizeof(b));
  CHECK(validFrame(b,n));e=decode(b,n);CHECK_EQ(e.mode,-1);CHECK_EQ(e.fitnessMode,-1);
  // Truncation must not reinterpret CRC bytes as a two-byte fitness value.
  n=synth(b,18,"551204",{{10,0x10},{11,1},{13,0x89},{14,0x3e},{15,4}});
  e=decode(b,n);CHECK_EQ(e.fitnessMode,-1);

  // cmd 0x0f bulk response: frame type 0x08, count u16 at 12, params from 14.
  n = synth(b, 30, "551e08", {{10,0x0f},{12,3},{13,0},{14,0x86},{15,0x3e},{16,0x28},{17,0x00},{18,0x89},{19,0x3e},{20,0x04},{21,0x00},{22,0xb0},{23,0x4f},{24,1}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 40); CHECK_EQ(e.fitnessMode, 4); CHECK_EQ(e.mode, 1);

  // Read-settings request frame: cmd 0x0f, 6 params, valid header length.
  CHECK_EQ((int)readSettingsFrame().data[10], 0x0f);
  CHECK_EQ((int)readSettingsFrame().data[1], (int)readSettingsFrame().len);
  CHECK_EQ((int)readSettingsFrame().data[11], 6);

  // Guided-load status in a cmd 0x0f response: safety 538d=0, state 53c7=1, countdown 53c8=2500 ms, ctrl 53c9=1.
  n = synth(b, 30, "551e08", {{10,0x0f},{12,4},{13,0},{14,0x8d},{15,0x53},{16,0},{17,0xc7},{18,0x53},{19,1},{20,0xc8},{21,0x53},{22,0xc4},{23,0x09},{24,0xc9},{25,0x53},{26,1}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.guidedCountdownMs, 2500); CHECK_EQ(e.guidedSafety, 0);
  // Fitness mode 0x26 = armed, not unloaded.
  CHECK(fitnessModeArmed(0x26)); CHECK(!fitnessModeUnloaded(0x26)); CHECK(!fitnessModeLoaded(0x26));
  CHECK(fitnessModeArmed(0x27)); CHECK(!fitnessModeLoaded(0x27)); CHECK(fitnessModeLoaded(5));
  CHECK_EQ(e.guidedState, 1); CHECK_EQ(e.guidedCtrl, 1);
  // Trigger frame: vendor cmd 0xaa, sub-command 0x12, 14 bytes with valid length byte.
  CHECK_EQ(guidedLoadTriggerFrame().len, 14u);
  CHECK_EQ((int)guidedLoadTriggerFrame().data[10], 0xaa); CHECK_EQ((int)guidedLoadTriggerFrame().data[11], 0x12);
  CHECK_EQ((int)guidedLoadTriggerFrame().data[1], 14);
  CHECK_EQ((int)readGuidedLoadStatusFrame().data[11], 4);

  // Inverse chains is a separate one-byte setting, including in bulk replies.
  n=synth(b,20,"551408",{{10,0x0f},{12,1},{14,0xb0},{15,0x53},{16,1}});
  e=decode(b,n);CHECK_EQ(e.inverseChainsEnabled,1);CHECK_EQ(e.chainsLbs,-1);
  Frame inverse;CHECK(!inverseChainsFrame(-1,inverse));CHECK(!inverseChainsFrame(2,inverse));
  for(int lbs=0;lbs<=1;++lbs){CHECK(inverseChainsFrame(lbs,inverse));CHECK(validFrame(inverse.data,inverse.len));CHECK_EQ(inverse.data[13],0xb0);CHECK_EQ(inverse.data[14],0x53);CHECK_EQ(inverse.data[15],lbs);}
  // Cable position and saved offset are two-byte values; save notification is distinct from polling.
  n=synth(b,24,"551804",{{10,0x10},{11,2},{13,0x82},{14,0x3e},{15,44},{17,0x6a},{18,0x50},{19,40}});
  e=decode(b,n);CHECK_EQ(e.cableCm,44);CHECK_EQ(e.cableOffsetCm,40);CHECK(e.cableSavedNotification);
  n=synth(b,24,"551808",{{10,0x0f},{12,2},{14,0x82},{15,0x3e},{16,44},{18,0x6a},{19,0x50},{20,40}});
  e=decode(b,n);CHECK_EQ(e.cableCm,44);CHECK_EQ(e.cableOffsetCm,40);CHECK(!e.cableSavedNotification);
  for(int unit=0;unit<=1;++unit){n=synth(b,22,"551608",{{10,0x0f},{12,1},{14,0x6f},{15,0x50},{16,unit}});e=decode(b,n);CHECK_EQ(e.lengthUnitRaw,unit);CHECK_EQ(e.weightUnitRaw,-1);}
  CHECK(validFrame(cableFrame(true).data,cableFrame(true).len));CHECK_EQ(cableFrame(true).data[15],0);CHECK_EQ(cableFrame(true).data[16],0x10);
  CHECK(validFrame(cableFrame(false).data,cableFrame(false).len));CHECK(validFrame(readCableFrame().data,readCableFrame().len));
  CHECK(validFrame(readTwinFrame().data,readTwinFrame().len));
  for(int i=0;i<7;++i){const auto probe=readTwinProbeFrame(i);CHECK(validFrame(probe.data,probe.len));CHECK_EQ(probe.data[10],0x0f);CHECK_EQ(probe.data[11],1);}
  CHECK_EQ(readTwinProbeFrame(7).len,0u);
  n=synth(b,34,"552208",{{10,0x0f},{12,5},{14,0x6c},{15,0x51},{16,3},{17,0x38},{18,0x54},{19,1},{20,0xb3},{21,0x53},{22,162},{24,0xb4},{25,0x53},{26,162},{28,0xb5},{29,0x53},{30,68},{31,1}});
  e=decode(b,n);CHECK_EQ(e.twinConnection,3);CHECK_EQ(e.twinLastRole,1);CHECK_EQ(e.twinMainForce,162);CHECK_EQ(e.twinSubForce,162);CHECK_EQ(e.twinTotalForce,324);
  for(int i=0;i<MODE_PARAMETER_COUNT;++i){uint8_t storage[17];Frame f;CHECK(readModeParameterFrame(i,storage,f));CHECK(validFrame(f.data,f.len));CHECK_EQ(f.data[10],0x0f);CHECK_EQ(f.data[13]|(f.data[14]<<8),MODE_PARAMETERS[i].id);
    for(int width:{1,2,4}){n=synth(b,18+width,"551308",{{10,0x0f},{12,1},{14,int(MODE_PARAMETERS[i].id&255)},{15,int(MODE_PARAMETERS[i].id>>8)},{16,7}});e=decode(b,n);CHECK_EQ(e.modeParameter,i);CHECK_EQ(e.modeParameterValue,7);CHECK_EQ(e.modeParameterWidth,width);}
  }
  // Real 2026-10-13 unloaded trainer readback: runtime wire weight is 1 lb,
  // while the separately polled configured base weight was 6 lb.
  n=fromHex("551408ce10aa002020000f000100833e01005867",b,sizeof(b));
  CHECK(validFrame(b,n));e=decode(b,n);CHECK(e.hasRuntimeWeight);CHECK_EQ(e.runtimeWeightLbs,1);
  CHECK_EQ(e.weightLbs,-1);CHECK_EQ(e.activeWeightTenths,-1);
  for(int value:{-32768,-1,0,1,59,32767}){
    n=synth(b,20,"551408",{{10,0x0f},{12,1},{14,0x83},{15,0x3e},{16,value&255},{17,(value>>8)&255}});
    e=decode(b,n);CHECK(e.hasRuntimeWeight);CHECK_EQ(e.runtimeWeightLbs,value);
    n=synth(b,19,"551304",{{10,0x10},{11,1},{13,0x83},{14,0x3e},{15,value&255},{16,(value>>8)&255}});
    e=decode(b,n);CHECK(e.hasRuntimeWeight);CHECK_EQ(e.runtimeWeightLbs,value);
  }
  n=synth(b,19,"551308",{{10,0x0f},{12,1},{14,0x83},{15,0x3e},{16,5}});
  e=decode(b,n);CHECK(!e.hasRuntimeWeight); // truncated int16, no CRC-as-data
  for(int width:{0,1,3,4}){
    n=synth(b,18+width,"551408",{{10,0x0f},{12,1},{14,0x83},{15,0x3e},{16,6}});
    e=decode(b,n);CHECK(!e.hasRuntimeWeight);
    n=synth(b,17+width,"551304",{{10,0x10},{11,1},{13,0x83},{14,0x3e},{15,6}});
    e=decode(b,n);CHECK(!e.hasRuntimeWeight);
  }
  // Read-only telemetry setup probes have precise register widths.
  for(auto query:{readTelemetryRateFrame(),readTelemetrySubscriptionFrame(),readRuntimeWireWeightFrame()}){
    CHECK(validFrame(query.data,query.len));CHECK_EQ(query.data[10],0x0f);CHECK_EQ(query.data[11],1);
  }
  for(int hz:{0,10,40,100}){
    n=synth(b,19,"551308",{{10,0x0f},{12,1},{14,0x82},{15,0x51},{16,hz}});
    e=decode(b,n);CHECK_EQ(e.telemetryRateHz,hz);CHECK_EQ(e.mode,-1);
    b[11]=1;e=decode(b,n);CHECK_EQ(e.telemetryRateHz,-1);
  }
  for(uint32_t mask:{0u,0x7bf5u,0x00657bf5u,0xffffffffu}){
    n=synth(b,22,"551608",{{10,0x0f},{12,1},{14,0x83},{15,0x51}});
    for(int i=0;i<4;++i)b[16+i]=(mask>>(8*i))&255;
    e=decode(b,n);CHECK(e.hasTelemetrySubscription);CHECK_EQ(e.telemetrySubscription,mask);CHECK_EQ(e.mode,-1);
    e=decode(b,n-1);CHECK(!e.hasTelemetrySubscription);
    b[11]=1;e=decode(b,n);CHECK(!e.hasTelemetrySubscription);
  }
  // Garbage and short input.
  uint8_t junk[3] = {1, 2, 3};
  CHECK(decode(junk, 3).type == EventType::Unknown);
  CHECK(decode(nullptr, 0).type == EventType::Unknown);
}

// Synthetic layout regressions against the verified Cortex decoder contract.
// These are not physical Twin telemetry captures; device validation is separate.
static void testTwinRuntime(){
 uint8_t b[160];
 for(bool lengthByte:{false,true})for(int bodySize:{64,66}){
   const int start=lengthByte?13:12;
   const size_t n=synth(b,start+bodySize+2,"550004",{{8,0x20},{10,0xaa},{11,0x81}});
   if(lengthByte)b[12]=bodySize;
   auto put=[&](int off,int value){b[start+off]=value&255;b[start+off+1]=(value>>8)&255;};
   b[start]=1;put(1,3);put(3,7);put(9,40);put(11,300);put(13,-200);
   b[start+43]=3;put(44,60);put(46,500);put(48,250);
   auto e=decode(b,n);CHECK(e.type==EventType::Telemetry);CHECK(e.twinTelemetry);CHECK(e.hasTwinPairMotion);
   CHECK_EQ(e.repCount,7);CHECK_EQ(e.setCounter,3);CHECK_EQ(e.activeWeightTenths,40);CHECK_EQ(e.peerForceTenths,60);
   CHECK_EQ(e.cableVelocityMmPerSecond,-200);CHECK_EQ(e.peerCableVelocityMmPerSecond,250);
   CHECK(!e.hasTargetForceReport); // Do not mislabel local target as pair total.
   b[11]=0xa1;e=decode(b,n);CHECK(!e.hasTwinPairMotion);CHECK_EQ(e.repCount,-1);
 }
 for(int length:{29,31,41,43,52,54,63,65,67}){
   const size_t n=synth(b,15+length,"550004",{{8,0x20},{10,0xaa},{11,0x81},{12,length}});
   CHECK(!decode(b,n).hasTwinPairMotion);
 }
 const size_t n=synth(b,31,"550004",{{8,0x12},{10,0xb4},{11,40},{17,3},{19,7}});
 auto e=decode(b,n);CHECK(e.twinTelemetry);CHECK_EQ(e.repCount,7);CHECK_EQ(e.setCounter,3);
 CHECK(e.hasTwinPairMotion);CHECK_EQ(e.activeWeightTenths,40);CHECK_EQ(e.peerForceTenths,0);
 b[19]=0;b[20]=0;e=decode(b,n);CHECK_EQ(e.repCount,0);
 b[9]=1;e=decode(b,n);CHECK_EQ(e.repCount,-1);b[9]=0;
 for(int source:{0x10,0x11,0x13}){b[8]=source;CHECK_EQ(decode(b,n).repCount,-1);}
 b[8]=0x12;CHECK_EQ(decode(b,n-1).repCount,-1);
}

// Actual Twin samples captured during five reps: a stationary cable, return
// motion, a range update, and a completed-count update. No device IDs are stored.
static void testCapturedTwinRuntime(){
 struct Fixture{const char* hex;int force,peer,velocity,peerVelocity,sets,reps;};
 for(const auto& fixture:{
   Fixture{"551b047510aa1e9e1000b40600020000003c00080002000000456f",6,8,0,0,-1,-1},
   Fixture{"551b047510aaa6b11000b46700640091fe67006b00630085fe657c",103,107,-367,-379,-1,-1},
   Fixture{"551f044e10aa72a91100b44200bd0064fe00000e013c003f00c200f7fec097",66,63,-412,-265,-1,-1},
   Fixture{"551f044e10aa0ea91200b43d00f20085fe030001003c003f00ed0032fe55c3",61,63,-379,-462,3,1}}){
   uint8_t bytes[40]{};const size_t size=std::strlen(fixture.hex)/2;
   for(size_t i=0;i<size;++i){unsigned value;std::sscanf(fixture.hex+2*i,"%2x",&value);bytes[i]=value;}
   CHECK(validFrame(bytes,size));const auto e=decode(bytes,size);
   CHECK(e.twinTelemetry);CHECK(e.hasTwinPairMotion);CHECK(e.hasMotionTelemetry);
   CHECK_EQ(e.activeWeightTenths,fixture.force);CHECK_EQ(e.peerForceTenths,fixture.peer);
   CHECK_EQ(e.cableVelocityMmPerSecond,fixture.velocity);CHECK_EQ(e.peerCableVelocityMmPerSecond,fixture.peerVelocity);
   CHECK_EQ(e.setCounter,fixture.sets);CHECK_EQ(e.repCount,fixture.reps);
   CHECK(!e.hasTargetForceReport);CHECK_EQ(e.targetForceTenths,-1);
   for(size_t length=13;length<size;++length)CHECK(!decode(bytes,length).hasTwinPairMotion);
   CHECK(!decode(bytes,size+1).hasTwinPairMotion);CHECK(!decode(bytes,size+2).hasTwinPairMotion);
   // A missing target cannot erase the two actual measurements.
   const size_t target=bytes[8]==0x10?17:21;bytes[target]=bytes[target+1]=0xff;
   CHECK_EQ(decode(bytes,size).peerForceTenths,fixture.peer);
   CHECK_EQ(decode(bytes,size).activeWeightTenths,fixture.force);
   bytes[9]=1;CHECK(!decode(bytes,size).hasTwinPairMotion);bytes[9]=0;
   bytes[2]=8;CHECK(!decode(bytes,size).hasTwinPairMotion);
 }
}

MINITEST_MAIN({ testFrames(); testDecode(); testTwinRuntime(); testCapturedTwinRuntime(); })
