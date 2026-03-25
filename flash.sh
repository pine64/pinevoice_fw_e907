#!/bin/bash
PV_TTY=$(find /dev -maxdepth 1 -type c -name 'tty*' ! -name 'tty' | head -n1)

../../tools/flashtool/bflb_iot_tool-ubuntu --interface=uart --baudrate=2000000 --chipname=bl606p \
        --boot2="../../boards/bl606p_pinevoice_e907/bootimgs/boot2_isp_release.bin" \
        --firmware="yoc_rfpa.bin" \
        --pt="../../boards/bl606p_pinevoice_e907/configs/partition.toml" \
        --port ${PV_TTY}
if [ "$1" == "cli" ]; then
    tio ${PV_TTY} -b 2000000 -m INLCRNL
fi