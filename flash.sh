#!/bin/bash
PV_TTY=$(find /dev -maxdepth 1 -type c -name 'tty*' ! -name 'tty' | head -n1)

cp ../../boards/bl606p_pinevoice_e907/configs/eflash_loader_cfg.ini ../../tools/flashtool/chips/bl606p/eflash_loader/eflash_loader_cfg.ini

if [ "$2" == "full" ]; then
    ../../tools/flashtool/bflb_iot_tool-ubuntu --interface=uart --baudrate=2000000 --chipname=bl606p \
        --firmware="yoc_rfpa.bin" \
        --pt="../../boards/bl606p_pinevoice_e907/configs/partition.toml" \
        --media="generated/littlefs.bin" \
        --mfg="../../boards/bl606p_pinevoice_e907/bootimgs/bl606p_mfg_gu_8476f7743.bin" \
        --port ${PV_TTY}

    # --boot2="../../boards/bl606p_pinevoice_e907/bootimgs/boot2_isp_release.bin" \
    # --dts="/workspace/boards/bl606p_pinevoice_e907/configs/chip_params.dts" \ # DTS is not required, as we already flash _rfpa.bin

else
            # --boot2="../../boards/bl606p_pinevoice_e907/bootimgs/boot2_isp_release.bin" \
    ../../tools/flashtool/bflb_iot_tool-ubuntu --interface=uart --baudrate=2000000 --chipname=bl606p \
            --firmware="yoc_rfpa.bin" \
            --pt="../../boards/bl606p_pinevoice_e907/configs/partition.toml" \
            --port ${PV_TTY}
fi
if [ "$1" == "cli" ]; then
    tio ${PV_TTY} -b 2000000 -m INLCRNL
fi