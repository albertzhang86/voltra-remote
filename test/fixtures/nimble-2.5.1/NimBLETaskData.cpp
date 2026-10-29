/*
 * Copyright 2020-2026 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

NimBLETaskData::NimBLETaskData(void* pInstance, int flags, void* buf)
    : m_pInstance{pInstance},
      m_flags{flags},
      m_pBuf{buf}
# if defined INC_FREERTOS_H
      ,
      m_pHandle{xTaskGetCurrentTaskHandle()} {
}
# else
{
    ble_npl_sem* sem = new ble_npl_sem;
    if (ble_npl_sem_init(sem, 0) != BLE_NPL_OK) {
        NIMBLE_LOGE(LOG_TAG, "Failed to init semaphore");
        delete sem;
        m_pHandle = nullptr;
    } else {
        m_pHandle = sem;
    }
}
# endif

/**
 * @brief Destructor.
 */
NimBLETaskData::~NimBLETaskData() {
# if !defined INC_FREERTOS_H
    if (m_pHandle != nullptr) {
        ble_npl_sem_deinit(static_cast<ble_npl_sem*>(m_pHandle));
        delete static_cast<ble_npl_sem*>(m_pHandle);
    }
# endif
}

/**
 * @brief Blocks the calling task until released or timeout.
 * @param [in] taskData A pointer to the task data structure.
 * @param [in] timeout The time to wait in milliseconds.
 * @return True if the task completed, false if the timeout was reached.
 */
bool NimBLEUtils::taskWait(const NimBLETaskData& taskData, uint32_t timeout) {
    ble_npl_time_t ticks;
    if (timeout == BLE_NPL_TIME_FOREVER) {
        ticks = BLE_NPL_TIME_FOREVER;
    } else {
        ble_npl_time_ms_to_ticks(timeout, &ticks);
    }

# if defined INC_FREERTOS_H
    uint32_t notificationValue;
    xTaskNotifyWait(0, TASK_BLOCK_BIT, &notificationValue, 0);
    if (notificationValue & TASK_BLOCK_BIT) {
        return true;
    }

    return xTaskNotifyWait(0, TASK_BLOCK_BIT, nullptr, ticks) == pdTRUE;

# else
    return ble_npl_sem_pend(static_cast<ble_npl_sem*>(taskData.m_pHandle), ticks) == BLE_NPL_OK;
# endif
} // taskWait

/**
 * @brief Release a task.
 * @param [in] taskData A pointer to the task data structure.
 * @param [in] flags A return value to set in the task data structure.
 */
void NimBLEUtils::taskRelease(const NimBLETaskData& taskData, int flags) {
    taskData.m_flags = flags;
    if (taskData.m_pHandle != nullptr) {
# if defined INC_FREERTOS_H
        xTaskNotify(static_cast<TaskHandle_t>(taskData.m_pHandle), TASK_BLOCK_BIT, eSetBits);
# else
        ble_npl_sem_release(static_cast<ble_npl_sem*>(taskData.m_pHandle));
# endif
    }
} // taskRelease

