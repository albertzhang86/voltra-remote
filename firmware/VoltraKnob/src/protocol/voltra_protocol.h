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
Frame readWeightUnitFrame();
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
Frame modeWeightTrainingFrame();
Frame modeIdleFrame();
bool modeFrame(int mode,Frame& out);
bool weightFrame(int lbs, Frame& out);
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
  int twinConnection=-1,twinLastRole=-1,twinMainForce=-1,twinSubForce=-1,twinTotalForce=-1;
  int cableScreenId=-1;
  int lengthUnitRaw=-1,cableCm=-1,cableOffsetCm=-1;
  bool cableSavedNotification=false;
  int chainsLbs = -1;
  int inverseChainsEnabled = -1;
  int eccentricLbs = -1000;
  int mode = -1;        // Settings, ModeConfirmed (0 idle, 1 weight training, ...)
  int fitnessMode = -1; // Settings, ModeConfirmed: 893e register; 5 or 0x27 = motor engaged, 4 = ready, 0x26 = auto-load armed
  int guidedCountdownMs = -1; // Settings: 53c8 register while auto-load is armed
  int guidedSafety = -1;      // Settings: 538d register (0 = ok)
  int guidedState = -1;       // Settings: 53c7 register
  int guidedCtrl = -1;        // Settings: 53c9 register
  int battery = -1;     // Battery, 0..100
  int phase = -1;       // Telemetry: 0 idle, 1 concentric, 2 hold, 3 eccentric
  int repCount = -1;    // PerRep, SetSummary
  int setCounter = -1;  // PerRep
};

constexpr int FITNESS_MODE_AUTOLOAD_ARMED = 0x26;
// 0x27 (direct-load active) is reported right after arming, before the pull, so it counts as armed.
constexpr bool fitnessModeLoaded(int fm) { return fm == 5 || fm == 21; }
constexpr bool fitnessModeArmed(int fm) { return fm == FITNESS_MODE_AUTOLOAD_ARMED || fm == 0x27; }
constexpr bool fitnessModeUnloaded(int fm) { return fm == 4 || fm == 0; }

// Decode one BLE notification. Never throws; unknown or short input yields EventType::Unknown.
Event decode(const uint8_t* d, size_t n);

}  // namespace voltra
