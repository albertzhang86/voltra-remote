#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace voice {
// One audio producer per buffer. Published samples are immutable. Storage and
// counters are never reset while producers run; one bounded capture per boot.
class CaptureBuffer {
 public:
  void arm(int16_t* storage,size_t capacity){data_=storage;capacity_=capacity;armed_.store(true,std::memory_order_release);}
  void append(const int16_t* samples,size_t count){
   if(!armed_.load(std::memory_order_acquire))return;
   size_t used=used_.load(std::memory_order_relaxed);
   size_t n=std::min(count,capacity_-used);
   if(n){std::memcpy(data_+used,samples,n*sizeof(int16_t));used_.store(used+n,std::memory_order_release);}
  }
  size_t size()const{return used_.load(std::memory_order_acquire);}
 private:
  std::atomic<bool> armed_{false};
  std::atomic<size_t> used_{0};
  int16_t* data_=nullptr;size_t capacity_=0;
};
}
