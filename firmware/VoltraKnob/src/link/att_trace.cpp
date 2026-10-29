#include "att_trace.h"
#include "att_trace_buffer.h"
#include <Arduino.h>

namespace {
portMUX_TYPE traceMutex=portMUX_INITIALIZER_UNLOCKED;
vlink::AttTraceBuffer trace;
const char* kindName(uint8_t kind){
  switch(kind){
  case 1:return "queued";
  case 2:return "att-tx";
  case 3:return "att-result";
  case 4:return "gatt-start";
  case 5:return "att-rx";
  case 6:return "write-match";
  case 7:return "gatt-result";
  case 8:return "timeout";
  case 9:return "hci-tx";
  case 10:return "hci-result";
  default:return "unknown";
  }
}
}

// Called by the pinned NimBLE diagnostic hooks. No allocation, USB output,
// BLE calls, or waiting on the application's state mutex from this callback.
extern "C" void voltra_att_trace_event(uint8_t kind,uint16_t connection,
                                     uint16_t cid,uint16_t detail,int32_t result){
  const uint32_t now=millis();
  portENTER_CRITICAL(&traceMutex);
  trace.record({now,result,connection,cid,detail,kind});
  portEXIT_CRITICAL(&traceMutex);
}

namespace vlink {
void watchAttTrace(uint16_t connection){
  portENTER_CRITICAL(&traceMutex);trace.watch(connection);portEXIT_CRITICAL(&traceMutex);
}
void freezeAttTrace(){
  portENTER_CRITICAL(&traceMutex);trace.freeze();portEXIT_CRITICAL(&traceMutex);
}
void printAttTrace(){
  // Drain at most one line per worker iteration, only when the USB buffer fits.
  if(Serial.availableForWrite()<220)return;
  AttTraceEvent event;uint32_t notifications,lastNotification;
  portENTER_CRITICAL(&traceMutex);
  const bool available=trace.pop(event);
  notifications=trace.notifications();lastNotification=trace.lastNotification();
  portEXIT_CRITICAL(&traceMutex);
  if(!available)return;
  Serial.printf("[att-trace] at=%lu event=%s conn=%u cid=%u detail=%u result=%ld notifications=%lu lastNotifyMs=%lu\n",
    (unsigned long)event.at,kindName(event.kind),event.connection,event.cid,event.detail,
    (long)event.result,(unsigned long)notifications,(unsigned long)lastNotification);
}
}
