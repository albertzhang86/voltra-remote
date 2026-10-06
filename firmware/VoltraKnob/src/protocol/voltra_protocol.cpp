#include "voltra_protocol.h"

namespace voltra {

bool readModeParameterFrame(int index,uint8_t* storage,Frame& out){
 if(index<0||index>=MODE_PARAMETER_COUNT||!storage)return false;
 const uint16_t id=MODE_PARAMETERS[index].id;
 const uint8_t bytes[17]={0x55,17,4,0,0xaa,0x10,0,0x20,0x20,0,0x0f,1,0,uint8_t(id),uint8_t(id>>8),0,0};
 for(int i=0;i<17;++i)storage[i]=bytes[i];
 uint8_t c8=0x77;for(int i=0;i<3;++i){c8^=storage[i];for(int b=0;b<8;++b)c8=(c8>>1)^((c8&1)?0x8c:0);}storage[3]=c8;
 uint16_t c16=0x3692;for(int i=0;i<15;++i){c16^=storage[i];for(int b=0;b<8;++b)c16=(c16>>1)^((c16&1)?0x8408:0);}storage[15]=c16&255;storage[16]=c16>>8;
 out={storage,17};return true;
}
namespace {
bool parameterWrite(uint16_t id,int value,int width,uint8_t* storage,Frame& out){
 if(!storage)return false;const int size=17+width;
 const uint8_t header[]={0x55,uint8_t(size),4,0,0xaa,0x10,0,0x20,0x20,0,0x11,1,0,uint8_t(id),uint8_t(id>>8)};
 for(int i=0;i<15;++i)storage[i]=header[i];
 for(int i=0;i<width;++i)storage[15+i]=(uint32_t(value)>>(8*i))&255;
 uint8_t c8=0x77;for(int i=0;i<3;++i){c8^=storage[i];for(int b=0;b<8;++b)c8=(c8>>1)^((c8&1)?0x8c:0);}storage[3]=c8;
 uint16_t crc=0x3692;for(int i=0;i<size-2;++i){crc^=storage[i];for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0x8408:0);}
 storage[size-2]=crc&255;storage[size-1]=crc>>8;out={storage,size_t(size)};return true;
}
}
bool isokineticFrame(int index,int value,uint8_t* storage,Frame& out){
 if(!validIsokineticSetting(index,value))return false;
 return parameterWrite(MODE_PARAMETERS[index].id,value,index==7?4:index==8?1:2,storage,out);
}
bool damperLevelFrame(int index,uint8_t* storage,Frame& out){
 if(!storage||index<0||index>9)return false;
 const uint8_t bytes[18]={0x55,18,4,0,0xaa,0x10,0,0x20,0x20,0,0x11,1,0,3,0x51,uint8_t(index),0,0};
 for(int i=0;i<18;++i)storage[i]=bytes[i];
 uint8_t c8=0x77;for(int i=0;i<3;++i){c8^=storage[i];for(int b=0;b<8;++b)c8=(c8>>1)^((c8&1)?0x8c:0);}storage[3]=c8;
 uint16_t crc=0x3692;for(int i=0;i<16;++i){crc^=storage[i];for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0x8408:0);}
 storage[16]=crc&255;storage[17]=crc>>8;out={storage,18};return true;
}
bool bandMaximumFrame(int pounds,uint8_t* storage,Frame& out){
 if(!storage||pounds<15||pounds>200)return false;
 // Same single-register write envelope as weight; dedicated Band register.
 for(int i=0;i<19;++i)storage[i]=frames::WEIGHT[pounds-WEIGHT_MIN][i];
 storage[13]=0x62;storage[14]=0x53;
 uint16_t crc=0x3692;for(int i=0;i<17;++i){crc^=storage[i];for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0x8408:0);}
 storage[17]=crc&255;storage[18]=crc>>8;out={storage,19};return true;
}
Frame authFrame() { return {frames::AUTH_KNOB, sizeof(frames::AUTH_KNOB)}; }
Frame authFrameFallback() { return {frames::AUTH_IPHONE, sizeof(frames::AUTH_IPHONE)}; }
Frame telemetrySubscribeAllFrame() { return {frames::TELEMETRY_SUBSCRIBE_ALL, sizeof(frames::TELEMETRY_SUBSCRIBE_ALL)}; }
Frame readWeightUnitFrame() { return {frames::READ_WEIGHT_UNIT, sizeof(frames::READ_WEIGHT_UNIT)}; }
Frame readCableFrame(){return {frames::READ_CABLE,sizeof(frames::READ_CABLE)};}
Frame readLengthUnitFrame(){return {frames::READ_LENGTH_UNIT,sizeof(frames::READ_LENGTH_UNIT)};}
Frame cableFrame(bool open){return open?Frame{frames::CABLE_OPEN,sizeof(frames::CABLE_OPEN)}:Frame{frames::CABLE_CLOSE,sizeof(frames::CABLE_CLOSE)};}
Frame readTwinProbeFrame(int index){
 switch(index){
 case 0:return {frames::READ_TWIN_0,sizeof(frames::READ_TWIN_0)};
 case 1:return {frames::READ_TWIN_1,sizeof(frames::READ_TWIN_1)};
 case 2:return {frames::READ_TWIN_2,sizeof(frames::READ_TWIN_2)};
 case 3:return {frames::READ_TWIN_3,sizeof(frames::READ_TWIN_3)};
 case 4:return {frames::READ_TWIN_4,sizeof(frames::READ_TWIN_4)};
 case 5:return {frames::READ_TWIN_5,sizeof(frames::READ_TWIN_5)};
 case 6:return {frames::READ_TWIN_6,sizeof(frames::READ_TWIN_6)};
 default:return {nullptr,0};
 }
}
Frame readTwinFrame(){return {frames::READ_TWIN,sizeof(frames::READ_TWIN)};}
Frame readSettingsFrame() { return {frames::READ_SETTINGS, sizeof(frames::READ_SETTINGS)}; }
Frame guidedLoadTriggerFrame() { return {frames::GUIDED_LOAD_TRIGGER, sizeof(frames::GUIDED_LOAD_TRIGGER)}; }
Frame readGuidedLoadStatusFrame() { return {frames::READ_GUIDED_LOAD_STATUS, sizeof(frames::READ_GUIDED_LOAD_STATUS)}; }
Frame initFrame(size_t i) {
  if (i == 0) return {frames::INIT0, sizeof(frames::INIT0)};
  return {frames::INIT1, sizeof(frames::INIT1)};
}
Frame setupFrame() { return {frames::SETUP, sizeof(frames::SETUP)}; }
Frame goFrame() { return {frames::GO, sizeof(frames::GO)}; }
Frame stopFrame() { return {frames::STOP, sizeof(frames::STOP)}; }
Frame modeWeightTrainingFrame() { return {frames::MODE_WEIGHT_TRAINING, sizeof(frames::MODE_WEIGHT_TRAINING)}; }
Frame modeIdleFrame() { return {frames::MODE_IDLE, sizeof(frames::MODE_IDLE)}; }

bool modeFrame(int mode,Frame& out) {
 switch(mode) {
  case 0:out=modeIdleFrame();return true;
  case 1:out=modeWeightTrainingFrame();return true;
  case 2:out={frames::MODE_RESISTANCEBAND,sizeof(frames::MODE_RESISTANCEBAND)};return true;
  case 3:out={frames::MODE_ROWING,sizeof(frames::MODE_ROWING)};return true;
  case 4:out={frames::MODE_DAMPER,sizeof(frames::MODE_DAMPER)};return true;
  case 6:out={frames::MODE_CUSTOMCURVES,sizeof(frames::MODE_CUSTOMCURVES)};return true;
  case 7:out={frames::MODE_ISOKINETIC,sizeof(frames::MODE_ISOKINETIC)};return true;
  case 8:out={frames::MODE_ISOMETRIC,sizeof(frames::MODE_ISOMETRIC)};return true;
  default:return false;
 }
}
bool weightFrame(int lbs, Frame& out) {
  if (lbs < WEIGHT_MIN || lbs > WEIGHT_MAX) return false;
  out = {frames::WEIGHT[lbs - WEIGHT_MIN], frames::WEIGHT_FRAME_LEN};
  return true;
}

bool chainsFrame(int lbs, Frame& out) {
  if (lbs < frames::CHAINS_MIN || lbs > frames::CHAINS_MAX) return false;
  out = {frames::CHAINS[lbs - frames::CHAINS_MIN], sizeof(frames::CHAINS[0])}; return true;
}
bool inverseChainsFrame(int lbs, Frame& out) {
  if(lbs<0||lbs>1)return false;
  out={frames::INVERSE_CHAINS[lbs-frames::INVERSE_CHAINS_MIN],sizeof(frames::INVERSE_CHAINS[0])};return true;
}
bool eccentricFrame(int percent, Frame& out) {
  if (percent < frames::ECCENTRIC_MIN || percent > frames::ECCENTRIC_MAX) return false;
  out = {frames::ECCENTRIC[percent - frames::ECCENTRIC_MIN], sizeof(frames::ECCENTRIC[0])}; return true;
}
// Validate both checksums before any received state can authorize a motor command.
bool validFrame(const uint8_t* d, size_t n) {
  if (!d || n < 13 || d[0] != 0x55 || d[1] != n) return false;
  uint8_t c8 = 0x77;
  for (size_t i=0; i<3; ++i) { c8 ^= d[i]; for(int b=0;b<8;++b) c8 = (c8>>1) ^ ((c8&1)?0x8c:0); }
  if(c8 != d[3]) return false;
  uint16_t c16 = 0x3692;
  for(size_t i=0;i<n-2;++i) { c16 ^= d[i]; for(int b=0;b<8;++b) c16 = (c16>>1) ^ ((c16&1)?0x8408:0); }
  return d[n-2] == (c16&255) && d[n-1] == (c16>>8);
}
namespace {
inline int u16le(const uint8_t* d, size_t off) { return d[off] | (d[off + 1] << 8); }
inline bool vendor(const uint8_t* d, size_t n, uint8_t a, uint8_t b) {
  return n >= 13 && d[10] == 0xaa && d[11] == a && d[12] == b;
}
// Widths for the cmd 0x0f bulk response (SDK CMD_0F_KNOWN_PARAM_WIDTHS). 0 = unknown, stop parsing.
inline size_t cmd0fWidth(uint8_t hi, uint8_t lo) {
  if(lo==0x51&&hi==0x6c)return 1; // COWORKER_CONN_ST
  if(lo==0x54&&hi==0x38)return 1; // LAST_COWORKER_ROLE (saved, not proof of active Twin)
  if(lo==0x53&&(hi==0xb3||hi==0xb4||hi==0xb5))return 2;
  if (lo == 0x3e && (hi == 0x86 || hi == 0x87 || hi == 0x88 || hi == 0x89 || hi == 0x82)) return 2;
  if (lo == 0x50 && hi == 0x6a) return 2;
  if(lo==0x50&&hi==0x11)return 1;
  if (lo == 0x53 && (hi == 0x62 || hi == 0xb7 || hi == 0xd2)) return 2;
  if (lo == 0x54 && hi == 0x31) return 2;
  if (lo == 0x53 && hi == 0xc8) return 2;                                    // guided-load countdown
  if (lo == 0x53 && (hi == 0x8d || hi == 0xc7 || hi == 0xc9)) return 1;      // guided-load safety, state, ctrl
  if (lo == 0x53 && (hi == 0x61 || hi == 0xb6 || hi == 0xb0 || hi == 0xc6)) return 1;
  if (lo == 0x52 && hi == 0xe3) return 1;
  if (lo == 0x51 && (hi == 0x06 || hi == 0x03)) return 1;
  if (lo == 0x4f && hi == 0xb0) return 1;
  return 0;
}
inline void applyParam(Event& e, uint8_t hi, uint8_t lo, int value) {
  if(lo==0x51&&hi==0x6c)e.twinConnection=value;
  else if(lo==0x54&&hi==0x38)e.twinLastRole=value;
  else if(lo==0x53&&hi==0xb3)e.twinMainForce=value;
  else if(lo==0x53&&hi==0xb4)e.twinSubForce=value;
  else if(lo==0x53&&hi==0xb5)e.twinTotalForce=value;
  else if (hi == 0x86 && lo == 0x3e) e.weightLbs = value;
  else if (hi == 0x87 && lo == 0x3e) e.chainsLbs = value;
  else if (hi == 0x88 && lo == 0x3e) e.eccentricLbs = static_cast<int16_t>(value);
  else if(hi==0x11&&lo==0x50)e.cableScreenId=value;
  else if(hi==0x82&&lo==0x3e)e.cableCm=static_cast<int16_t>(value);
  else if(hi==0x6a&&lo==0x50)e.cableOffsetCm=value;
  else if (hi == 0xb0 && lo == 0x53) e.inverseChainsEnabled = value;
  else if (hi == 0xb0 && lo == 0x4f) e.mode = value;
  else if (hi == 0x89 && lo == 0x3e) e.fitnessMode = value;
  else if (hi == 0xc8 && lo == 0x53) e.guidedCountdownMs = value;
  else if (hi == 0x8d && lo == 0x53) e.guidedSafety = value;
  else if (hi == 0xc7 && lo == 0x53) e.guidedState = value;
  else if (hi == 0xc9 && lo == 0x53) e.guidedCtrl = value;
}
}  // namespace

Event decode(const uint8_t* d, size_t n) {
  Event e;
  if (d == nullptr || n < 4) return e;

  if(n>=19&&d[0]==0x55&&(d[2]==0x08||d[2]==0x09)&&d[10]==0x0f&&d[11]==0&&d[12]==1&&d[13]==0){
    const int index=modeParameterIndex(d[14]|(d[15]<<8));const size_t width=n-18;
    if(index>=0&&(width==1||width==2||width==4)){
      uint32_t raw=0;for(size_t i=0;i<width;++i)raw|=uint32_t(d[16+i])<<(8*i);
      if(raw<=0x7fffffff){e.type=EventType::Settings;e.modeParameter=index;e.modeParameterValue=int(raw);e.modeParameterWidth=width;}
      return e;
    }
  }
  // Isolated unit query: parameter 0x506e, documented as uint32 in vendor catalog.
  if(n >= 19 && d[0]==0x55 && (d[2]==0x08||d[2]==0x09) && d[10]==0x0f
     && d[12]==1 && d[13]==0 && (d[14]==0x6e||d[14]==0x6f) && d[15]==0x50) {
    const size_t width=n-18;
    if(width==1 || width==2 || width==4){
      uint32_t raw=0;for(size_t i=0;i<width;++i)raw|=static_cast<uint32_t>(d[16+i])<<(8*i);
      if(raw<=0x7fffffff){e.type=EventType::Settings;if(d[14]==0x6e)e.weightUnitRaw=static_cast<int>(raw);else e.lengthUnitRaw=static_cast<int>(raw);}
    }
    return e;
  }
  if (n >= 30 && d[0] == 0x55 && d[1] == 0x3a && d[2] == 0x04 && d[3] == 0x70) {
    e.type = EventType::Telemetry; e.phase = d[13]; return e;
  }
  if (vendor(d, n, 0x82, 0x3b) && n >= 74) {
    e.type = EventType::PerRep; e.setCounter = d[15]; e.repCount = d[17]; return e;
  }
  if (vendor(d, n, 0x85, 0x5f) && n >= 110) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 26); return e;
  }
  if (vendor(d, n, 0x86, 0x7d) && n >= 140) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 16); return e;
  }
  if (vendor(d, n, 0x80, 0x25) && n >= 52) {
    return e;  // state dump; aliases the 5534 header, never read battery from it
  }
  if (n >= 16 && d[10] == 0x10) {
    e.type = EventType::Settings;
    const int count = d[11];
    size_t off = 13;
    for (int i = 0; i < count && i < 16; ++i) {
      if (off + 2 > n) break;
      const uint8_t hi = d[off], lo = d[off + 1];
      off += 2;
      const size_t width = cmd0fWidth(hi,lo);
      if (!width || off + width > n-2) break;
      const int value = width == 2 ? u16le(d, off) : d[off];
      off += width;
      applyParam(e, hi, lo, value);
      if(hi==0x6a&&lo==0x50)e.cableSavedNotification=true;
    }
    return e;
  }
  if (n >= 14 && d[0] == 0x55 && (d[2] == 0x08 || d[2] == 0x09) && d[10] == 0x0f) {
    e.type = EventType::Settings;
    const int count = u16le(d, 12);
    size_t off = 14;
    for (int i = 0; i < count && i < 32; ++i) {
      if (off + 2 > n) break;
      const uint8_t hi = d[off], lo = d[off + 1];
      off += 2;
      const size_t width = cmd0fWidth(hi, lo);
      if (width == 0 || off + width > n) break;
      const int value = width == 2 ? u16le(d, off) : d[off];
      off += width;
      applyParam(e, hi, lo, value);
    }
    return e;
  }
  if (d[0] == 0x55 && d[1] == 0x12 && n >= 18) {
    e.type = EventType::ModeConfirmed;
    if (d[13] == 0x89 && d[14] == 0x3e) e.fitnessMode = d[15];
    else e.mode = d[15];
    return e;
  }
  if (d[0] == 0x55 && d[1] == 0x23 && n >= 35) { e.type = EventType::Battery; e.battery = d[11]; return e; }
  if (d[0] == 0x55 && d[1] == 0x34 && n >= 52) { e.type = EventType::Battery; e.battery = d[12]; return e; }
  return e;
}

}  // namespace voltra
