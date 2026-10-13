#pragma once
#include <cstdint>
namespace board {
struct RotateEvent { int8_t direction; uint32_t ms; };
bool begin();
// Estimated knob battery percentage; -1 means no valid voltage reading.
int batteryPercent();
void logInputs();
void traceDial(const char* reason,int field,int direction,int before,int target,int actual,bool pending,bool sent,uint32_t eventMs);
bool pollRotate(RotateEvent& out);
void setBacklight(uint8_t level);
void setRingColor(uint32_t rgb);
}
