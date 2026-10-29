# Pinned NimBLE source fixtures

These Apache-2.0 sources were copied from the installed NimBLE-Arduino 2.5.1
package before applying the host-queued-write patch. Original copyright and
license notices remain in each file. Upstream: https://github.com/h2zero/NimBLE-Arduino

- `NimBLERemoteValueAttribute.cpp` and `.h`: complete upstream files.
- `NimBLETaskData.cpp`: original constructor/destructor, `taskWait`, and
  `taskRelease` from `src/NimBLEUtils.cpp`.
- `ble_gattc_write.c`: original `ble_gattc_write`, `ble_gattc_write_flat`, and
  `ble_gattc_rx_write_rsp` from `src/nimble/nimble/host/src/ble_gattc.c`.

`test_patch_nimble.py` applies the production patch to these files and compiles
its actual write implementation with the original GATT functions and FreeRTOS
completion functions. The harness supplies deterministic scheduling, transport,
and procedure storage. A response preempting a worker between TX and procedure
registration is dropped by the upstream response handler; host-queued initiation
makes that response dispatch occur after registration. This demonstrates the
source race and the ordering fix, not that a particular hardware timeout was
caused by it. It does not simulate radio delivery, ESP32 task scheduling, or all
NimBLE procedures.

The production opt-in API is intentionally restricted to short acknowledged
read-only trainer polls. Control commands and long/no-response writes keep their
existing initiation path. Waiting and security retries remain on the worker.

Fixture SHA-256 checksums:

```
bc2c8985b194a1051b4b367f8a8025673ce24b8ebe170c40ea4f17e7ac72e4b2  NimBLERemoteValueAttribute.cpp
f21c54f1bf264a9c08b8de36dcc47623e118f3d604e012449f4bdcf85c05a5e7  NimBLERemoteValueAttribute.h
8c9b90de42ba1f6dd4121f0a480f8b3fa1de260bff7c78f71cf86da27d032e38  NimBLETaskData.cpp
fa4357e7cbe3b47eb96bb07aa119577712aa6901d96582af1f1894698af3eae9  ble_gattc_write.c
```
