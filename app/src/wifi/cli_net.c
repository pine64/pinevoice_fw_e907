
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aos/cli.h>
#include "wifi_mgmr_ext.h"
#include "lwip/inet.h"

static void cmd_ifconfig_wifi(char *wbuf, int wbuf_len, int argc, char **argv)
{
	uint32_t ip = 0, gw = 0, mask = 0, dns1 = 0, dns2 = 0;
	uint8_t mac[6];

	wifi_mgmr_sta_ip_get(&ip, &gw, &mask);
	wifi_mgmr_sta_dns_get(&dns1, &dns2);
	wifi_mgmr_sta_mac_get(mac);

	printf("inet %s\r\n", inet_ntoa(ip));
	printf("gw   %s\r\n", inet_ntoa(gw));
	printf("mask %s\r\n", inet_ntoa(mask));
	printf("dns  %s\r\n", inet_ntoa(dns1));
	printf("mac  %02x:%02x:%02x:%02x:%02x:%02x\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

}

void cli_reg_cmd_ifconfig(void)
{
    static const struct cli_command cmd_info = {
        "ifconfig",
        "network config",
        cmd_ifconfig_wifi,
    };

    aos_cli_register_command(&cmd_info);
}

