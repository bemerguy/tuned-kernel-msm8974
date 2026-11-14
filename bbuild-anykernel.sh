#!/bin/bash
#
# Build the Tuned kernel and pack an AnyKernel3 zip in dist/.
#
# usage: ./bbuild-anykernel.sh <variant>
#   variants: klte klteduos kltedv kltekor kltechn kltekdi klteactive kltespr
#
# optional environment:
#   LLVM=0       build with GCC (arm-linux-gnueabihf-) instead of clang
#   NUM_CPUS=N   parallel jobs (default: nproc - 1)
#   NO_CCACHE=1  don't use ccache
#   FLASH=0      don't offer to install the zip over adb/TWRP

set -o pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 1

VARIANTS="klte klteduos kltedv kltekor kltechn kltekdi klteactive kltespr"
DEFCONFIG="lineage_klte_pn547_defconfig"
TOOLCHAINS="$(readlink -f ../toolchains)"
CLANG_DIR="$TOOLCHAINS/llvm-23.1.5-x86_64/bin"
DTBTOOL="tools_boeffla/dtbToolCM"

COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NEUTRAL="\033[0m"

step() { echo -e "${COLOR_GREEN}\n$*\n${COLOR_NEUTRAL}"; }
die()  { echo -e "${COLOR_RED}\n$*\n${COLOR_NEUTRAL}" >&2; exit 1; }

#######################################
# arguments and toolchain

VAR="$1"
if [ -z "$VAR" ] || ! echo " $VARIANTS " | grep -q " $VAR "; then
	echo "usage: $0 <variant>" >&2
	echo "variants: $VARIANTS" >&2
	exit 1
fi

FILENAME="tuned-$VAR-$(date +"%Y%m%d%H%M")"
NUM_CPUS=${NUM_CPUS:-$(($(nproc) - 1))}

export ARCH=arm
export LLVM=${LLVM:-1}
if [ "$LLVM" = "1" ]; then
	[ -x "$CLANG_DIR/clang" ] || die "clang not found in $CLANG_DIR"
	export PATH="$CLANG_DIR:$PATH"
	export CROSS_COMPILE="$CLANG_DIR/"
	CC_BIN="clang"
else
	export CROSS_COMPILE="arm-linux-gnueabihf-"
	CC_BIN="${CROSS_COMPILE}gcc"
fi

MAKE_ARGS=(O=out ARCH=arm CONFIG_NO_ERROR_ON_MISMATCH=y)
if [ "${NO_CCACHE:-0}" != "1" ] && command -v ccache >/dev/null; then
	MAKE_ARGS+=(CC="ccache $CC_BIN")
fi

#####################
# internal functions
#####################

step2_make_config()
{
	step "2 - make config"

	mkdir -p out
	cp arch/arm/configs/$DEFCONFIG out/.config

case "$VAR" in
        klte)
    echo "Compiling kernel for klte"
    ;;

        kltedv)
    scripts/configcleaner "
CONFIG_NFC_PN547
CONFIG_NFC_PN547_PMC8974_CLK_REQ
CONFIG_BCM2079X_NFC_I2C
"

    echo "
# CONFIG_NFC_PN547 is not set
# CONFIG_NFC_PN547_PMC8974_CLK_REQ is not set
CONFIG_BCM2079X_NFC_I2C=y
" >> out/.config

    echo "Compiling kernel for kltedv"
    ;;

        kltekdi)
    scripts/configcleaner "
CONFIG_MACH_KLTE_EUR
CONFIG_MACH_KLTE_JPN
CONFIG_MACH_KLTE_KDI
CONFIG_NFC_PN547
CONFIG_NFC_PN547_PMC8974_CLK_REQ
CONFIG_USE_VM_KEYBOARD_REJECT
CONFIG_CHARGER_SMB1357
CONFIG_FELICA
CONFIG_NFC_FELICA
CONFIG_CHARGE_LEVEL
"

    echo "
# CONFIG_MACH_KLTE_EUR is not set
CONFIG_MACH_KLTE_JPN=y
CONFIG_MACH_KLTE_KDI=y
# CONFIG_NFC_PN547 is not set
# CONFIG_NFC_PN547_PMC8974_CLK_REQ is not set
# CONFIG_USE_VM_KEYBOARD_REJECT is not set
CONFIG_CHARGER_SMB1357=y
CONFIG_FELICA=y
CONFIG_NFC_FELICA=y
# CONFIG_CHARGE_LEVEL is not set
" >> out/.config

    echo "Compiling kernel for kltekdi"
    ;;

        kltechn)
    scripts/configcleaner "
CONFIG_MACH_KLTE_EUR
CONFIG_MACH_KLTE_CHN
CONFIG_MACH_KLTE_CU
CONFIG_SEC_LOCALE_CHN
CONFIG_WLAN_REGION_CODE
CONFIG_USE_VM_KEYBOARD_REJECT
CONFIG_W1_CF
CONFIG_SND_SOC_ES704_TEMP
CONFIG_SENSORS_FPRINT_SECURE
"
    echo "
# CONFIG_MACH_KLTE_EUR is not set
CONFIG_MACH_KLTE_CHN=y
CONFIG_MACH_KLTE_CU=y
CONFIG_SEC_LOCALE_CHN=y
CONFIG_WLAN_REGION_CODE=300
# CONFIG_USE_VM_KEYBOARD_REJECT is not set
CONFIG_W1_CF=y
CONFIG_SND_SOC_ES704_TEMP=y
CONFIG_SENSORS_FPRINT_SECURE=y
" >> out/.config

    echo "Compiling kernel for kltechn"
    ;;

        kltekor)
    scripts/configcleaner "
CONFIG_MACH_KLTE_EUR
CONFIG_MACH_KLTE_KOR
CONFIG_MACH_KLTE_KTT
CONFIG_MSM_L2_ERP_PORT_PANIC
CONFIG_WLAN_REGION_CODE
CONFIG_SEC_DEVIDE_RINGTONE_GAIN
CONFIG_SND_SOC_ES704_TEMP
CONFIG_USB_LOCK_SUPPORT_FOR_MDM
CONFIG_SENSORS_SSP_SHTC1
"

    echo "
# CONFIG_MACH_KLTE_EUR is not set
CONFIG_MACH_KLTE_KOR=y
CONFIG_MACH_KLTE_KTT=y
CONFIG_MSM_L2_ERP_PORT_PANIC=y
CONFIG_WLAN_REGION_CODE=200
CONFIG_SEC_DEVIDE_RINGTONE_GAIN=y
CONFIG_SND_SOC_ES704_TEMP=y
CONFIG_USB_LOCK_SUPPORT_FOR_MDM=y
CONFIG_SENSORS_SSP_SHTC1=y
" >> out/.config

    echo "Compiling kernel for kltekor"
    ;;

        klteduos)
    scripts/configcleaner "
CONFIG_MACH_KLTE_LTNDUOS
"

    echo "
CONFIG_MACH_KLTE_LTNDUOS=y
" >> out/.config

    echo "Compiling kernel for klteduos"
    ;;

        klteactive)
    scripts/configcleaner "
CONFIG_SEC_K_PROJECT
CONFIG_MACH_KLTE_EUR
CONFIG_SEC_KACTIVE_PROJECT
CONFIG_MACH_KACTIVELTE_EUR
CONFIG_SENSORS_HALL
CONFIG_SENSORS_HALL_IRQ_CTRL
CONFIG_KEYBOARD_CYPRESS_TOUCHKEY
CONFIG_SENSORS_FINGERPRINT
CONFIG_SENSORS_FINGERPRINT_SYSFS
CONFIG_SENSORS_VFS61XX
CONFIG_SENSORS_VFS61XX_KO
CONFIG_SENSORS_FPRINT_SECURE
CONFIG_BOEFFLA_TOUCH_KEY_CONTROL
"

  echo "
# CONFIG_SEC_K_PROJECT is not set
# CONFIG_MACH_KLTE_EUR is not set
CONFIG_SEC_KACTIVE_PROJECT=y
CONFIG_MACH_KACTIVELTE_EUR=y
# CONFIG_SENSORS_HALL is not set
# CONFIG_SENSORS_HALL_IRQ_CTRL is not set
# CONFIG_KEYBOARD_CYPRESS_TOUCHKEY is not set
# CONFIG_SENSORS_FINGERPRINT is not set
# CONFIG_BOEFFLA_TOUCH_KEY_CONTROL is not set
" >> out/.config
;;

esac

	make "${MAKE_ARGS[@]}" olddefconfig || die "make olddefconfig failed"
}

step3_compile()
{
	step "3 - compile (${CC_BIN}, -j$NUM_CPUS${MAKE_ARGS[3]:+, ccache})"

	local t0=$(date +%s)

	# no 'rm -rf out/arch/arm/boot': keep the build incremental and detect
	# failures from make's exit status instead
	rm -f anykernel_boeffla/zImage anykernel_boeffla/dt

	make "${MAKE_ARGS[@]}" -j"$NUM_CPUS" 2>&1 | tee ../compile.log
	[ ${PIPESTATUS[0]} -eq 0 ] && [ -e out/arch/arm/boot/zImage ] ||
		die "Compile was NOT successful !! Aborting. (see ../compile.log)"

	echo -e ">>> compiling DTB\n"
	[ -x "$DTBTOOL" ] || chmod +x "$DTBTOOL"
	"$DTBTOOL" -2 -o out/arch/arm/boot/dt.img -s 2048 -p out/scripts/dtc/ out/arch/arm/boot/ ||
		die "dtbTool failed"

	echo "compile time: $(( $(date +%s) - t0 )) seconds," \
	     "warnings: $(grep -c 'warning:' ../compile.log)"
}

step4_prepare_anykernel()
{
	step "4 - prepare anykernel"

	cp out/arch/arm/boot/zImage anykernel_boeffla/zImage &&
	cp out/arch/arm/boot/dt.img anykernel_boeffla/dt ||
		die "copying zImage/dt failed"
}

step5_create_anykernel_zip()
{
	step "5 - create anykernel zip"

	mkdir -p dist
	rm -f dist/$FILENAME.zip
	(cd anykernel_boeffla && zip -qr9 ../dist/$FILENAME.zip *) || die "zip failed"
	echo "dist/$FILENAME.zip ($(du -h dist/$FILENAME.zip | cut -f1))"
}

step6_flash()
{
	# only when run from a terminal
	[ "${FLASH:-1}" = "1" ] && [ -t 0 ] || return 0
	command -v adb >/dev/null || return 0

	local state reboot=0
	while true; do
		adb start-server >/dev/null 2>&1
		state=$(adb get-state 2>&1)
		if [ "$state" = "recovery" ]; then
			echo "Device is in recovery mode."
			break
		elif [ "$state" = "device" ]; then
			if [ "$(adb shell getprop sys.boot_completed 2>/dev/null)" = "1" ]; then
				echo -e "\a"
				read -p "Device is booted. Press Enter to reboot to recovery and flash (Ctrl+C to skip)..."
				adb reboot recovery
				echo "Rebooting to recovery mode..."
				reboot=1
			fi
		else
			echo "No devices found. Retrying in 5 seconds... (Ctrl+C to skip)"
			[ $reboot -eq 1 ] || echo -e "\a"
		fi
		sleep 5
	done
	sleep 1
	adb push dist/$FILENAME.zip /sdcard/
	sleep 1
	adb shell twrp install /sdcard/$FILENAME.zip
}

################
# main function
################

T0=$(date +%s)
step2_make_config
step3_compile
step4_prepare_anykernel
step5_create_anykernel_zip
echo "total: $(( $(date +%s) - T0 )) seconds. Check 'dist' folder"
step6_flash
