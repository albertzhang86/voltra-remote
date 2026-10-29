#!/usr/bin/env python3
"""Bounded ATT diagnostic hooks for pinned NimBLE 2.5.1; no transport changes.

Hooks are weak and inactive unless firmware supplies voltra_att_trace_event.
They record queueing, transmit results, response dispatch and timeout without
printing from the BLE host. These are host observations, not an over-air trace.
"""
from pathlib import Path
import sys

HOST = "src/nimble/nimble/host/src/"
DECLARATION = """
/* Voltra Remote: optional, nonblocking diagnostic observer. */
extern void voltra_att_trace_event(uint8_t, uint16_t, uint16_t, uint16_t, int32_t)
    __attribute__((weak));
#define VOLTRA_ATT_TRACE(kind, connection, cid, detail, result) do { \
    if (voltra_att_trace_event) \
        voltra_att_trace_event(kind, connection, cid, detail, result); \
} while (0)
"""

PATCHES = {
    "ble_att_cmd.c": [
        ("    int rc;\n    struct os_mbuf_pkthdr *omp;",
         "    int rc;\n    uint8_t trace_op;\n    struct os_mbuf_pkthdr *omp;"),
        ("            STAILQ_INSERT_TAIL(&conn->att_tx_q, OS_MBUF_PKTHDR(txom), omp_next);",
         "            VOLTRA_ATT_TRACE(1, conn->bhc_handle, chan->scid, txom->om_data[0], 0);\n            STAILQ_INSERT_TAIL(&conn->att_tx_q, OS_MBUF_PKTHDR(txom), omp_next);"),
        ("    ble_att_inc_tx_stat(txom->om_data[0]);",
         "    trace_op = txom->om_data[0];\n    VOLTRA_ATT_TRACE(2, conn->bhc_handle, chan->scid, trace_op, conn->client_att_busy);\n    ble_att_inc_tx_stat(txom->om_data[0]);"),
        ("    rc = ble_l2cap_tx(conn, chan, txom);\n    assert(rc == 0);",
         "    rc = ble_l2cap_tx(conn, chan, txom);\n    VOLTRA_ATT_TRACE(3, conn->bhc_handle, chan->scid, trace_op, rc);\n    assert(rc == 0);"),
    ],
    "ble_att.c": [
        ("    if (cid == BLE_L2CAP_CID_ATT && ble_att_is_response_op(op)) {",
         """    /* Capture before response dispatch, including unmatched responses. */
    uint8_t trace_payload[5] = {0};
    uint16_t trace_length = OS_MBUF_PKTLEN(*om);
    if (trace_length > sizeof(trace_payload)) trace_length = sizeof(trace_payload);
    os_mbuf_copydata(*om, 0, trace_length, trace_payload);
    uint32_t trace_detail = (uint32_t)trace_payload[1] |
        (uint32_t)trace_payload[2] << 8 | (uint32_t)trace_payload[3] << 16 |
        (uint32_t)trace_payload[4] << 24;
    VOLTRA_ATT_TRACE(5, conn_handle, cid, op, (int32_t)trace_detail);

    if (cid == BLE_L2CAP_CID_ATT && ble_att_is_response_op(op)) {"""),
    ],
    "ble_gattc.c": [
        ("    cb = ble_gattc_tmo_dispatch_get(proc->op);",
         "    VOLTRA_ATT_TRACE(8, proc->conn_handle, proc->cid, proc->op, 0);\n    cb = ble_gattc_tmo_dispatch_get(proc->op);"),
        ("    rc = ble_att_clt_tx_write_req(conn_handle, proc->cid, attr_handle, txom);",
         "    VOLTRA_ATT_TRACE(4, conn_handle, proc->cid, attr_handle, OS_MBUF_PKTLEN(txom));\n    rc = ble_att_clt_tx_write_req(conn_handle, proc->cid, attr_handle, txom);"),
        ("    /* Free the mbuf in case the send failed. */\n    os_mbuf_free_chain(txom);",
         "    VOLTRA_ATT_TRACE(7, conn_handle, proc ? proc->cid : 0, attr_handle, rc);\n    /* Free the mbuf in case the send failed. */\n    os_mbuf_free_chain(txom);"),
        ("    proc = ble_gattc_extract_first_by_conn_cid_op(conn_handle, cid,\n                                              BLE_GATT_OP_WRITE);\n    if (proc != NULL) {",
         "    proc = ble_gattc_extract_first_by_conn_cid_op(conn_handle, cid,\n                                              BLE_GATT_OP_WRITE);\n    VOLTRA_ATT_TRACE(6, conn_handle, cid, proc != NULL, 0);\n    if (proc != NULL) {"),
    ],
    "ble_hs_hci.c": [
        ("        rc = ble_hs_tx_data(frag);\n        if (rc != 0) {",
         """        /* Copy only headers before ble_hs_tx_data consumes the mbuf. */
        uint8_t trace_header[9] = {0};
        uint16_t trace_cid = 0, trace_op = 0;
        if (pb == BLE_HCI_PB_FIRST_NON_FLUSH && OS_MBUF_PKTLEN(frag) >= sizeof(trace_header) &&
            os_mbuf_copydata(frag, 0, sizeof(trace_header), trace_header) == 0) {
            trace_cid = (uint16_t)trace_header[6] | (uint16_t)trace_header[7] << 8;
            trace_op = trace_header[8];
        }
        VOLTRA_ATT_TRACE(9, conn->bhc_handle, trace_cid, trace_op, ble_hs_hci_avail_pkts);
        rc = ble_hs_tx_data(frag);
        VOLTRA_ATT_TRACE(10, conn->bhc_handle, trace_cid, trace_op, rc);
        if (rc != 0) {"""),
    ],
}

def apply_patch(library: Path):
    if "version=2.5.1" not in (library / "library.properties").read_text().splitlines():
        raise ValueError("Expected NimBLE-Arduino 2.5.1 for ATT diagnostic hooks")
    updates = {}
    for name, replacements in PATCHES.items():
        path = library / HOST / name
        original = path.read_text()
        updated = original
        if DECLARATION not in updated:
            anchor = '#include "ble_hs_priv.h"\n'
            if updated.count(anchor) != 1:
                raise ValueError(f"Unexpected includes in {name}")
            updated = updated.replace(anchor, anchor + DECLARATION, 1)
        for before, after in replacements:
            if updated.count(after) == 1:
                continue
            if updated.count(before) != 1:
                raise ValueError(f"Unexpected NimBLE source in {name}; refusing a partial trace patch")
            updated = updated.replace(before, after, 1)
        if updated != original:
            updates[path] = updated
    for path, updated in updates.items():
        path.write_text(updated)
    if updates:
        print("Applied bounded NimBLE ATT diagnostic hooks (no transport behavior changes)")

if __name__ == "__main__":
    try:
        apply_patch(Path(sys.argv[1]) if len(sys.argv) > 1 else
                    Path(__file__).resolve().parents[1] / ".arduino/user/libraries/NimBLE-Arduino")
    except (OSError, ValueError) as error:
        sys.exit(f"ATT trace patch failed: {error}")
