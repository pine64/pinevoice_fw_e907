#!/bin/bash

set -e
set -x

if [ "$1" == "clean" ];then
    # clean e907
    make clean
    # clean c906
    # cd ../smart_speaker_v2dsp/
    # ./go clean
    # cd -
    exit
fi

BOARD=pinevoice
if [ "$1" != "" ];then
BOARD=$1
else
echo "default use trv03"
fi

if [ "$1" != "dvk65" ];then
DSP_TYPE=""
else
DSP_TYPE=$1
fi

# cd ../smart_speaker_v2dsp/
# ./go smart_speaker_v2 $DSP_TYPE
# cd -

if [ -f "./generated/imtb.bin" ];then
xxd -i < ./generated/imtb.bin > ../../boards/bl606p_${BOARD}_e907/include/imtb_bin.h 
fi

CPU_NUM=`cat /proc/cpuinfo  | egrep 'processor' | wc -l`
make -j${CPU_NUM}

# del gen rftlv_bin, mv to boards\XXXX\script\aft_build.sh
# make rftlv_bin

#### cp ./generated/partition.toml ../../tools/flashtool/
#### cp ./generated/littlefs.bin ../../tools/flashtool/
#### cp ./generated/imtb.bin ../../tools/flashtool/
#### cp ../../tools/flashtool/chips/bl606p/builtin_imgs/boot2_isp_bl606p*/boot2_isp_debug.bin ../../tools/flashtool/boot2.bin
#### 
#### #cp ../../tools/flash_tool/chips/bl606p/builtin_imgs/606p_mfg*/606p_mfg_gu_autoboot.bin ../../flash_tool
#### cp ../../tools/flashtool/chips/bl606p/builtin_imgs/bl606p_mfg*/bl606p_mfg_gu_*.bin ../../tools/flashtool/
#### 
#### if [ ! -f "../../tools/flashtool/yoc.bin" ];then
#### cp yoc.bin ../../tools/flashtool/
#### else
#### rm ../../tools/flashtool/yoc.bin
#### cp yoc.bin ../../tools/flashtool/
#### fi
#### 
#### if [ ! -f "../../tools/flashtool/yoc_rfpa.bin" ];then
#### cp yoc_rfpa.bin ../../tools/flashtool/
#### else
#### rm ../../tools/flashtool/yoc_rfpa.bin
#### cp yoc_rfpa.bin ../../tools/flashtool/
#### fi

#/usr/bin/python2 ../../tools/map_parse_gcc_riscv_e907.py ./yoc.map
#cp app/version.h ../../flash_tool/version

if [ ! -f "../../tools/flashtool/yoc.bin" ];then
    exit 1
else
    exit 0
fi

