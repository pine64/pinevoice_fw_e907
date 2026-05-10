// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "improv.h"

#include <aos/cli.h>
#include <ulog/ulog.h>
#include <aos/ble.h>
#include <bl_efuse.h>
#include <bl606p_ef_ctrl.h>
#include <wifi/multi_ssid/wifi_config.h>
#include <wifi/app_wifi.h>
#include <app_main.h>
#include <uservice/uservice.h>
#include <event_mgr/app_event.h>
#include <yoc/netmgr.h>
#include "../display/pwm_led/pwm_led.h"

#define TAG "IMPROV"

struct improv_state {
  uint8_t current_state;
  uint8_t error_state;
  bool provisioning_done;
  uint32_t timeout;
  // BLE stuff
  bool stack_init;
  bool connected;
  uint16_t conn_handle;
  uint16_t svc_handle;
  uint8_t rpc_buffer[256];
} static prov_state;

gatt_service improv_service;

enum {
  IMPROV_IDX_SVC,
  
  IMPROV_IDX_STA,
  IMPROV_IDX_STA_VAL,
  IMPROV_IDX_STA_CCC,

  IMPROV_IDX_ERR,
  IMPROV_IDX_ERR_VAL,
  IMPROV_IDX_ERR_CCC,

  IMPROV_IDX_RPC_CMD,
  IMPROV_IDX_RPC_CMD_VAL,
  IMPROV_IDX_RPC_CMD_CCC,

  IMPROV_IDX_RPC_RES,
  IMPROV_IDX_RPC_RES_VAL,
  IMPROV_IDX_RPC_RES_CCC,

  IMPROV_IDX_CAP,
  IMPROV_IDX_CAP_VAL,
  IMPROV_IDX_CAP_CCC,

  IMPROV_IDX_MAX,
};

// 00467768-6228-2272-4663-277478268000
#define UUID_IMPROV UUID128_DECLARE(0x00,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)
// 00467768-6228-2272-4663-277478268001
#define UUID_IMPROV_STA UUID128_DECLARE(0x01,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)
// 00467768-6228-2272-4663-277478268002
#define UUID_IMPROV_ERR UUID128_DECLARE(0x02,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)
// 00467768-6228-2272-4663-277478268003
#define UUID_IMPROV_RPC_CMD UUID128_DECLARE(0x03,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)
// 00467768-6228-2272-4663-277478268004
#define UUID_IMPROV_RPC_RES UUID128_DECLARE(0x04,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)
// 00467768-6228-2272-4663-277478268005
#define UUID_IMPROV_CAP UUID128_DECLARE(0x05,0x80,0x26,0x78,0x74,0x27,0x63,0x46,0x72,0x22,0x28,0x62,0x68,0x77,0x46,0x00)


static gatt_attr_t improv_attrs[IMPROV_IDX_MAX] = {
  [IMPROV_IDX_SVC] = GATT_PRIMARY_SERVICE_DEFINE(UUID_IMPROV),
  [IMPROV_IDX_STA] = GATT_CHAR_DEFINE(UUID_IMPROV_STA, GATT_CHRC_PROP_READ | GATT_CHRC_PROP_NOTIFY),
  [IMPROV_IDX_STA_VAL] = GATT_CHAR_VAL_DEFINE(UUID_IMPROV_STA, GATT_PERM_READ),
  [IMPROV_IDX_STA_CCC] = GATT_CHAR_CCC_DEFINE(),
  [IMPROV_IDX_ERR] = GATT_CHAR_DEFINE(UUID_IMPROV_ERR, GATT_CHRC_PROP_READ | GATT_CHRC_PROP_NOTIFY),
  [IMPROV_IDX_ERR_VAL] = GATT_CHAR_VAL_DEFINE(UUID_IMPROV_ERR, GATT_PERM_READ),
  [IMPROV_IDX_ERR_CCC] = GATT_CHAR_CCC_DEFINE(),
  [IMPROV_IDX_RPC_CMD] = GATT_CHAR_DEFINE(UUID_IMPROV_RPC_CMD, GATT_CHRC_PROP_WRITE),
  [IMPROV_IDX_RPC_CMD_VAL] = GATT_CHAR_VAL_DEFINE(UUID_IMPROV_RPC_CMD, GATT_PERM_READ | GATT_PERM_WRITE),
  [IMPROV_IDX_RPC_CMD_CCC] = GATT_CHAR_CCC_DEFINE(),
  [IMPROV_IDX_RPC_RES] = GATT_CHAR_DEFINE(UUID_IMPROV_RPC_RES, GATT_CHRC_PROP_READ | GATT_CHRC_PROP_NOTIFY),
  [IMPROV_IDX_RPC_RES_VAL] = GATT_CHAR_VAL_DEFINE(UUID_IMPROV_RPC_RES, GATT_PERM_READ),
  [IMPROV_IDX_RPC_RES_CCC] = GATT_CHAR_CCC_DEFINE(),
  [IMPROV_IDX_CAP] = GATT_CHAR_DEFINE(UUID_IMPROV_CAP, GATT_CHRC_PROP_READ),
  [IMPROV_IDX_CAP_VAL] = GATT_CHAR_VAL_DEFINE(UUID_IMPROV_CAP, GATT_PERM_READ),
  [IMPROV_IDX_CAP_CCC] = GATT_CHAR_CCC_DEFINE(),
};

static int start_adv(void);

/* 连接事件处理 */
void conn_change(ble_event_en event, void *event_data)
{
  /* 连接事件对应的事件数据 */
  evt_data_gap_conn_change_t *e = (evt_data_gap_conn_change_t *)event_data;

  if (e->connected == CONNECTED) {
    LOGI(TAG, "Connected");
    prov_state.connected = true;
    prov_state.conn_handle = e->conn_handle;
  } else {
    prov_state.connected = false;
    LOGI(TAG, "Disconnected");
    if (prov_state.provisioning_done) {
      ble_stack_adv_stop();
    } else {
      start_adv();
    }
    // TODO: Check if we did not finished the provisioning
  }
}

static uint8_t answer_buffer[20];

static void event_char_read(ble_event_en event, void *event_data)
{
  evt_data_gatt_char_read_t *e = (evt_data_gatt_char_read_t *)event_data;

  if (e->char_handle < prov_state.svc_handle || e->char_handle >= prov_state.svc_handle + IMPROV_IDX_MAX) {
    return;
  }

  uint16_t char_idx = e->char_handle - prov_state.svc_handle;

  LOGD(TAG, "event_char_read conn handle %d char handle 0x%04x, len %d, offset %d, idx: %d",
     e->conn_handle, e->char_handle, e->len, e->offset, char_idx);

  e->data = NULL;
  e->len = 0;

  switch (char_idx) {
    case IMPROV_IDX_CAP_VAL: {
      answer_buffer[0] = 0;
      if (1) answer_buffer[0] |= 1 << 0; // Supports identify command
      e->data = answer_buffer;
      e->len = 1;
      break;
    }
    case IMPROV_IDX_STA_VAL: {
      answer_buffer[0] = prov_state.current_state;
      e->data = answer_buffer;
      e->len = 1;
      break;
    }
    case IMPROV_IDX_ERR_VAL: {
      answer_buffer[0] = prov_state.error_state;
      e->data = answer_buffer;
      e->len = 1;
      break;
    }
    default: LOGD(TAG, "unknown read idx: %d", char_idx); break;
  }
}

static void improv_set_error(enum improv_error_state_e state)
{
  uint8_t err = state;
  // TODO: Conn handle check
  // TODO: if didnt changed, do not do anything
  prov_state.error_state = state;
  int ret = ble_stack_gatt_notificate(prov_state.conn_handle, prov_state.svc_handle + IMPROV_IDX_ERR_VAL, &err, 1);
  if (ret != 0) {
    LOGE(TAG, "ble_stack_gatt_notificate returned %d", ret);
  }
}

static void improv_set_state(enum improv_state_e state)
{
  uint8_t sta = state;
  // TODO: Conn handle check
  prov_state.current_state = state;
  int ret = ble_stack_gatt_notificate(prov_state.conn_handle, prov_state.svc_handle + IMPROV_IDX_STA_VAL, &sta, 1);
  if (ret != 0) {
    LOGE(TAG, "ble_stack_gatt_notificate returned %d", ret);
  }
}

uint8_t improv_calculate_crc(uint8_t* data, uint16_t length)
{
  uint32_t crc = 0;
  for (uint16_t i = 0; i < length; i++) {
    crc += data[i];
  }
  return (crc & 0xFF);
}

static void handle_wifi_res_cb(uint32_t event_id, const void *param, void *context)
{
  LOGD(TAG, "Got EVENT!!! %d", event_id);
  switch (event_id) {
    case EVENT_NETMGR_GOT_IP: {
      improv_set_state(IMPROV_STATE_PROVISIONED);
      uint8_t* buf = prov_state.rpc_buffer;
      buf[0] = IMPROV_CMD_WIFI_SETTINGS;
      buf[1] = 0;
      buf[2] = improv_calculate_crc(buf, 2);
      int ret = ble_stack_gatt_notificate(prov_state.conn_handle, prov_state.svc_handle + IMPROV_IDX_RPC_RES_VAL, buf, 3);
      if (ret != 0) {
        LOGE(TAG, "ble_stack_gatt_notificate returned %d", ret);
      }
      app_event_update(EVENT_STATUS_WIFI_PROV_RECVED);
      app_wifi_config_save();
      light_show_state_msg_send(LIGHT_SHOW_TALKING_STOP, NULL);
      local_audio_play("wifi-provisioning-success.opus");

      break;
    }
    case EVENT_NET_CHECK_TIMER: {
      if (prov_state.current_state == IMPROV_STATE_PROVISIONING) {
        improv_set_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
        improv_set_state(IMPROV_STATE_AUTHORIZED);
        app_event_update(EVENT_STATUS_WIFI_PROV_FAILED);
        local_audio_play("wifi-provisioning-failed.opus");
        app_wifi_stop();
        app_wifi_config_del(app_wifi_config_get_cur_ssid());
      }
      break;
    }
  }
  event_unsubscribe(EVENT_NETMGR_GOT_IP, handle_wifi_res_cb, NULL);
  event_unsubscribe(EVENT_NET_CHECK_TIMER, handle_wifi_res_cb, NULL);
}

extern netmgr_hdl_t app_netmgr_hdl;
static void wifi_network_init(char *ssid, char *psk)
{
  netmgr_config_wifi(app_netmgr_hdl, ssid, strlen(ssid), psk, strlen(psk));
  aos_dev_t *dev = netmgr_get_dev(app_netmgr_hdl);
  hal_net_set_hostname(dev, "PineVoice");
  netmgr_start(app_netmgr_hdl);

  return;
}

static enum improv_error_state_e handle_rpc_cmd_wifi_settings(uint8_t* data, uint16_t data_length)
{
  if (prov_state.current_state != IMPROV_STATE_AUTHORIZED) return;
  // TODO: maybe mutex??

  const char* ssid[257];
  const char* password[257];
  uint16_t pos = 0;
  uint8_t ssid_length = data[pos];
  pos += 1;
  memcpy(ssid, data + pos, ssid_length);
  ssid[ssid_length] = '\0';
  pos += ssid_length;
  uint8_t password_length = data[pos];
  pos += 1;
  memcpy(password, data + pos, password_length);
  password[password_length] = '\0';
  pos += password_length;

  LOGD(TAG, "SSID: %s, PASS: *****", ssid);

  improv_set_state(IMPROV_STATE_PROVISIONING);

  event_subscribe(EVENT_NETMGR_GOT_IP, handle_wifi_res_cb, NULL);
  event_subscribe(EVENT_NET_CHECK_TIMER, handle_wifi_res_cb, NULL);

  app_wifi_config_add(ssid, password);
  wifi_network_init(ssid, password);
  // app_wifi_network_init_list();
  event_publish_delay(EVENT_NET_CHECK_TIMER, NULL, 20 * 1000);
  
  return IMPROV_NO_ERROR;
}

static void handle_rpc_command(evt_data_gatt_char_write_t *e)
{
  if (e->len < 2) {
    return improv_set_error(IMPROV_ERROR_INVALID_RPC_PACKET);
  }
  enum improv_command_e command = e->data[0];
  uint8_t data_length = e->data[1];
  if (e->len != data_length + 1 + 1 + 1) { // Command, Length and CRC
    return improv_set_error(IMPROV_ERROR_INVALID_RPC_PACKET);
  }
  void* data = &e->data[2];
  uint8_t crc = e->data[2 + data_length];
  uint8_t our_crc = improv_calculate_crc(e->data, e->len - 1);
  if (crc != our_crc) {
    return improv_set_error(IMPROV_ERROR_INVALID_RPC_PACKET);
  }
  enum improv_error_state_e result = IMPROV_ERROR_UNKNOWN_RPC_COMMAND;
  switch (command) {
    case IMPROV_CMD_WIFI_SETTINGS: result = handle_rpc_cmd_wifi_settings(data, data_length); break;
    case IMPROV_CMD_IDENTIFY: light_show_state_msg_send(LIGHT_SHOW_NET_DISCONNECTED, NULL); break;
  }
  improv_set_error(result);
}

static void event_char_write(ble_event_en event, void *event_data)
{
  evt_data_gatt_char_write_t *e = (evt_data_gatt_char_write_t *)event_data;

  if (e->char_handle < prov_state.svc_handle || e->char_handle >= prov_state.svc_handle + IMPROV_IDX_MAX) {
    return;
  }

  uint16_t char_idx = e->char_handle - prov_state.svc_handle;

  LOGD(TAG, "conn handle %d char handle 0x%04x, len %d, data: %02X, IDX: %d", e->conn_handle, e->char_handle, e->len, e->data[0], char_idx);

  switch (char_idx) {
    case IMPROV_IDX_RPC_CMD_VAL: handle_rpc_command(e); break;
    default: LOGD(TAG, "unknown write idx: %d", char_idx); break;
  }

  // e->len = e->len < RX_MAX_LEN ?  e->len : RX_MAX_LEN;
  // e->len = 0;
  return;
}

// For debug
void* event_names[] = {
    EVENT_STACK_INIT, "EVENT_STACK_INIT",
    EVENT_GAP_CONN_CHANGE, "EVENT_GAP_CONN_CHANGE",
    EVENT_GAP_DEV_FIND, "EVENT_GAP_DEV_FIND",
    EVENT_GAP_CONN_PARAM_REQ, "EVENT_GAP_CONN_PARAM_REQ",
    EVENT_GAP_CONN_PARAM_UPDATE, "EVENT_GAP_CONN_PARAM_UPDATE",
    EVENT_GAP_CONN_SECURITY_CHANGE, "EVENT_GAP_CONN_SECURITY_CHANGE",
    EVENT_GAP_ADV_TIMEOUT, "EVENT_GAP_ADV_TIMEOUT",
    EVENT_GAP_ADV_START, "EVENT_GAP_ADV_START",
    EVENT_GAP_ADV_STOP, "EVENT_GAP_ADV_STOP",
    EVENT_GAP_SCAN_START, "EVENT_GAP_SCAN_START",
    EVENT_GAP_SCAN_STOP, "EVENT_GAP_SCAN_STOP",
    EVENT_GATT_NOTIFY, "EVENT_GATT_NOTIFY",
    EVENT_GATT_INDICATE, "EVENT_GATT_INDICATE",
    EVENT_GATT_CHAR_READ, "EVENT_GATT_CHAR_READ",
    EVENT_GATT_CHAR_WRITE, "EVENT_GATT_CHAR_WRITE",
    EVENT_GATT_INDICATE_CB, "EVENT_GATT_INDICATE_CB",
    EVENT_GATT_CHAR_READ_CB, "EVENT_GATT_CHAR_READ_CB",
    EVENT_GATT_CHAR_WRITE_CB, "EVENT_GATT_CHAR_WRITE_CB",
    EVENT_GATT_CHAR_CCC_CHANGE, "EVENT_GATT_CHAR_CCC_CHANGE",
    EVENT_GATT_CHAR_CCC_WRITE, "EVENT_GATT_CHAR_CCC_WRITE",
    EVENT_GATT_CHAR_CCC_MATCH, "EVENT_GATT_CHAR_CCC_MATCH",
    EVENT_GATT_MTU_EXCHANGE, "EVENT_GATT_MTU_EXCHANGE",
    EVENT_GATT_DISCOVERY_SVC, "EVENT_GATT_DISCOVERY_SVC",
    EVENT_GATT_DISCOVERY_INC_SVC, "EVENT_GATT_DISCOVERY_INC_SVC",
    EVENT_GATT_DISCOVERY_CHAR, "EVENT_GATT_DISCOVERY_CHAR",
    EVENT_GATT_DISCOVERY_CHAR_DES, "EVENT_GATT_DISCOVERY_CHAR_DES",
    EVENT_GATT_DISCOVERY_COMPLETE, "EVENT_GATT_DISCOVERY_COMPLETE",
    EVENT_SMP_PASSKEY_DISPLAY, "EVENT_SMP_PASSKEY_DISPLAY",
    EVENT_SMP_PASSKEY_CONFIRM, "EVENT_SMP_PASSKEY_CONFIRM",
    EVENT_SMP_PASSKEY_ENTER, "EVENT_SMP_PASSKEY_ENTER",
    EVENT_SMP_PAIRING_CONFIRM, "EVENT_SMP_PAIRING_CONFIRM",
    EVENT_SMP_PAIRING_COMPLETE, "EVENT_SMP_PAIRING_COMPLETE",
    EVENT_SMP_CANCEL, "EVENT_SMP_CANCEL",
    EVENT_SMP_IDENTITY_ADDRESS_GET, "EVENT_SMP_IDENTITY_ADDRESS_GET",
};

/* BLE Host事件回调函数 */
static int event_callback(ble_event_en event, void *event_data)
{
  const char* event_name = "UNKNOWN";
  for (int i = 0; i < (sizeof(event_names) / sizeof(void*)); i += 2) {
    if (event_names[i] == event) {
      event_name = event_names[i+1];
      break;
    }
  }

  // LOGI(TAG, "event %x %s", event, event_name);

  switch (event) {
    /* 连接变化事件 */
    case EVENT_GAP_CONN_CHANGE:
      conn_change(event, event_data);
      break;

     case EVENT_GATT_CHAR_READ:
      event_char_read(event, event_data);
      break;
    case EVENT_GATT_CHAR_WRITE:
      event_char_write(event, event_data);
      break;

    default:
      LOGW(TAG, "Unhandle event %x %s", event, event_name);
      break;
  }

  return 0;
}

static ble_event_cb_t ble_cb = {
  .callback = event_callback,
};

static int start_adv(void)
{
  int ret;
  ad_data_t ad[3] = {0};
  ad_data_t sd[0] = {0};

  prov_state.current_state = IMPROV_STATE_AUTHORIZED;
  prov_state.error_state = IMPROV_NO_ERROR;
  prov_state.connected = false;
  prov_state.provisioning_done = false;

  /* 配置广播数据和扫描响应数据 */
  uint8_t flag = AD_FLAG_GENERAL | AD_FLAG_NO_BREDR;

  ad[0].type = AD_DATA_TYPE_FLAGS;
  ad[0].data = (uint8_t *)&flag;
  ad[0].len = 1;

  uint8_t state = 0x02;
  uint8_t capabilities = 0x1;


  ad[1].type = AD_DATA_TYPE_UUID128_ALL;
  ad[1].data = UUID128(UUID_IMPROV);
  ad[1].len = 16;

  uint8_t service_data[] = {
    0x77,
    0x46,
    state,
    capabilities,
    0x00,
    0x00,
    0x00,
    0x00,
  };
  ad[2].type = AD_DATA_TYPE_SVC_DATA16; //128
  ad[2].data = (uint8_t *)service_data;
  ad[2].len = sizeof(service_data);

  /* 配置广播参数 */
  adv_param_t param = {
    ADV_IND,
    ad,
    NULL,
    BLE_ARRAY_NUM(ad),
    NULL,
    ADV_FAST_INT_MIN_2,
    ADV_FAST_INT_MAX_2,
  };


  ret = ble_stack_adv_start(&param);

  if (ret) {
    LOGE(TAG, "adv start fail %d!", ret);
  } else {
    LOGI(TAG, "adv start!");
  }

  return ret;
}

int improv_start(uint32_t timeout_s) {
  // TODO: Do not start Improv when it's already started
  // I am leaving BT stack init here instead of init on boot, to spare memory and cpu. We use BLE only for provisioning right now
  int ret;

  if (!prov_state.stack_init) {
    dev_addr_t addr = {DEV_ADDR_LE_PUBLIC_ID};
    char device_name[30] = "PineVoice"; // Just use this name, anyway Improv gives us option to identify the device + MAC address can be used to identify
    init_param_t init = {
      .dev_name = device_name,
      .dev_addr = &addr,
      .conn_num_max = 1,
    };

    // bt stack init
    ret = ble_stack_init(&init);
    if (ret) {
      printf("error: ble_stack_init!, ret = %x\r\n", ret);
      return -1;
    }

    ble_stack_event_register(&ble_cb);

    ret = ble_stack_gatt_registe_service(&improv_service, improv_attrs, BLE_ARRAY_NUM(improv_attrs));
    if (ret < 0) LOGE(TAG, "gatt reg failed %d", ret);
    prov_state.svc_handle = ret;
  }

  prov_state.timeout = timeout_s;
  ret = start_adv();
  light_show_state_msg_send(LIGHT_SHOW_NET_UNAUTH, NULL);
  local_audio_play("wifi-provisioning-started.opus");

  // TODO: Timeout
  return ret;
}

static void cmd_improv(char *wbuf, int wbuf_len, int argc, char **argv) {
  improv_start(0);
}

void cli_reg_cmd_improv(void) {  
  static const struct cli_command cmd_info = {"improv", "start improv", cmd_improv};
  aos_cli_register_command(&cmd_info);
}
