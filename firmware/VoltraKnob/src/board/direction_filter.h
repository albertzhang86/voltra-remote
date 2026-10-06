#pragma once
#include <cstdint>

namespace board {
// This board produces independent active-low direction pulses. Captured genuine
// pulses lasted tens of milliseconds; opposite-pin release glitches lasted only
// microseconds. Qualify both press and release, without delaying real reversals.
class DirectionFilter {
 public:
  bool sample(bool low, uint32_t us) {
    if (low != candidate_) { candidate_ = low; changedUs_ = us; }
    if (candidate_ == stable_ || uint32_t(us - changedUs_) < 1000) return false;
    stable_ = candidate_;
    return stable_; // Exactly one event per qualified low pulse.
  }
 private:
  bool candidate_ = false, stable_ = false;
  uint32_t changedUs_ = 0;
};
}
