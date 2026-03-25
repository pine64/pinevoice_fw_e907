/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */

#include <stdio.h>
#include <aos/kernel.h>
#include <ulog/ulog.h>
#include <aos/cli.h>
#include <aos/yloop.h>
#include <sys_clk.h>
#include <board.h>

#include "sys/app_sys.h"
#include "player/app_player.h"
#include "wifi/app_net.h"
#include "key_msg/app_key_msg.h"
#include "event_mgr/app_event.h"
#include "wyoming/wyoming.h"
#include "mbedtls/threading.h"
#include "app_main.h"
#include "version.h"
#include "bl606p_clock.h"
#include <bl_efuse.h>
//#include "aws_iot_mqtt/app_service.h"
#define DEFAULT_SOFTWARE_VER    "8.01.01.03"
#define TAG "main"

extern void thirdparty_app_init(void);
extern int volume2db2regval(int val);
extern void cxx_system_init(void);

#if 0
#include <mww.h>
#include <mww_streaming_model.h>
#include <hey_jarvis.h>
#include "test_sample.h"
void test()
{
    struct mww_t mww;
    int32_t ret = mww_init(&mww);
    if (ret != 0) {
        fprintf(stderr, "Failed to init mww: %d\n", ret);
        return -1;
    }

    struct mww_streaming_model_t model;
    ret = mww_streaming_model_init(&model, hey_jarvis_tflite, 0.97 * 255, 5, 22860);
    if (ret != 0) {
        fprintf(stderr, "Failed to init model: %d\n", ret);
        return -1;
    }

    mww_set_model(&mww, &model);

    uint8_t* buff;
    uint32_t pos = 0;
    uint32_t time = aos_now_ms();
    while (1) {
        size_t bytes_read = 320;
        buff = test_raw + pos;
        pos += 320;
        if (pos >= test_raw_len) break;
        struct mww_detection_info_t info;
        ret = mww_do_inference(&mww, buff, bytes_read, &info);
        if (ret >= 0 && info.detected) {
            static const float uint8_to_float_divisor =
            255.0f;  // Converting a quantized uint8 probability to floating point
            LOGD("MWW", "Detected Hey Jarvis with sliding average probability is %.2f and max probability is %.2f\n",
                (info.average_probability / uint8_to_float_divisor),
                (info.max_probability / uint8_to_float_divisor));
        }
    }
    LOGD("MWW", "DONE!!! %d", (aos_now_ms() - time));
}
#endif

int smartspeaker_main(int argc, char *argv[])
{

#if defined(CONDIF_APP_AMAZON_ENABLE) && (CONDIF_APP_AMAZON_ENABLE == 1)
    volume2db2regval(60);
#endif

    cxx_system_init();

    mbedtls_threading_set_alt(threading_mutex_init,
            threading_mutex_free,
            threading_mutex_lock,
            threading_mutex_unlock);

    board_yoc_init();

    app_sys_init();

    // test();
    app_sys_set_boot_reason_cache(BOOT_REASON_FACTORY_MODE);

    app_cli_init();

    printf("git commit:"BL606P_GIT_VERSION"\r\n");
    printf("fw  commit:"BL606P_WIFI_FW_VERSION"\r\n");
    printf("phy commit:"BL606P_PHY_VERSION"\r\n");
    printf("DEFAULT_SOFTWARE_VER: %s \r\n", DEFAULT_SOFTWARE_VER);

#if CONFIG_BT_RAMBTDUMP_DEBUG
    printf("erase flash, please wait sometimes!\r\n");
#endif
    LOGI("main", "version: %s build time: %s, %s clock=%uHz psram_clk:%uHZ\r\n", DEFAULT_SOFTWARE_VER, __DATE__, __TIME__, soc_get_cur_cpu_freq(),
            Clock_Peripheral_Clock_Get(BL_PERIPHERAL_CLOCK_PSRAMB));
    //aos_msleep(5000);

#if CONFIG_BT_RAMBTDUMP_DEBUG
    bl_flash_erase(0x5BB000, 0x57F000);
#endif
    app_event_init();

    app_key_msg_init();

    app_speaker_init();

    app_player_init();

#if defined(CONFIG_WIFI_NET) && (CONFIG_WIFI_NET == 1)
    app_network_init();
#endif

    // app_mic_init();

    board_app_init();

    if (app_sys_get_boot_reason() != BOOT_REASON_FACTORY_MODE) {
        wyoming_init();
        wyoming_start();
    } else {
        printf("!FCT!READY\r\n");
    }
    volume2db2regval(70);


    return 0;
}
