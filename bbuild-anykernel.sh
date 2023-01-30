#!/bin/bash

# Boeffla Kernel Universal Build Script
#
# Version 1.3, 11.10.2016
#
# (C) Lord Boeffla (aka andip71)

#######################################
# Parameters to be configured manually
#######################################
VAR="$2"

BOEFFLA_FILENAME="tuned-kernel-$(date +"%Y%m%d%H%M")-$VAR"

TOOLCHAIN="/root/armv7-eabihf--glibc--bleeding-edge-2022.08-1/bin/arm-buildroot-linux-gnueabihf-"

COMPILER_FLAGS_KERNEL="-Wno-maybe-uninitialized -Wno-array-bounds"
COMPILER_FLAGS_MODULE="-Wno-maybe-uninitialized -Wno-array-bounds"

COMPILE_DTB="y"
DTBTOOL="dtbToolCM"
DTBTOOL_CMD="-2"
MODULES_IN_SYSTEM="y"

DEFCONFIG="lineage_klte_pn547_defconfig"

KERNEL_NAME="Boeffla-Kernel"

NUM_CPUS=""   # number of cpu cores used for build (leave empty for auto detection)

#######################################
# automatic parameters, do not touch !
#######################################

COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NEUTRAL="\033[0m"

if [ -z "$NUM_CPUS" ]; then
	NUM_CPUS=$(($(nproc) - 1))
fi

# set environment
export ARCH=arm
export CROSS_COMPILE="$TOOLCHAIN"


#####################
# internal functions
#####################

step0_copy_code()
{
	echo -e $COLOR_GREEN"\n0 - copy code\n"$COLOR_NEUTRAL

}

step2_make_config()
{
	echo -e $COLOR_GREEN"\n2 - make config\n"$COLOR_NEUTRAL
	echo

	# build make string depending on if we need to compile to an output folder
	# and if we need to have a defconfig variant
	MAKESTRING="ARCH=arm oldconfig"

	if [ ! -z "out" ]; then
		mkdir -p out
		MAKESTRING="O=out $MAKESTRING"
	        cp arch/arm/configs/$DEFCONFIG out/.config
	fi

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

	echo "Makestring: $MAKESTRING"
	make $MAKESTRING
}

step3_compile()
{
	echo -e $COLOR_GREEN"\n3 - compile\n"$COLOR_NEUTRAL

	TIMESTAMP1=$(date +%s)

        # remove a previous kernel image
        rm -rf out/arch/arm/boot &>/dev/null
	rm anykernel_boeffla/zImage &>/dev/null
	rm anykernel_boeffla/dt &>/dev/null

	make -j$NUM_CPUS O=out CFLAGS_KERNEL="$COMPILER_FLAGS_KERNEL" CFLAGS_MODULE="$COMPILER_FLAGS_MODULE" CONFIG_NO_ERROR_ON_MISMATCH=y 2>&1 |tee ../compile.log

       # if kernel image does not exist, exit processing
       if [ ! -e out/arch/arm/boot/zImage ]; then
               echo -e $COLOR_RED
               echo ""
               echo "Compile was NOT successful !! Aborting."
               echo ""
               echo -e $COLOR_NEUTRAL
               exit
       fi

	# compile dtb if required
	if [ "y" == "$COMPILE_DTB" ]; then
		echo -e ">>> compiling DTB\n"
		echo

		chmod 777 tools_boeffla/$DTBTOOL
		tools_boeffla/$DTBTOOL $DTBTOOL_CMD -o out/arch/arm/boot/dt.img -s 2048 -p out/scripts/dtc/ out/arch/arm/boot/
	fi

	TIMESTAMP2=$(date +%s)

	# Log compile time (screen output)
	echo "compile time:" $(($TIMESTAMP2 - $TIMESTAMP1)) "seconds"

}

step4_prepare_anykernel()
{
	echo -e $COLOR_GREEN"\n4 - prepare anykernel\n"$COLOR_NEUTRAL

	# copy kernel image
	cp out/arch/arm/boot/zImage anykernel_boeffla/zImage

	# copy dtb (if we have one)
	if [ "y" == "$COMPILE_DTB" ]; then
		cp out/arch/arm/boot/dt.img anykernel_boeffla/dt
	fi

}

step5_create_anykernel_zip()
{
	echo -e $COLOR_GREEN"\n5 - create anykernel zip\n"$COLOR_NEUTRAL

	# Creating recovery flashable zip
	echo -e ">>> create flashable zip\n"

	cd anykernel_boeffla

	# create zip file
	mkdir -p ../dist

	rm ../dist/$BOEFFLA_FILENAME.zip &>/dev/null
	zip -r9 ../dist/$BOEFFLA_FILENAME.zip *

}


################
# main function
################

unset CCACHE_DISABLE

case "$1" in
	rel)
		export CCACHE_DISABLE=1
		step0_copy_code
		step2_make_config
		step3_compile
		step4_prepare_anykernel
		step5_create_anykernel_zip
		;;
	a)
		step0_copy_code
		step2_make_config
		step3_compile
		step4_prepare_anykernel
		step5_create_anykernel_zip
		;;
	u)
		step3_compile
		step4_prepare_anykernel
		step5_create_anykernel_zip
		;;
	0)
		step0_copy_code
		;;
	1)
		;;
	2)
		step2_make_config
		;;
	3)
		step3_compile
		;;
	4)
		step4_prepare_anykernel
		;;
	5)
		step5_create_anykernel_zip
		;;
	6)
		# do nothing
		;;
	7)
		;;
	8)
		step8_transfer_kernel
		;;
	9)
		step9_send_finished_mail
		;;

	*)
		display_help
		;;
esac
