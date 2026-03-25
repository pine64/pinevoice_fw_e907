CPRE := @
ifeq ($(V),1)
CPRE :=
VERB := --verbose
endif

cpunum = $(shell cat /proc/cpuinfo| grep "processor"| wc -l)

.PHONY:startup
startup: all

all:
	@echo "Build Solution by $(BOARD) $(SDK) "
	$(CPRE) touch app/src/app_main.c
	echo "scons $(VERB) --board=$(BOARD) --sdk=$(SDK) -j$(cpunum)"
	$(CPRE) scons $(VERB) --board=$(BOARD) --sdk=$(SDK) -j$(cpunum)
	@echo YoC SDK Done

.PHONY:map
map:
	echo "please run 'yoc map' or 'yoc map -o footprint.xlsx'"

.PHONY:flashall
flashall:
	$(CPRE) scons --flash=all --board=$(BOARD) --sdk=$(SDK)

rftlv_bin:
	../../tools/flashtool/bflb_iot_tool-ubuntu --chipname=bl606p --firmware="yoc.bin" --build --dts="../../tools/flashtool/chip_params.dts" --pt="../../tools/flashtool/partition.toml"

.PHONY:erasechip
erasechip:
	$(CPRE) scons --flash=erasechip --board=$(BOARD) --sdk=$(SDK)

.PHONY:flash
flash:
	$(CPRE) scons --flash=prim --board=$(BOARD) --sdk=$(SDK)

.PHONY:flashfs
flashfs:
	$(CPRE) scons --flash=lfs --board=$(BOARD) --sdk=$(SDK)
.PHONY:clean
clean:
	$(CPRE) rm -rf yoc_sdk binary out yoc.*  generated
	$(CPRE) rm -fr gdbinitflash


