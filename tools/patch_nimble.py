#!/usr/bin/env python3
"""Add an explicit repeat-pairing policy callback to pinned NimBLE-Arduino 2.5.1.

Upstream deletes a saved bond before retrying any incoming pairing request.
The TV bridge needs to preserve that bond unless the user opened pairing.
Apply before every build so dependency reinstalls cannot silently undo the guard.
"""

from pathlib import Path
import sys


DEFAULT_LIBRARY = Path(__file__).resolve().parents[1] / ".arduino/user/libraries/NimBLE-Arduino"

PATCHES = {
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
        raise ValueError("Expected NimBLE-Arduino 2.5.1; review the repeat-pairing patch before upgrading")

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

    # Validate both files before writing either one.
    for path, updated in updates.items():
        path.write_text(updated)
    if updates:
        print("Applied NimBLE 2.5.1 repeat-pairing policy callback")


if __name__ == "__main__":
    try:
        apply_patch(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_LIBRARY)
    except (OSError, ValueError) as error:
        sys.exit(f"NimBLE patch failed: {error}")
