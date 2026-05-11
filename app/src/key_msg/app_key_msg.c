/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */

#include <stdint.h>

#include <aos/kernel.h>
#include <aos/kv.h>
#include <aos/debug.h>

#include <smart_audio.h>
#include <bt/yoc_app_bt.h>
#include <board.h>

#include "app_key_msg.h"
#include "sys/app_sys.h"
#include "event_mgr/app_event.h"
#include "../display/pwm_led/pwm_led.h"
#include <wyoming/satellite.h>
#include <yoc/mic.h>
#define TAG "keymsg"

#define MESSAGE_NUM 10

static uint8_t     s_q_buffer[sizeof(int) * MESSAGE_NUM];
static aos_queue_t s_queue;

void app_key_msg_send(int keymsg_id, void *priv)
{
    LOGD(TAG, "keymsg_id=%d\n", keymsg_id);
    int ret = aos_queue_send(&s_queue, &keymsg_id, sizeof(int));
    if (ret < 0) {
        LOGE(TAG, "queue send failed");
    }
}

__attribute__(( weak )) int smartspeak_key_key_msg_factory_pressed(void)
{
    printf("==================================== smartspeak_key_key_msg_factory_pressed KEY_MSG_PLAY press\r\n");
    return 0;
}

extern int volume2db2regval(int val);

static void key_msg_proc_task(void *arg)
{
    static int vol = 60;
    int    keymsg_id;
    size_t len;

    while (1) {
        aos_queue_recv(&s_queue, AOS_WAIT_FOREVER, &keymsg_id, &len);
        app_event_update(EVENT_KEY_PRESSED);
        if (app_sys_get_boot_reason() == BOOT_REASON_FACTORY_MODE) return;

        switch (keymsg_id) {
            case KEY_MSG_VOL_UP_0:
                // if (light_state_get() != LIGHT_SHOW_NET_AUTH) {
                //     LOGE(TAG, "light state unauth:%d", light_state_get());
                //     break;
                // }
                led_pwm_rgb_config(1000, 1000, 1000, 100, 100, 100);
                break;
            
            case KEY_MSG_VOL_UP:
                // if (light_state_get() != LIGHT_SHOW_NET_AUTH) {
                //     LOGE(TAG, "light state unauth:%d", light_state_get());
                //     break;
                // }
                vol += 10;
                if (vol > 100) {
                    vol = 100;
                    // smtaudio_vol_up(10);
                    // led_pwm_rgb_config(1000, 1000, 1000, 0, 0, 0);
                    // break;
                }
                LOGD(TAG, "vol up:%d", vol);
                volume2db2regval(vol);
                //smtaudio_vol_up(10);
                led_pwm_rgb_config(1000, 1000, 1000, 0, 0, 0);
                break;

            case KEY_MSG_VOL_DOWN_0:
                // if (light_state_get() != LIGHT_SHOW_NET_AUTH) {
                //     LOGE(TAG, "light state unauth:%d", light_state_get());
                //     break;
                // }
                led_pwm_rgb_config(1000, 1000, 1000, 100, 100, 100);
                break;
            
            case KEY_MSG_VOL_DOWN:
                // if (light_state_get() != LIGHT_SHOW_NET_AUTH) {
                //     LOGE(TAG, "light state unauth:%d", light_state_get());
                //     break;
                // }
                vol -= 10;
                if (vol < 0) {
                    vol = 0;
                    // smtaudio_vol_down(10);
                    // led_pwm_rgb_config(1000, 1000, 1000, 0, 0, 0);
                    // break;
                }
                LOGD(TAG, "vol down:%d", vol);
                volume2db2regval(vol);
                led_pwm_rgb_config(1000, 1000, 1000, 0, 0, 0);
                //smtaudio_vol_down(10);
                break;

            case KEY_MSG_MUTE:
                smtaudio_mute();
                app_event_update(EVENT_PLAYER_CHANGE);
                break;

            case KEY_MSG_PLAY: {
#if 0
                smtaudio_state_t smta_state = smtaudio_get_state();
                LOGD(TAG, "smta state %d", smta_state);
                if (smta_state == SMTAUDIO_STATE_PLAYING) {
                    smtaudio_pause();
                } else if (smta_state == SMTAUDIO_STATE_PAUSE || smta_state == SMTAUDIO_STATE_STOP) {
                    smtaudio_resume();
                }
                app_event_update(EVENT_PLAYER_CHANGE);
#else
                LOGD("zz", "center pressedd");
                int32_t ret = wsat_wake_detection();
                
                if (!app_network_internet_is_connected() || ret == -WSAT_ERROR_SAT_DISCONNECTED) {
                    if (!app_network_internet_is_connected()) {
                        local_audio_play("wifi-is-disconnected.opus");
                    } else {
                        local_audio_play("wsat-is-disconnected.opus");
                    }
                    light_show_state_msg_send(LIGHT_SHOW_ERROR, LIGHT_SHOW_MSG_FLAGS(LIGHT_SHOW_MSG_FLAG_INTERRUPT));
                }
                // smartspeak_key_key_msg_play_pressed();
#endif
            } break;

            case KEY_MSG_WIFI_PROV:
                LOGD(TAG, "wifi prov");
                // aos_kv_setint("wprov_method", WIFI_PROVISION_SOFTAP);
                // TODO: PROV
                app_sys_reboot(BOOT_REASON_WIFI_CONFIG);
                break;

            case KEY_MSG_POWER:
                LOGD(TAG, "power key");
                aos_msleep(10000);
                break;

            case KEY_MSG_UPLOG:
                LOGD(TAG, "upload log");
                aos_msleep(10000);
                break;

            case KEY_MSG_FACTORY:
                LOGD(TAG, "factory");
                if (!(app_wifi_config_is_empty() || app_sys_get_boot_reason() == BOOT_REASON_WIFI_CONFIG)) {
                    // light_show_state_msg_send(LIGHT_SHOW_NET_DISCONNECTED, NULL);
                    aos_msleep(1000);
                    app_sys_reboot(BOOT_REASON_WIFI_CONFIG);
                }
                // smartspeak_key_key_msg_factory_pressed();
                // aos_msleep(10000);
                break;

            default:
                break;
        }
    }
}

void app_key_msg_init(void)
{
    aos_task_t task;

    int ret = aos_queue_new(&s_queue, s_q_buffer, MESSAGE_NUM * sizeof(int), sizeof(int));
    aos_check(!ret, EIO);
    aos_task_new_ext(&task, "keymsg", key_msg_proc_task, NULL, 4096, AOS_DEFAULT_APP_PRI + 4);
}
