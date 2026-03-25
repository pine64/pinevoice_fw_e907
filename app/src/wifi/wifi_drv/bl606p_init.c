/*
 * Copyright (C) 2019-2022 Alibaba Group Holding Limited
 */

#include <devices/wifi.h>
#include <devices/bl606p_wifi.h>

extern int wifi_mgmr_cli_init(void);

int app_wifi_driver_init(void)
{
    wifi_bl606p_register(NULL);
    wifi_mgmr_cli_init();
    return 0;
}
