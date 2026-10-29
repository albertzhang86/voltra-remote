/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

int
ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle,
                struct os_mbuf *txom, ble_gatt_attr_fn *cb, void *cb_arg)
{
#if !MYNEWT_VAL(BLE_GATT_WRITE)
    return BLE_HS_ENOTSUP;
#endif

    struct ble_gattc_proc *proc;
    int rc;

    STATS_INC(ble_gattc_stats, write);

    proc = ble_gattc_proc_alloc();
    if (proc == NULL) {
        rc = BLE_HS_ENOMEM;
        goto done;
    }

    ble_gattc_proc_prepare(proc, conn_handle, BLE_GATT_OP_WRITE);

    proc->write.att_handle = attr_handle;
    proc->write.cb = cb;
    proc->write.cb_arg = cb_arg;

    ble_gattc_log_write(attr_handle, OS_MBUF_PKTLEN(txom), 1);

    rc = ble_att_clt_tx_write_req(conn_handle, proc->cid, attr_handle, txom);
    txom = NULL;
    if (rc != 0) {
        goto done;
    }

done:
    if (rc != 0) {
        STATS_INC(ble_gattc_stats, write_fail);
    }

    /* Free the mbuf in case the send failed. */
    os_mbuf_free_chain(txom);

    ble_gattc_process_status(proc, rc);
    return rc;
}

int
ble_gattc_write_flat(uint16_t conn_handle, uint16_t attr_handle,
                     const void *data, uint16_t data_len,
                     ble_gatt_attr_fn *cb, void *cb_arg)
{
    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_from_flat(data, data_len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_gattc_write(conn_handle, attr_handle, om, cb, cb_arg);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_gattc_rx_write_rsp(uint16_t conn_handle, uint16_t cid)
{
#if !NIMBLE_BLE_ATT_CLT_WRITE
    return;
#endif

    struct ble_gattc_proc *proc;

    proc = ble_gattc_extract_first_by_conn_cid_op(conn_handle, cid,
                                              BLE_GATT_OP_WRITE);
    if (proc != NULL) {
        ble_gattc_write_cb(proc, 0, 0);
        ble_gattc_process_status(proc, BLE_HS_EDONE);
    }
}

