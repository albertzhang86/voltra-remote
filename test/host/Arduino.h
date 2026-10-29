#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// ESP32 and macOS provide strlcpy; Ubuntu 22.04's libc does not. Keep the
// actual UI source portable in the native touch tests without changing firmware.
inline size_t hostStrlcpy(char* dest,const char* source,size_t capacity){
  const size_t length=std::strlen(source);
  if(capacity){const size_t copied=length<capacity?length:capacity-1;std::memcpy(dest,source,copied);dest[copied]='\0';}
  return length;
}
#define strlcpy hostStrlcpy
uint32_t millis();
