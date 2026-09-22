# SPDX-License-Identifier: GPL-2.0+
.DEFAULT_GOAL := edk2
EDK2_SOURCE ?= $(abspath ../edk2)
EDK2_BOARD ?= meles
EDK2_TARGET ?= RELEASE
CROSS_COMPILE ?= riscv64-linux-gnu-
JOBS ?= 8
BUILD := $(CURDIR)/build
EDK2_BUILD := $(BUILD)/$(EDK2_BOARD)
# Existing packaged images/DTBs are inputs to optional integration checks.
FIRMWARE_BUILD ?= $(abspath ../build)

ifeq ($(EDK2_BOARD),meles)
EDK2_PLATFORM := Meles
EDK2_BOOT := spi-nor
EDK2_IMAGE := meles-edk2-spi.bin
IMAGE_BUILD := $(FIRMWARE_BUILD)/edk2
else ifeq ($(EDK2_BOARD),lpi4a)
EDK2_PLATFORM := Lpi4a
EDK2_BOOT := emmc-boot0
EDK2_IMAGE := lpi4a-edk2-boot0.bin
IMAGE_BUILD := $(FIRMWARE_BUILD)/edk2-lpi4a
else ifeq ($(EDK2_BOARD),meles-recovery)
EDK2_PLATFORM := MelesRecovery
else
$(error Unsupported EDK2_BOARD=$(EDK2_BOARD))
endif

export EDK2_SOURCE EDK2_BOARD EDK2_TARGET CROSS_COMPILE JOBS

.PHONY: all edk2 edk2-firmware edk2-lpi4a basetools edk2-hsm-test
all edk2: edk2-firmware
basetools:
	$(MAKE) -C $(EDK2_SOURCE)/BaseTools -j$(JOBS)

edk2-firmware: basetools
	tools/build.sh
	mkdir -p $(EDK2_BUILD)
	cp \
		$(BUILD)/Build/$(EDK2_PLATFORM)/$(EDK2_TARGET)_GCC/FV/TH1520.fd \
		$(EDK2_BUILD)/TH1520.fd

edk2-lpi4a:
	$(MAKE) edk2 EDK2_BOARD=lpi4a

.PHONY: edk2-recovery
edk2-recovery:
	$(MAKE) edk2 EDK2_BOARD=meles-recovery

edk2-hsm-test: basetools
	tools/build.sh -D HSM_TEST -m tests/HsmTest/HsmTest.inf

.PHONY: check check-artifacts
check: edk2-display-check edk2-usb-check edk2-reset-check
check: edk2-gmac-address-check
check-artifacts: edk2-meles-usb-check edk2-lpi4a-usb-check
check: edk2-recovery-check
check-artifacts: edk2-check edk2-gmac-check
	python3 tests/edk2-usb-dt.py $(IMAGE_BUILD)/$(EDK2_BOARD).dtb \
		$(IMAGE_BUILD)/uboot/$(EDK2_IMAGE)

.PHONY: edk2-display-check
edk2-display-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -Wall -Wextra -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		tests/edk2-display.c \
		platforms/Th1520Pkg/Drivers/DisplayDxe/Edid.c \
		-o $(EDK2_BUILD)/test-display
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-display
	$(CC) -g -Wall -Wextra -fsanitize=address,undefined -fshort-wchar \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-Iplatforms/Th1520Pkg/Include tests/edk2-display-hw.c \
		platforms/Th1520Pkg/Drivers/DisplayDxe/Hardware.c \
		platforms/Th1520Pkg/Drivers/DisplayDxe/Edid.c \
		-o $(EDK2_BUILD)/test-display-hw
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-display-hw

.PHONY: edk2-usb-check
edk2-usb-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		tests/edk2-usb-dma.c \
		platforms/Th1520Pkg/Drivers/UsbHostDxe/Dma.c \
		-o $(EDK2_BUILD)/test-usb-dma
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-usb-dma

.PHONY: edk2-reset-check
edk2-reset-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -ffunction-sections \
		-fdata-sections \
		-Wl,--gc-sections -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		tests/edk2-reset.c \
		platforms/Th1520Pkg/Library/ResetSystemLib/ResetSystemLib.c \
		-o $(EDK2_BUILD)/edk2-reset
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/edk2-reset

.PHONY: edk2-gmac-address-check
edk2-gmac-address-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -D_PCD_GET_MODE_BOOL_PcdFirmwareOnEmmc=0 \
		-g -fsanitize=address,undefined -ffunction-sections \
		-fdata-sections \
		-Wl,--gc-sections -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/EmbeddedPkg/Include \
		-I$(EDK2_SOURCE)/CryptoPkg/Include \
		-Iplatforms/Th1520Pkg/Include \
		tests/edk2-gmac-address.c \
		platforms/Th1520Pkg/Drivers/GmacDxe/Address.c \
		platforms/Th1520Pkg/Library/MacStoreLib/Random.c \
		-o $(EDK2_BUILD)/test-gmac-address
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-gmac-address
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-gmac-address saved
	@for kind in zero broadcast multicast short long empty volatile; do \
		ASAN_OPTIONS=detect_leaks=0 \
			$(EDK2_BUILD)/test-gmac-address $$kind || exit $$?; \
	done

BOARD_LIB = platforms/Th1520Pkg/Library/BoardSupportLib
FDT_SOURCES = $(BOARD_LIB)/Fdt.c $(BOARD_LIB)/Pinctrl.c
BOARD_SOURCES = $(FDT_SOURCES) $(BOARD_LIB)/Resources.c \
  $(BOARD_LIB)/BoardSupport.c
FDT_TEST_FLAGS = -g -fsanitize=address,undefined -fshort-wchar \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I$(EDK2_SOURCE)/MdePkg/Include \
  -I$(EDK2_SOURCE)/MdePkg/Include/X64 \
  -I$(EDK2_SOURCE)/EmbeddedPkg/Include \
  -Iplatforms/Th1520Pkg/Include

.PHONY: edk2-meles-usb-check edk2-lpi4a-usb-check
edk2-meles-usb-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) -Wl,--wrap=Th1520Pca9557Output \
		tests/edk2-usb-power.c \
		tests/edk2-fdt-host.c $(BOARD_SOURCES) \
		platforms/MelesPkg/Library/BoardLib/Board.c \
		-lfdt -o $(EDK2_BUILD)/test-meles-usb-power
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-meles-usb-power \
		$(FIRMWARE_BUILD)/edk2/meles.dtb

edk2-lpi4a-usb-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) -Wl,--wrap=Th1520Pca9557Output \
		-DTEST_LPI4A tests/edk2-usb-power.c \
		tests/edk2-fdt-host.c $(BOARD_SOURCES) \
		platforms/Lpi4aPkg/Library/BoardLib/Board.c \
		-lfdt -o $(EDK2_BUILD)/test-lpi4a-usb-power
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-lpi4a-usb-power \
		$(FIRMWARE_BUILD)/edk2-lpi4a/lpi4a.dtb

.PHONY: edk2-check
edk2-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/CryptoPkg/Include \
		tests/edk2-image.c \
		platforms/Th1520Pkg/Application/Fastboot/Image.c \
		-lfdt -lcrypto -o $(EDK2_BUILD)/test-image
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-image \
		$(IMAGE_BUILD)/uboot/$(EDK2_IMAGE) $(EDK2_BOARD) \
		$(EDK2_BOOT)

.PHONY: edk2-gmac-check
edk2-gmac-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) tests/edk2-gmac-fdt.c \
		tests/edk2-fdt-host.c $(FDT_SOURCES) \
		platforms/Th1520Pkg/Drivers/GmacDxe/Fdt.c \
		-lfdt -o $(EDK2_BUILD)/test-gmac-fdt
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-gmac-fdt \
		$(FIRMWARE_BUILD)/edk2/meles.dtb \
		$(FIRMWARE_BUILD)/edk2-lpi4a/lpi4a.dtb

.PHONY: edk2-recovery-check
edk2-recovery-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -Wall -Wextra -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		tests/edk2-recovery.c \
		platforms/Th1520Pkg/Application/Fastboot/Targets.c \
		-o $(EDK2_BUILD)/test-recovery
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-recovery

.PHONY: edk2-io-test edk2-emmc-check
edk2-io-test: basetools
	tools/build.sh -D IO_TEST -m tests/IoTest/IoTest.inf

check: edk2-emmc-check
edk2-emmc-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -Wall -Wextra -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		tests/edk2-emmc-adma.c \
		platforms/Th1520Pkg/Drivers/EmmcDxe/Adma.c \
		-o $(EDK2_BUILD)/test-emmc-adma
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-emmc-adma

.PHONY: edk2-net-test edk2-usb-register-check
edk2-net-test: basetools
	tools/build.sh -D NET_TEST -m tests/NetTest/NetTest.inf

check: edk2-usb-register-check
edk2-usb-register-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=undefined -ffunction-sections \
		-fdata-sections -Wl,--gc-sections -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include \
		-Iplatforms/Th1520Pkg/Include tests/edk2-usb-registers.c \
		-o $(EDK2_BUILD)/test-usb-registers
	$(EDK2_BUILD)/test-usb-registers

.PHONY: edk2-gmac-queue-check
check: edk2-gmac-queue-check
edk2-gmac-queue-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=undefined -ffunction-sections \
		-fdata-sections -Wl,--gc-sections -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/EmbeddedPkg/Include \
		-Iplatforms/Th1520Pkg/Include tests/edk2-gmac-queue.c \
		-o $(EDK2_BUILD)/test-gmac-queue
	$(EDK2_BUILD)/test-gmac-queue

.PHONY: edk2-emmc-fdt-check
check: edk2-emmc-fdt-check
edk2-emmc-fdt-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=undefined -ffunction-sections \
		-fdata-sections -Wl,--gc-sections -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/EmbeddedPkg/Include \
		-Iplatforms/Th1520Pkg/Include tests/edk2-emmc-fdt.c \
		-o $(EDK2_BUILD)/test-emmc-fdt
	$(EDK2_BUILD)/test-emmc-fdt

.PHONY: edk2-hotplug-check
check: edk2-hotplug-check
edk2-hotplug-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include \
		-I$(EDK2_SOURCE)/EmbeddedPkg/Include \
		tests/edk2-display-hotplug.c -o $(EDK2_BUILD)/test-hotplug
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-hotplug

.PHONY: edk2-nvstore-check
check: edk2-nvstore-check
edk2-nvstore-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-Iplatforms/Th1520Pkg/Include tests/edk2-nvstore.c \
		platforms/Th1520Pkg/Library/NvStoreLib/Journal.c \
		platforms/Th1520Pkg/Library/NvStoreLib/Records.c \
		-lz -o $(EDK2_BUILD)/test-nvstore
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-nvstore

KERNEL_SOURCE ?= $(abspath ../linux-mainline)
DTS_DIR = $(KERNEL_SOURCE)/arch/riscv/boot/dts/thead
ifeq ($(EDK2_BOARD),lpi4a)
FIRMWARE_DTS = $(DTS_DIR)/th1520-lichee-pi-4a.dts
else
FIRMWARE_DTS = platforms/MelesPkg/Firmware.dts
endif
.PHONY: edk2-dtb
edk2-dtb:
	mkdir -p $(EDK2_BUILD)
	$(CC) -E -P -x assembler-with-cpp -nostdinc -undef -D__DTS__ \
		-I$(KERNEL_SOURCE)/include \
		-I$(KERNEL_SOURCE)/arch/riscv/boot/dts/thead \
		$(FIRMWARE_DTS) -o $(EDK2_BUILD)/firmware.dts
	dtc -I dts -O dtb $(EDK2_BUILD)/firmware.dts \
		-o $(EDK2_BUILD)/firmware.dtb

.PHONY: edk2-gmac-policy-check
check: edk2-gmac-policy-check
edk2-gmac-policy-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) tests/edk2-gmac-policy.c \
		platforms/Th1520Pkg/Library/MacStoreLib/MacStore.c \
		-o $(EDK2_BUILD)/test-gmac-policy
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-gmac-policy

.PHONY: edk2-mac-store-check
check: edk2-mac-store-check
edk2-mac-store-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-Iplatforms/Th1520Pkg/Include tests/edk2-mac-store.c \
		platforms/Th1520Pkg/Library/MacStoreLib/MacStore.c \
		-lz -o $(EDK2_BUILD)/test-mac-store
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-mac-store

.PHONY: edk2-spi-runtime-check
check: edk2-spi-runtime-check
edk2-spi-runtime-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) -g -fsanitize=address,undefined -fshort-wchar \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I$(EDK2_SOURCE)/MdePkg/Include \
		-I$(EDK2_SOURCE)/MdePkg/Include/X64 \
		-Iplatforms/Th1520Pkg/Include tests/edk2-spi-runtime.c \
		-o $(EDK2_BUILD)/test-spi-runtime
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-spi-runtime

.PHONY: edk2-nv-runtime-check
check: edk2-nv-runtime-check
edk2-nv-runtime-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include tests/edk2-nv-runtime.c \
		platforms/Th1520Pkg/Library/NvStoreLib/Journal.c \
		platforms/Th1520Pkg/Library/NvStoreLib/Records.c \
		-lz -o $(EDK2_BUILD)/test-nv-runtime
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-nv-runtime

.PHONY: edk2-mac-parse-check
check: edk2-mac-parse-check
edk2-mac-parse-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) tests/edk2-mac-parse.c \
		platforms/Th1520Pkg/Drivers/MacConfigDxe/Parse.c \
		platforms/Th1520Pkg/Library/MacStoreLib/MacStore.c \
		-o $(EDK2_BUILD)/test-mac-parse
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-mac-parse

.PHONY: edk2-usb-start-check
check: edk2-usb-start-check
edk2-usb-start-check:
	mkdir -p $(EDK2_BUILD)
	$(CC) $(FDT_TEST_FLAGS) \
		-I$(EDK2_SOURCE)/MdeModulePkg/Include tests/edk2-usb-start.c \
		-o $(EDK2_BUILD)/test-usb-start
	ASAN_OPTIONS=detect_leaks=0 $(EDK2_BUILD)/test-usb-start
