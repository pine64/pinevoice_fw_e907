/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */
#include <aos/kv.h>
#include <yoc/lpm.h>
#include <yoc/sysinfo.h>

#include <yoc/partition.h>
#include <vfs.h>
#include <vfs_cli.h>
#include <yoc/nvram.h>
#include <littlefs_vfs.h>
#include "vfs_ifs.h"
#include "app_sys.h"

#define TAG                     "APP_SYS"
#define DEVICE_NAME_FORMAT      "YOC-%s"

static int boot_reason = -1;

static void _vfs_init(void)
{
    int ret;

    aos_vfs_init();
    ret = vfs_lfs_register("lfs");
    if (ret != 0) {
        LOGE(TAG, "littlefs register failed(%d)", ret);
        return;
    }
    LOGI(TAG, "filesystem init ok.");

    cli_reg_cmd_ls();
    cli_reg_cmd_rm();
    cli_reg_cmd_cat();
    cli_reg_cmd_mkdir();
    cli_reg_cmd_mv();
}

static void sys_fs_init()
{
    int ret = partition_init();
    if (ret <= 0) {
        LOGE(TAG, "partition init failed");
    } else {
        LOGI(TAG, "find %d partitions", ret);
        if (aos_kv_init("kv")) {
            LOGE(TAG, "kv init failed.");
        }
#ifdef CONFIG_STANDALONE_NVRAM
        if (nvram_init("fct")) {
            LOGE(TAG, "nvram init failed.");
        }
#endif
        _vfs_init();

#if defined(CONFIG_TB_KP) && CONFIG_TB_KP
        extern uint32_t km_init(void);
        km_init();
#endif
    }
    vfs_ifs_server_init();
}

void app_sys_reboot(int reason)
{
    if (reason >= 0 && reason < BOOT_REASON_NONE) {
        app_sys_set_boot_reason(reason);
    }

    ulog_flush();
    aos_reboot();
}

int app_sys_set_boot_reason(int reason)
{
    CHECK_PARAM(reason < BOOT_REASON_NONE && reason >= BOOT_REASON_SOFT_RESET, -1);
    int ret;
    int cur_reason;

    ret = aos_kv_getint("SYS_BOOT_REASON", &cur_reason);
    if (ret != 0 || cur_reason != reason) {
        ret = aos_kv_setint("SYS_BOOT_REASON", reason);
        CHECK_RET_WITH_RET(ret == 0, -1);
    }

    return 0;
}

int app_sys_get_boot_reason()
{
    return boot_reason;
}

void app_sys_set_boot_reason_cache(int reason)
{
    boot_reason = reason;
}

void standby_wku_set_boot_reason_hook(void)
{
    app_sys_set_boot_reason(BOOT_REASON_WAKE_STANDBY);
}

void app_sys_init()
{
    sys_fs_init();

#ifdef CONFIG_DEBUG
    app_sys_except_init(APPEXP_MODE_DEV);
#else
    app_sys_except_init(APPEXP_MODE_RLS);
#endif

    int reason;
    int ret;

    ret = aos_kv_getint("SYS_BOOT_REASON", &reason);
    if (ret != 0 || reason >= BOOT_REASON_NONE || reason < BOOT_REASON_SOFT_RESET) {
        reason = BOOT_REASON_POWER_ON;
    }

    /* set next reboot default reason */
    aos_kv_setint("SYS_BOOT_REASON", BOOT_REASON_POWER_ON);

    /* set current boot reason */
    boot_reason = reason;

    if(BOOT_REASON_WAKE_STANDBY == boot_reason) {
        app_sys_reboot(-1);
    }
}

int app_sys_get_device_name(char dev_name[9])
{
    char *sn;
    char *sn_tail;
    int sn_len;
    static int got_valid = 0;
    static char dev_name_buf[9];

    aos_check_param(dev_name);

    if (got_valid) {
        memcpy(dev_name, dev_name_buf, 9);
        return 0;
    }

    /* device name = Nano-<last 4 bytes of product SN> */
    sn = aos_get_device_id();
    if (sn && (sn_len = strlen(sn)) >= 4) {
        sn_tail = sn + sn_len - 4;
        got_valid = 1;
    } else {
        sn_tail = "1520";
    }

    sprintf(dev_name, DEVICE_NAME_FORMAT, sn_tail);

    if (got_valid) {
        memcpy(dev_name_buf, dev_name, 9);
    }
    LOGD(TAG, "device name %s", dev_name);
    return 0;
}
