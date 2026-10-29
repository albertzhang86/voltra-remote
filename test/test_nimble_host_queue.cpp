// Deterministic host scheduler around the exact patched NimBLE 2.5.1 write
// implementation and upstream GATT write/response functions. No BLE hardware.
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

#define INC_FREERTOS_H 1
#define MYNEWT_VAL(x) 1
#define NIMBLE_BLE_ATT_CLT_WRITE 1
#define STATS_INC(...) ((void)0)
#define NIMBLE_LOGD(...) ((void)0)
#define NIMBLE_LOGI(...) ((void)0)
#define NIMBLE_LOGE(...) ((void)0)
#define BLE_HS_ATT_ERR(x) (0x100 + (x))
[[maybe_unused]] constexpr int BLE_HS_ENOTSUP=1;
constexpr int BLE_HS_ENOMEM=2, BLE_HS_ENOTCONN=3, BLE_HS_EDONE=4;
constexpr int BLE_ATT_ERR_ATTR_NOT_LONG=11, BLE_ATT_ERR_INSUFFICIENT_AUTHEN=12;
constexpr int BLE_ATT_ERR_INSUFFICIENT_AUTHOR=13, BLE_ATT_ERR_INSUFFICIENT_ENC=14;
constexpr int BLE_GATT_OP_WRITE=1, pdTRUE=1, eSetBits=0;
constexpr uint32_t BLE_NPL_TIME_FOREVER=UINT32_MAX;
constexpr uint32_t TASK_BLOCK_BIT=1u<<31;
using ble_npl_time_t=uint32_t;
using TaskHandle_t=void*;
struct ble_gatt_error { int status; };
struct ble_gatt_attr {};
using ble_gatt_attr_fn=int(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*);
struct os_mbuf { unsigned length; };
#define OS_MBUF_PKTLEN(x) ((x)->length)
struct ble_gattc_proc {
    uint16_t conn_handle=0,cid=4;
    struct { uint16_t att_handle=0; ble_gatt_attr_fn* cb=nullptr; void* cb_arg=nullptr; } write;
};
struct ble_npl_event { void (*fn)(ble_npl_event*)=nullptr; void* arg=nullptr; };
struct ble_npl_eventq {};
static bool host=false, eagerQueue=false, fastResponse=true, connected=true;
static uint16_t connection=7, mtu=23;
static uint32_t notifications=0;
static unsigned queuedCount=0, txCount=0, dropped=0, waits=0, shortCalls=0, longCalls=0, noResponseCalls=0, securityCalls=0;
static int immediateError=0, completionError=0, longError=0;
static bool secureSucceeds=true;
static std::deque<std::function<void()>> events;
static ble_gattc_proc* pending=nullptr;
static std::function<void()> beforeEvent;
static std::vector<int> ordering;
static void runEvent() {
    auto event=events.front();events.pop_front();
    bool previous=host;host=true;event();host=previous;
}
static void drain() { while(!events.empty())runEvent(); }
extern "C" int ble_hs_is_parent_task(void) { return host; }
static ble_npl_eventq* nimble_port_get_dflt_eventq() { static ble_npl_eventq q;return &q; }
static void ble_npl_event_init(ble_npl_event* e,void(*fn)(ble_npl_event*),void* arg) { e->fn=fn;e->arg=arg; }
static void* ble_npl_event_get_arg(ble_npl_event* e) { return e->arg; }
static void ble_npl_eventq_put(ble_npl_eventq*,ble_npl_event* e) {
    ++queuedCount;
    events.push_back([e]{ if(beforeEvent){auto action=std::move(beforeEvent);beforeEvent={};action();}e->fn(e); });
    if(eagerQueue)drain(); // Host may preempt before the worker starts waiting.
}
static void* xTaskGetCurrentTaskHandle() { assert(!host);return &notifications; }
static int xTaskNotify(TaskHandle_t handle,uint32_t bit,int) { assert(handle==&notifications);notifications|=bit;return pdTRUE; }
static int xTaskNotifyWait(uint32_t,uint32_t clear,uint32_t* value,uint32_t ticks) {
    assert(!host); // Synchronous waits must never execute on the host.
    if(ticks && !notifications) { ++waits;drain(); }
    if(ticks && !notifications)throw std::runtime_error("GATT response dropped before procedure registration");
    if(value)*value=notifications;
    bool signaled=notifications!=0;notifications&=~clear;return signaled?pdTRUE:0;
}
static void ble_npl_time_ms_to_ticks(uint32_t ms,uint32_t* ticks){*ticks=ms;}
struct NimBLETaskData {
    NimBLETaskData(void* instance=nullptr,int flags=0,void* buf=nullptr);
    ~NimBLETaskData();
    void* m_pInstance=nullptr;mutable int m_flags=0;void* m_pBuf=nullptr;
    mutable void* m_pHandle=nullptr;
};
struct NimBLEUtils {
    static bool taskWait(const NimBLETaskData&,uint32_t);
    static void taskRelease(const NimBLETaskData&,int=0);
    static const char* returnCodeToString(int){return "fake";}
};
struct NimBLEClient {
    uint16_t getMTU()const{return mtu;}
    uint16_t getConnHandle()const{return connection;}
    bool isConnected()const{return connected;}
    bool secureConnection()const{assert(!host);++securityCalls;return secureSucceeds;}
};
struct NimBLERemoteValueAttribute {
    NimBLEClient client;
    const NimBLEClient* getClient()const{return &client;}
    uint16_t getHandle()const{return 19;}
    bool writeValue(const uint8_t*,size_t,bool=false)const;
    bool writeValueHostQueued(const uint8_t*,size_t)const;
    bool writeValueImpl(const uint8_t*,size_t,bool,bool)const;
    static int onWriteCB(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*);
};
static os_mbuf* ble_hs_mbuf_from_flat(const void*,unsigned length){return new os_mbuf{length};}
static void os_mbuf_free_chain(os_mbuf* p){delete p;}
static ble_gattc_proc* ble_gattc_proc_alloc(){return new ble_gattc_proc;}
static void ble_gattc_proc_prepare(ble_gattc_proc* p,uint16_t handle,int){p->conn_handle=handle;}
static void ble_gattc_log_write(uint16_t,unsigned,int){}
static void ble_gattc_process_status(ble_gattc_proc* p,int status){
    if(status==0){assert(!pending);pending=p;ordering.push_back(2);}else delete p;
}
static ble_gattc_proc* ble_gattc_extract_first_by_conn_cid_op(uint16_t handle,uint16_t cid,int){
    if(!pending||pending->conn_handle!=handle||pending->cid!=cid){++dropped;return nullptr;}
    auto* p=pending;pending=nullptr;return p;
}
static int ble_gattc_write_cb(ble_gattc_proc* p,int status,uint16_t){
    ble_gatt_error error{status};return p->write.cb(p->conn_handle,&error,nullptr,p->write.cb_arg);
}
void ble_gattc_rx_write_rsp(uint16_t,uint16_t);
static int ble_att_clt_tx_write_req(uint16_t handle,uint16_t cid,uint16_t,os_mbuf* data){
    ++txCount;ordering.push_back(1);delete data;
    if(immediateError)return immediateError;
    auto response=[handle,cid]{
        ordering.push_back(3);
        if(completionError){int status=completionError;completionError=0;
            auto* p=ble_gattc_extract_first_by_conn_cid_op(handle,cid,BLE_GATT_OP_WRITE);
            if(p){ble_gattc_write_cb(p,status,0);ble_gattc_process_status(p,BLE_HS_EDONE);}
        }else ble_gattc_rx_write_rsp(handle,cid);
    };
    if(fastResponse&&!host){bool old=host;host=true;response();host=old;}else events.push_back(response);
    return 0;
}
int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,ble_gatt_attr_fn*,void*);
static int ble_gattc_write_no_rsp_flat(uint16_t,uint16_t,const void*,unsigned){++noResponseCalls;return immediateError;}
static int ble_gattc_write_long(uint16_t handle,uint16_t,uint16_t,os_mbuf* data,ble_gatt_attr_fn* cb,void* arg){
    ++longCalls;assert(!host);delete data;
    if(immediateError)return immediateError;
    events.push_back([=]{ble_gatt_error error{longError};longError=0;cb(handle,&error,nullptr,arg);});return 0;
}

#include "extracted_task_data.cpp"
#include "extracted_gatt_write.cpp"
#include "patched_remote_write.cpp"

static void reset(){
    assert(events.empty());delete pending;pending=nullptr;
    host=false;eagerQueue=false;fastResponse=true;connected=true;connection=7;mtu=23;
    notifications=0;queuedCount=txCount=dropped=waits=shortCalls=longCalls=noResponseCalls=securityCalls=0;
    immediateError=completionError=longError=0;secureSucceeds=true;beforeEvent={};ordering.clear();
}
int main(){
    NimBLERemoteValueAttribute attr;uint8_t data[100]{};
    // Exact upstream write code permits a host response before registration.
    bool timedOut=false;try{attr.writeValue(data,13,true);}catch(const std::runtime_error&){timedOut=true;}
    assert(timedOut&&dropped==1&&queuedCount==0&&(ordering==std::vector<int>{1,3,2}));reset();
    // Exact same C TX/register path, now wholly within one host callback.
    assert(attr.writeValueHostQueued(data,13));
    assert(dropped==0&&queuedCount==1&&(ordering==std::vector<int>{1,2,3}));reset();
    // Completion can happen before the worker calls its original taskWait.
    eagerQueue=true;assert(attr.writeValueHostQueued(data,13));assert(waits==0);reset();
    immediateError=BLE_HS_ENOMEM;assert(!attr.writeValueHostQueued(data,13));
    assert(txCount==1&&events.empty()&&!pending);reset();
    beforeEvent=[]{connected=false;};assert(!attr.writeValueHostQueued(data,13));assert(txCount==0);reset();
    beforeEvent=[]{connection=8;};assert(!attr.writeValueHostQueued(data,13));assert(txCount==0);reset();
    completionError=99;assert(!attr.writeValueHostQueued(data,13));assert(txCount==1);reset();
    completionError=BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    assert(attr.writeValueHostQueued(data,13));assert(txCount==2&&queuedCount==2&&securityCalls==1);reset();
    completionError=BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC);secureSucceeds=false;
    assert(!attr.writeValueHostQueued(data,13));assert(txCount==1&&securityCalls==1);reset();
    host=true;assert(!attr.writeValueHostQueued(data,13));host=false;
    assert(!attr.writeValueHostQueued(nullptr,13));assert(!attr.writeValueHostQueued(data,0));
    assert(!attr.writeValueHostQueued(data,21));connected=false;assert(!attr.writeValueHostQueued(data,13));
    connected=true;mtu=2;assert(!attr.writeValueHostQueued(data,1));assert(txCount==0&&queuedCount==0);reset();
    // Ordinary controls remain immediate; no-response and long writes unchanged.
    fastResponse=false;assert(attr.writeValue(data,13,true));assert(queuedCount==0&&txCount==1);reset();
    assert(attr.writeValue(data,13,false));assert(queuedCount==0&&noResponseCalls==1);reset();
    assert(attr.writeValue(data,30,true));assert(queuedCount==0&&longCalls==1);reset();
    // Upstream long-write fallback still truncates and retries on the worker.
    fastResponse=false;longError=BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG);
    assert(attr.writeValue(data,30,true));assert(queuedCount==0&&longCalls==1&&txCount==1);reset();
    std::cout<<"NimBLE patched-code race/completion/lifetime/retry regressions passed\n";
}
