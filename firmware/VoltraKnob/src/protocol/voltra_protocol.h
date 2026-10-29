#pragma once
#include "mode_parameters.h"
#include <cstddef>
#include <cstdint>
#include "frames.gen.h"

namespace voltra {

struct Frame { const uint8_t* data; size_t len; };

constexpr int WEIGHT_MIN = frames::WEIGHT_MIN;
constexpr int WEIGHT_MAX = frames::WEIGHT_MAX;
constexpr size_t INIT_COUNT = frames::INIT_COUNT;
constexpr const char* SERVICE_UUID = frames::SERVICE_UUID;
constexpr const char* WRITE_CHAR_UUID = frames::WRITE_CHAR_UUID;
constexpr const char* NOTIFY_CHAR_UUID = frames::NOTIFY_CHAR_UUID;
constexpr const char* DEVICE_NAME_PREFIX = frames::DEVICE_NAME_PREFIX;

Frame authFrame();                 // custom "Voltra Knob" identity
Frame authFrameFallback();         // SDK's iPhone identity, used if the custom one is rejected
Frame telemetrySubscribeAllFrame();
Frame readRuntimeWireWeightFrame();
Frame readTelemetryRateFrame();
Frame readTelemetrySubscriptionFrame();
Frame readWeightUnitFrame();
// Read the device-enforced ceiling; never write it or enable Overdrive.
bool readMaximumForceFrame(uint8_t* storage,Frame& out);
Frame readSettingsFrame();
// Caller supplies 19 bytes; Band maximum is uint16 lb, range 15..200.
// Caller supplies 21 bytes. Captured Isokinetic parameter widths are 4/1/2 bytes.
bool isokineticFrame(int index,int value,uint8_t* storage,Frame& out);
bool damperLevelFrame(int index,uint8_t* storage,Frame& out);
bool bandMaximumFrame(int pounds,uint8_t* storage,Frame& out);
bool readModeParameterFrame(int index,uint8_t* storage,Frame& out);
Frame readTwinFrame();
Frame readTwinProbeFrame(int index);
Frame readCableFrame(); Frame readLengthUnitFrame(); Frame cableFrame(bool open);         // cmd 0x0f read of base weight, fitness mode, training mode
Frame guidedLoadTriggerFrame();    // arm auto-load: device runs safety check + countdown, then engages
Frame readGuidedLoadStatusFrame(); // cmd 0x0f read of the guided-load status registers
Frame initFrame(size_t i);
Frame setupFrame();
Frame goFrame();
Frame stopFrame();
// Writes a known motor encoding without inventing flags. Caller supplies 19 bytes.
bool motorFrame(int reportedState,bool load,uint8_t* storage,Frame& out);
Frame modeWeightTrainingFrame();
Frame modeIdleFrame();
bool modeFrame(int mode,Frame& out);
bool weightFrame(int lbs, Frame& out);
bool weightFrame(int lbs, uint8_t* storage, Frame& out);
bool eccentricFrame(int lbs, uint8_t* storage, Frame& out);
bool chainsFrame(int lbs, Frame& out);
bool inverseChainsFrame(int lbs, Frame& out);
bool eccentricFrame(int percent, Frame& out);
bool validFrame(const uint8_t* data, size_t size);

enum class EventType : uint8_t { Unknown, Settings, ModeConfirmed, Battery, Telemetry, PerRep, SetSummary };

struct Event {
  int modeParameter=-1,modeParameterValue=-1,modeParameterWidth=0;
  EventType type = EventType::Unknown;
  int weightLbs = -1;   // Settings
  int weightUnitRaw = -1;
  int maximumForceLbs = -1;
  int telemetryRateHz = -1;
  bool hasTelemetrySubscription = false;
  uint32_t telemetrySubscription = 0;
  int twinConnection=-1,twinLastRole=-1,twinMainForce=-1,twinSubForce=-1,twinTotalForce=-1;
  int cableScreenId=-1;
  int lengthUnitRaw=-1,cableCm=-1,cableOffsetCm=-1;
  bool cableSavedNotification=false;
  int chainsLbs = -1;
  int inverseChainsEnabled = -1;
  int eccentricLbs = -1000;
  int mode = -1;        // Settings, ModeConfirmed (0 idle, 1 weight training, ...)
  int fitnessMode = -1; // Raw 0x3e89 motor report; interpreted by the helpers below, separately from mode.
  int guidedCountdownMs = -1; // Settings: 53c8 register while auto-load is armed
  int guidedSafety = -1;      // Settings: 538d register (0 = ok)
  int guidedState = -1;       // Settings: 53c7 register
  int guidedCtrl = -1;        // Settings: 53c9 register
  int battery = -1;     // Battery, 0..100
  int activeWeightTenths = -1; // Live cable force in tenths of a pound; never set weight.
  bool hasMotionTelemetry = false; // Force, cable length and velocity from the same B4 sample.
  int cableLengthMm = -1;
  int cableVelocityMmPerSecond = 0;
  // Twin runtime reports belong to the confirmed logical controller. A local
  // force alone is never a pair total; both channels must share one packet.
  bool twinTelemetry = false;
  bool hasTwinPairMotion = false;
  int peerForceTenths = -1;
  int peerCableVelocityMmPerSecond = 0;
  bool hasTargetForceReport = false; // B4 report, including an absent/invalid target.
  int targetForceTenths = -1; // Device target resistance, independent of measured cable force.
  bool hasRuntimeWeight = false;
  int runtimeWeightLbs = 0; // Signed whole-lb device runtime register 0x3e83.
  int phase = -1;       // Telemetry: 0 idle, 1 concentric, 2 hold, 3 eccentric
  int repCount = -1;    // Pull index + 1; return and SetSummary totals unchanged.
  int setCounter = -1;  // PerRep
};

constexpr int FITNESS_MODE_AUTOLOAD_ARMED = 0x26;
// Register 0x3e89 is independent of training mode. Native load/unload on
// VTR-161230 Firmware 2.0 reports 1/0 (2026-10-28); legacy reports 5/4.
// Decode only observed words, never mask arbitrary flags into load permission.
// An unloaded word alone is NOT readiness: the link checks mode, endpoint,
// settings, and fresh cable-zero readbacks before any motor write.
constexpr bool fitnessModeReady(int fm) { return fm == 0 || fm == 4 || fm == 0x0104; }
// 0x27 (direct-load active) is reported right after arming, before the pull, so it counts as armed.
constexpr bool fitnessModeLoaded(int fm) { return fm == 1 || fm == 5 || fm == 21; }
constexpr bool fitnessModeArmed(int fm) { return fm == FITNESS_MODE_AUTOLOAD_ARMED || fm == 0x27; }
constexpr bool fitnessModeUnloaded(int fm) { return fitnessModeReady(fm); }

// Keep the observed 0/1 family separate from the legacy 4/5 command family.
// Unknown states must not be normalized by clearing high bits.
constexpr int motorCommandValue(int fm,bool load){
 if(load)return fitnessModeReady(fm)?(fm==0?1:5):-1;
 if(fm==0||fm==1)return 0;
 return fitnessModeReady(fm)||fitnessModeLoaded(fm)||fitnessModeArmed(fm)?4:-1;
}

// Decode one BLE notification. Never throws; unknown or short input yields EventType::Unknown.
Event decode(const uint8_t* d, size_t n);

}  // namespace voltra
