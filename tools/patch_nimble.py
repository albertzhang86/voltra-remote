#!/usr/bin/env python3
"""Apply reviewed local fixes to pinned NimBLE-Arduino 2.5.1.

Upstream deletes a saved bond before retrying any incoming pairing request.
The TV bridge needs to preserve that bond unless the user opened pairing.
Apply before every build so dependency reinstalls cannot silently undo the guard.

The opt-in writeValueHostQueued API also starts short acknowledged writes on the
host event queue. Upstream transmits before registering its GATT procedure; a
fast response on the host task can otherwise arrive in that gap. Only read-only
trainer polls opt in. Ordinary writes, motor controls and long writes retain
their original scheduling. This source race is reproducible in the host test;
it is not evidence that every observed hardware timeout has the same cause.
"""

from pathlib import Path
import sys


DEFAULT_LIBRARY = Path(__file__).resolve().parents[1] / ".arduino/user/libraries/NimBLE-Arduino"

PATCHES = {
    "src/NimBLERemoteValueAttribute.h": [
        (
            "    bool writeValue(const uint8_t* data, size_t length, bool response = false) const;\n",
            """    bool writeValue(const uint8_t* data, size_t length, bool response = false) const;

    /**
     * @brief Complete a short acknowledged write initiated on the host queue.
     * @note Worker tasks only. The caller must keep this object and buffer alive
     * until return. Rejects empty, oversized and disconnected requests. Intended
     * for read-only polling; do not defer a motor/control transaction here.
     */
    bool writeValueHostQueued(const uint8_t* data, size_t length) const;
""",
        ),
        (
            "    static int onReadCB(uint16_t conn_handle, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg);\n",
            """    bool writeValueImpl(const uint8_t* data, size_t length, bool response, bool hostQueued) const;

    static int onReadCB(uint16_t conn_handle, const ble_gatt_error* error, ble_gatt_attr* attr, void* arg);
""",
        ),
    ],
    "src/NimBLERemoteValueAttribute.cpp": [
        (
            '# include "NimBLELog.h"\n',
            """# include "NimBLELog.h"

# ifdef USING_NIMBLE_ARDUINO_HEADERS
#  include "nimble/porting/nimble/include/nimble/nimble_port.h"
# else
#  include "nimble/nimble_port.h"
# endif

// Internal host-task check is pinned with this reviewed 2.5.1 patch.
extern "C" int ble_hs_is_parent_task(void);
""",
        ),
        (
            "bool NimBLERemoteValueAttribute::writeValue(const uint8_t* data, size_t length, bool response) const {\n",
            """namespace {
struct HostQueuedWrite {
    ble_npl_event event{};
    const NimBLEClient* client;
    uint16_t connection;
    uint16_t attribute;
    const uint8_t* data;
    uint16_t length;
    ble_gatt_attr_fn* callback;
    NimBLETaskData* task;

    static void run(ble_npl_event* event) {
        auto* request = static_cast<HostQueuedWrite*>(ble_npl_event_get_arg(event));
        // Never retarget a queued poll to a replacement connection.
        const int rc = !request->client->isConnected() ||
                               request->client->getConnHandle() != request->connection
                           ? BLE_HS_ENOTCONN
                           : ble_gattc_write_flat(request->connection, request->attribute,
                                                  request->data, request->length,
                                                  request->callback, request->task);
        if (rc != 0) {
            // No GATT callback follows an initiation failure. This is the last
            // access to the stack request: release may immediately wake its owner.
            NimBLEUtils::taskRelease(*request->task, rc);
        }
    }
};
} // namespace

bool NimBLERemoteValueAttribute::writeValue(const uint8_t* data, size_t length, bool response) const {
    return writeValueImpl(data, length, response, false);
}

bool NimBLERemoteValueAttribute::writeValueHostQueued(const uint8_t* data, size_t length) const {
    const auto* client = getClient();
    if (ble_hs_is_parent_task() || !client->isConnected() || data == nullptr ||
        length == 0 || client->getMTU() < 3 || length > static_cast<size_t>(client->getMTU() - 3)) {
        return false;
    }
    return writeValueImpl(data, length, true, true);
}

bool NimBLERemoteValueAttribute::writeValueImpl(const uint8_t* data, size_t length, bool response, bool hostQueued) const {
""",
        ),
        (
            "    NimBLETaskData      taskData(const_cast<NimBLERemoteValueAttribute*>(this));\n",
            """    // Recheck after entering the shared implementation: this opt-in API
    // must never fall through to a long write after an MTU change.
    if (hostQueued && (pClient->getMTU() < 3 || length > mtu)) return false;
    NimBLETaskData      taskData(const_cast<NimBLERemoteValueAttribute*>(this));
    // The worker owns the event, input buffer and completion task until the
    // existing unbounded wait completes. Only raw initiation runs on the host;
    // waiting, authentication and retry decisions remain on the worker.
    HostQueuedWrite queued{{}, pClient, pClient->getConnHandle(), getHandle(), data,
                           static_cast<uint16_t>(length), onWriteCB, &taskData};
    if (hostQueued) ble_npl_event_init(&queued.event, HostQueuedWrite::run, &queued);
""",
        ),
        (
            """        } else {
            rc = ble_gattc_write_flat(pClient->getConnHandle(),""",
            """        } else if (hostQueued) {
            // Processing the TX and procedure registration in one host event
            // prevents an incoming WRITE_RSP from overtaking registration.
            ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &queued.event);
            rc = 0;
        } else {
            rc = ble_gattc_write_flat(pClient->getConnHandle(),""",
        ),
    ],
    "src/NimBLEServer.h": [
        (
            "    virtual void onAuthenticationComplete(NimBLEConnInfo& connInfo);\n",
            """    virtual void onAuthenticationComplete(NimBLEConnInfo& connInfo);

    /**
     * @brief Decide whether an existing bond may be replaced by new pairing.
     * @return True to delete the old bond and retry; false to preserve and reject.
     * The default retains upstream behavior. Applications can require user consent.
     */
    virtual bool onRepeatPairing(NimBLEConnInfo& connInfo);
""",
        ),
    ],
    "src/NimBLEServer.cpp": [
        (
            """            ble_store_util_delete_peer(&peerInfo.m_desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY""",
            """            // Voltra Remote: ask before removing a previously trusted bond.
            if (!pServer->m_pServerCallbacks->onRepeatPairing(peerInfo)) {
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }

            ble_store_util_delete_peer(&peerInfo.m_desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY""",
        ),
        (
            """} // onAuthenticationComplete

void NimBLEServerCallbacks::onConnParamsUpdate""",
            """} // onAuthenticationComplete

bool NimBLEServerCallbacks::onRepeatPairing(NimBLEConnInfo& connInfo) {
    return true;
} // onRepeatPairing

void NimBLEServerCallbacks::onConnParamsUpdate""",
        ),
    ],
}


def apply_patch(library: Path) -> None:
    properties = (library / "library.properties").read_text()
    if "version=2.5.1" not in properties.splitlines():
        raise ValueError("Expected NimBLE-Arduino 2.5.1; review the local patches before upgrading")

    updates = {}
    for relative_path, replacements in PATCHES.items():
        path = library / relative_path
        original = path.read_text()
        updated = original
        for before, after in replacements:
            if updated.count(after) == 1:
                continue
            if updated.count(before) != 1:
                raise ValueError(f"Unexpected NimBLE source in {relative_path}; refusing a partial patch")
            updated = updated.replace(before, after, 1)
        if updated != original:
            updates[path] = updated

    # Validate every file before writing any of them.
    for path, updated in updates.items():
        path.write_text(updated)
    if updates:
        print("Applied NimBLE 2.5.1 pairing policy and opt-in host-queued polling patches")


if __name__ == "__main__":
    try:
        apply_patch(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_LIBRARY)
    except (OSError, ValueError) as error:
        sys.exit(f"NimBLE patch failed: {error}")
