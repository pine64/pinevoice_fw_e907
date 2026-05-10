// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include <aos/cli.h>
#include <aos/kernel.h>
#include <ulog/ulog.h>
#include <wyoming/satellite.h>
#include <yoc/mic.h>
#include <avutil/named_straightfifo.h>
#include <player.h>
#include "../display/pwm_led/pwm_led.h"
#include "wyoming.h"

#if 0
static uint8_t audio_data[320*20];
#else
#define AUD_SAMP_CNT 5
static uint8_t audio_data[2][320*AUD_SAMP_CNT];
static uint8_t audio_data_sel = 0;
#endif
static aos_sem_t audio_sem; 
static uint8_t buffers_cnt = 0;
static uint8_t data_ready = 0;

// extern int mwd_state;
// extern int ps_state;
// extern int r_state;
int mwd_state = 0;
int ps_state = 0;
int r_state = 0;


static void mic_evt_cb(int source, mic_event_id_t evt_id, void *data, int size) {
  static uint32_t i = 0;
  switch (evt_id) {
    case MIC_EVENT_SESSION_START: {
      LOGD("haw", "WAKE UP!!!");
      int32_t ret = wsat_wake_detection();
      if (!app_network_internet_is_connected() || ret == -WSAT_ERROR_SAT_DISCONNECTED) {
        if (!app_network_internet_is_connected()) {
          local_audio_play("wifi-is-disconnected.opus");
        } else {
          local_audio_play("wsat-is-disconnected.opus");
        }
        light_show_state_msg_send(LIGHT_SHOW_ERROR, NULL);
      }
      break;
    }
    case MIC_EVENT_PCM_DATA: {
      // printf("SIZE: %d\n", size);
      memcpy(audio_data[audio_data_sel] + (320 * buffers_cnt), data, size);
      buffers_cnt++;
      if (buffers_cnt >= AUD_SAMP_CNT) {
        if (data_ready) {
          LOGE("haw", "Data not processed mwd: %d, ps: %d, r: %d!!!!", mwd_state, ps_state, r_state);
        }
        buffers_cnt = 0;
        audio_data_sel = audio_data_sel ? 0 : 1;
        data_ready = 1;
        aos_sem_signal(&audio_sem);
      }
      // memcpy(audio_data + (320 * buffers_cnt), data, size);
      // buffers_cnt++;
      // if (buffers_cnt == 20) {
      //   wsat_mic_write_data(audio_data, sizeof(audio_data));
      //   buffers_cnt = 0;
      // }
      // wsat_mic_write_data(data, size);
      // if (i++ % 32 == 0) {
      //   LOGD("haw", "Got microphone shit: %d, s: %d", i, size);
      // }
      break;
    }
  }
}

static struct wsat_wake wake = {
  {
    WSAT_COMPONENT_TYPE_WAKE,
    NULL,
    NULL,
    NULL,
    false,
  },
  "hey jarvis"
};

static void mic_streamer_fn(void *arg)
{
  while (1) {
    aos_sem_wait(&audio_sem, AOS_WAIT_FOREVER);
    static uint8_t data[320*AUD_SAMP_CNT];
    uint8_t data_sel = audio_data_sel ? 0 : 1;
    memcpy(data, audio_data[data_sel], sizeof(data));
    data_ready = 0;
    wsat_mic_write_data(data, sizeof(data));
  }
}

static int32_t mic_init()
{
  aos_sem_create(&audio_sem, 0, NULL);
  aos_task_t task_handle;
  aos_task_new_ext(&task_handle, "mic_streamer", mic_streamer_fn, (void *)NULL, 4096, AOS_DEFAULT_APP_PRI);

  LOGI("haw", "MIC_CTRL_START_PCM");
  aui_mic_control(MIC_CTRL_START_PCM);
  return 0;
}

static int32_t mic_destroy()
{
  LOGI("haw", "MIC_CTRL_STOP_PCM");
  aui_mic_control(MIC_CTRL_STOP_PCM);
  return 0;
}

struct wsat_microphone mic = {
  {
    WSAT_COMPONENT_TYPE_MICROPHONE,
    mic_init,
    mic_destroy,
    NULL,
    false,
  },
  16000,
  2,
  1,
};

static nsfifo_t* g_playback_fifo;
static player_t* g_player;
// TODO: Make FIFO adjustable
#define FIFO_TTS_ADAM "fifo://ha_tts?avformat=rawaudio&avcodec=pcm_s16le&channel=1&rate=22050"

static void _player_event(player_t *player, uint8_t type, const void *data, uint32_t len)
{
  UNUSED(len);
  UNUSED(data);

  switch (type) {
  case PLAYER_EVENT_ERROR:
    LOGE("wyoming", "Error player!");
    player_stop(g_player);
    break;
  case PLAYER_EVENT_START:
    break;
  case PLAYER_EVENT_FINISH:
    LOGD("wyoming", "Finish playing! :)");
    player_stop(g_player);
    light_show_state_msg_send(LIGHT_SHOW_NET_AUTH, NULL);
    // led_pwm_rgb_stop();
    nsfifo_close(g_playback_fifo);
    g_playback_fifo = NULL;
    break;
  default:
    break;
  }
}

static int32_t snd_start_stream(uint32_t rate, uint8_t width, uint8_t channels)
{
  // TODO: Check if FIFO is opened or not.

  g_playback_fifo = nsfifo_open(FIFO_TTS_ADAM, O_CREAT, 1*1024*1024);
  if (NULL == g_playback_fifo) {
    LOGE("wyoming", "nsfifo_open fail");
    return;
  }

  player_play(g_player, FIFO_TTS_ADAM, 0);
  LOGD("wyo", "Start stream %d", rate);
  return 0;
}

static int32_t snd_stop_stream()
{
  nsfifo_set_eof(g_playback_fifo, 0, 1); // set weof
  LOGD("wyo", "Stop stream");
  return 0;
}

static int32_t snd_on_data(uint8_t* data, uint32_t size)
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
      LOGE("wyoming", "get wpos err. wlen = %d, reof = %d", wlen, reof);
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
  return 0;
}

int32_t snd_init()
{
  static int is_player_init = 0;
  if(!is_player_init)
  {
    ply_conf_t ply_cnf;

    player_conf_init(&ply_cnf);
    ply_cnf.resample_rate = 48000;
    ply_cnf.event_cb      = _player_event;
    g_player = player_new(&ply_cnf);

    is_player_init = 1;
    LOGD("wyo", "Player init!");
  }
}

int32_t snd_handle_sys_event(enum wsat_sys_event_type type, void* data)
{
  switch (type) {
  case WSAT_SYS_EVENT_SND_AUDIO_START: {
    struct wsat_sys_event_audio_start_params* info = data;
    snd_start_stream(info->rate, info->width, info->channels);
    break;
  }
  case WSAT_SYS_EVENT_SND_AUDIO_DATA: {
    struct wsat_sys_event_buffer_params* buffer = data;
    snd_on_data(buffer->data, buffer->size);
    break;
  }
  case WSAT_SYS_EVENT_SND_AUDIO_END: {
    snd_stop_stream();
  }
  default: break;
  }
}

static struct wsat_sound snd = {
  {
    WSAT_COMPONENT_TYPE_SOUND,
    snd_init,
    NULL,
    snd_handle_sys_event,
    false,
  }
};

void wyoming_event(int type)
{
  if (type == 0) {
    light_show_state_msg_send(LIGHT_SHOW_LISTENING_START, NULL);
    // light_show_state_msg_send(LIGHT_SHOW_LISTENING_ACTIVE, NULL);
  } else if (type == 1) {
    // led_pwm_rgb_stop();
    light_show_state_msg_send(LIGHT_SHOW_NET_AUTH, NULL);
  } else if (type == 2) {
    light_show_state_msg_send(LIGHT_SHOW_TALKING_ACTIVE, NULL);
  } else if (type == 3) {
    light_show_state_msg_send(LIGHT_SHOW_NET_CONNECTED, NULL);
  }
}

static void wyoming_server(void *arg)
{
  wsat_init();
  wsat_mic_set(&mic);
  wsat_snd_set(&snd);
  wsat_wake_set(&wake);
  wsat_run();
}

void wyoming_start(void)
{
  aos_task_t task_handle;
  aos_task_new_ext(&task_handle, "wyoming_server", wyoming_server, (void *)NULL, 4096, AOS_DEFAULT_APP_PRI);
}

void wyoming_init()
{
  aui_mic_register();

  utask_t *task_mic = utask_new("task_mic", 10 * 1024, 20, AOS_DEFAULT_APP_PRI);
  int ret           = aui_mic_init(task_mic);
  aui_mic_event_register(mic_evt_cb);
  aui_mic_start();
  wyoming_mdns_advertise_start();
  LOGI("wyo", "Wyoming init\r\n");
}

void cmd_wyoming(char *wbuf, int wbuf_len, int argc, char **argv) {
  LOGI("wyo", "Starting Wyoming...\r\n");
  wyoming_start();
}

void cli_reg_cmd_wyoming(void) {
    
  static const struct cli_command cmd_info = {"wyoming", "start Wyoming", cmd_wyoming};
  aos_cli_register_command(&cmd_info);
}
