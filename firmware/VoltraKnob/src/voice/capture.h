#pragma once
#include <cstddef>
#include <cstdint>
namespace voice_capture {
bool start();
bool armed();
void raw(const int16_t*,size_t);
void loop();
bool complete();
bool started();
}
