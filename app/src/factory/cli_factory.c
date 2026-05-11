/*
 * Copyright (C) 2022 Alibaba Group Holding Limited
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aos/cli.h>
#include <yoc/adb_server.h>
#include <sys/app_sys.h>
#include <yoc/netmgr.h>
#include <yoc/netmgr_service.h>
#include <wifi.h>
#include <msp/record.h>
#include <uservice/eventid.h>
#include <avutil/named_straightfifo.h>
#include <player.h>
#include <yoc/mic.h>

#include "display/pwm_led/pwm_led.h"


extern void fct_pcm_chk_record(char *play_url, int second, int vol, int pcmchk, int savefile);
void fct_step(int step, int argc, char **argv);

#define TAG "FACTORY"

static void cmd_factory_func(char *wbuf, int wbuf_len, int argc, char **argv)
{
    if (argc < 2) {
        return;
    }

    char *fct_cmd = argv[1];

    if (strcmp(fct_cmd, "pcmchk") == 0) {
        if (argc == 6) {
            int rec_sec = atoi(argv[2]);
            int vol = atoi(argv[3]);
            char *play_url = argv[4];
            int savefile = atoi(argv[5]);
            fct_pcm_chk_record(play_url, rec_sec, vol, 1, savefile);
        } else {
            adbserver_send("param error\r\n");
        }
    } else if (strcmp(fct_cmd, "pcmrec") == 0) {
        if (argc >= 3) {
            int rec_sec = atoi(argv[2]);
            fct_pcm_chk_record("none://", rec_sec, -1, 0, 1);
        } else {
            adbserver_send("param error\r\n");
        }
    } else if (strcmp(fct_cmd, "start") == 0) {
        app_sys_reboot(BOOT_REASON_FACTORY_MODE);
    } else if (strcmp(fct_cmd, "step") == 0) {
        if (argc < 3) return;
        fct_step(atoi(argv[2]), argc, argv);
    } else {
        ;
    }
}

static void fct_wifi_test();
static void fct_led_test();
static void fct_speaker_mic_test(int step);

void fct_step(int step, int argc, char **argv)
{
    switch (step) { 
        case 0: fct_wifi_test(); break;
        case 1: fct_led_test(); break;
        case 2: 
            if (argc < 4) return;
            fct_speaker_mic_test(atoi(argv[3]));
            break;
        case 3: {
            static bool cdc_enabled = false;
            if (cdc_enabled) return;
            cdc_enabled = true;
            extern void cdc_init(void);
            usb_clock_init();
            cdc_init();

            usbd_initialize();
            break;
        }
    }
}


extern netmgr_hdl_t app_netmgr_hdl;
static void scan_compeleted(aos_dev_t *dev, uint16_t number, wifi_ap_record_t *ap_records)
{
    wifi_ap_record_t wifiApRecord;
    if (number > 0) {
        LOGI(TAG, "!FCT!WIFI,SUCCESS,%d,%s", number, ap_records[0].ssid);
    } else {
        LOGE(TAG, "!FCT!WIFI,FAIL");
    }

    hal_wifi_install_event_cb(dev, NULL);
    hal_wifi_stop(dev);
}

static void fct_led_test()
{
    LOGI(TAG, "RGB test start");
    light_show_state_msg_send(LIGHT_SHOW_RGB_TEST, NULL);
}

static wifi_event_func wifi_event = {NULL, NULL, scan_compeleted, NULL};
static void fct_wifi_test()
{
    LOGI(TAG, "WiFi test start");
    aos_dev_t *dev = netmgr_get_dev(app_netmgr_hdl);
    hal_wifi_install_event_cb(dev, &wifi_event);
    hal_wifi_start_scan(dev, NULL, 1);
}

extern void play_sintest(int sec);

static uint8_t *g_mem_record = NULL;
static int      g_mem_size   = 0;
static player_t* g_player;
static nsfifo_t* g_playback_fifo;
#define FIFO_PLAYBACK_TEST "fifo://playback_test?avformat=rawaudio&avcodec=pcm_s16le&channel=1&rate=16000"

#if 0
extern int  rec_start_mem(const char *url, int rb_size, int chncnt, uint8_t *mem, int size);
extern void rec_stop(void);
static aos_timer_t record_timer;
static void record_stop_timer(void *timer, void *arg)
{
    
    LOGD(TAG, "REC stop");
    for (int j = 0; j < 32; j++) {
        LOGD(TAG, "SAMPLE: %d\n", g_mem_record[j]);
    }
    rec_stop();
    aos_timer_stop(&record_timer);
    
}
#else
static rec_hdl_t hdl_mic;
static uint32_t record_pos = 0;
static aos_sem_t record_stop_sem;
static volatile uint8_t record_stop_pending = 0;

static int mic_data_ready(void *arg, void *data, size_t rlen);

static int factory_record_open(void)
{
    char buf1[64];

#define PCM_PRIOD_MS 10
#define PCM_RATE     16000
#define PCM_CHN      3
#define PCM_FRAME    16
    snprintf(buf1, sizeof(buf1), "mic://format=%u&sample=%u&chan=%u&frame_ms=%u",
            PCM_FRAME, PCM_RATE, PCM_CHN, PCM_PRIOD_MS);

    int frame_size = PCM_RATE/1000*(PCM_FRAME/8)*PCM_CHN*PCM_PRIOD_MS;

    hdl_mic = record_register(buf1, "null://");
    if (hdl_mic == NULL) {
        LOGE(TAG, "record_register failed");
        return -1;
    }

    record_set_data_ready_cb(hdl_mic, mic_data_ready, NULL);
    record_set_chunk_size(hdl_mic, frame_size);
    return 0;
}

static void factory_record_close(void)
{
    rec_hdl_t hdl = hdl_mic;

    if (hdl == NULL) {
        return;
    }

    hdl_mic = NULL;
    record_stop(hdl);
    record_unregister(hdl);
}

static void factory_record_stop_task(void *arg)
{
    while (1) {
        aos_sem_wait(&record_stop_sem, AOS_WAIT_FOREVER);
        factory_record_close();
        record_stop_pending = 0;
    }
}

static int mic_data_ready(void *arg, void *data, size_t rlen)
{
    uint32_t copy_len;

    if (rlen <= 0) {
        return 0;
    }

    if (g_mem_record == NULL || g_mem_size <= 0 || hdl_mic == NULL) {
        return -1;
    }

    if (record_pos >= (uint32_t)g_mem_size) {
        return -1;
    }

    copy_len = rlen;
    if (record_pos + copy_len > (uint32_t)g_mem_size) {
        copy_len = (uint32_t)g_mem_size - record_pos;
    }

    memcpy(g_mem_record + record_pos, data, copy_len);
    record_pos += copy_len;

    if (record_pos >= (uint32_t)g_mem_size && record_stop_pending == 0) {
        LOGD(TAG, "REC finished, %d %d", record_pos, g_mem_size);
        record_pre_stop(hdl_mic);
        record_stop_pending = 1;
        aos_sem_signal(&record_stop_sem);
    }

    return 0;
}

#endif

static void _player_event(player_t *player, uint8_t type, const void *data, uint32_t len)
{
  UNUSED(len);
  UNUSED(data);

  switch (type) {
  case PLAYER_EVENT_ERROR:
    LOGE(TAG, "Error player!");
    player_stop(g_player);
    break;
  case PLAYER_EVENT_START:
    break;
  case PLAYER_EVENT_FINISH:
    LOGD(TAG, "Finish playing! :)");
    player_stop(g_player);
    nsfifo_close(g_playback_fifo);
    g_playback_fifo = NULL;
    break;
  default:
    break;
  }
}

static void fifo_write(uint8_t* data, uint32_t size)
{
  int wlen;
  char *pos;
  uint8_t reof = 0;

  int off = 0;
  int tmp_len;
  while (1) {
    wlen = nsfifo_get_wpos(g_playback_fifo, &pos, 10*1000);
    nsfifo_get_eof(g_playback_fifo, &reof, NULL);
    if (reof) {
      LOGE(TAG, "get wpos err. wlen = %d, reof = %d", wlen, reof);
      return 0;
    }

    if (wlen <= (size - off)) {
      tmp_len = wlen;
    } else {
      tmp_len = size - off;
    }

    if (tmp_len > 0) {
      memcpy(pos, data + off, tmp_len);
      nsfifo_set_wpos(g_playback_fifo, tmp_len);
      off += tmp_len;
    }

    if (wlen == 0) {
      aos_msleep(100);
    }

    if (off == size) {
      break;
    }
  }
}

static void playback_task(void *args) {
    int channel = args;
    g_playback_fifo = nsfifo_open(FIFO_PLAYBACK_TEST, O_CREAT, 1*1024*1024);
    if (NULL == g_playback_fifo) {
        LOGE(TAG, "nsfifo_open fail");
        return;
    }

    player_play(g_player, FIFO_PLAYBACK_TEST, 0);
    LOGD(TAG, "Start stream %d", channel);
    int16_t buffer[16*100];
    
    int16_t* samples = (int16_t*)g_mem_record;
    for (int i = 0; i < g_mem_size / 2 / 3; i++) {
        int step = i % 1600;
        // 3*2*16*1000 = 96000
        //              102400
        buffer[step] = samples[(i * 3) + channel] * 40.0f;

        if (step == (sizeof(buffer) / 2) - 1) {
            fifo_write(buffer, sizeof(buffer));
            aos_msleep(90);
        }
    }
    nsfifo_set_eof(g_playback_fifo, 0, 1); // set weof

}

void fct_speaker_mic_test(int step)
{
    static bool ply_rec_init = false;
    if (!ply_rec_init) {
#if 0
        aui_mic_register();

        utask_t *task_mic = utask_new("task_mic", 10 * 1024, 20, AOS_DEFAULT_APP_PRI);
        int ret           = aui_mic_init(task_mic);
        // aui_mic_event_register(mic_evt_cb);
        aui_mic_start();
#else
        aos_sem_new(&record_stop_sem, 0);
        aos_task_new("factory_rec_stop", factory_record_stop_task, NULL, 1024 * 4);
#endif

        ply_conf_t ply_cnf;

        player_conf_init(&ply_cnf);
        ply_cnf.resample_rate = 48000;
        ply_cnf.event_cb      = _player_event;
        g_player = player_new(&ply_cnf);

        ply_rec_init = true;
    }
    if (step == 0) {
        g_mem_size   = 3*2*16*1000; // 3ch*16b*16hz*1000ms
        g_mem_record = aos_zalloc_check(g_mem_size);
        record_pos = 0;
#if 0
        aos_timer_new(&record_timer, record_stop_timer, NULL, 1 * 1000, 0);
        rec_start_mem("mem://", 100 * 1024, 3, g_mem_record, g_mem_size);
#else
        if (hdl_mic == NULL && factory_record_open() != 0) {
            aos_free(g_mem_record);
            g_mem_record = NULL;
            g_mem_size = 0;
            return;
        }
        record_start(hdl_mic);  
#endif
        play_sintest(1);
    } else if (step == 1 || step == 2) {
        aos_task_new("prov_result", playback_task, step - 1, 1024*4);
    } else if (step == 3) {
        if (hdl_mic) {
            record_pre_stop(hdl_mic);
            factory_record_close();
        }
        aos_free(g_mem_record);
        g_mem_record = NULL;
        g_mem_size   = 0;
        record_pos = 0;
    }
}


void cli_reg_cmd_factory(void)
{
    static const struct cli_command cmd_info = { "factory", "factory test", cmd_factory_func };

    aos_cli_register_command(&cmd_info);
}
