#!/bin/bash
#path to gcc toolchain
TOOLCHAIN="/home/me/x-tools/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-"

#I'm gonna test using clang later
#export LLVM=1

if [ "$LLVM" = "1" ]; then
TOOLCHAIN="/usr/lib/llvm-21/bin/"
fi

export ARCH=arm

#######################################
#do not change anything below this if you dont know what youre doing
VAR="$1"

FILENAME="tuned-$VAR-$(date +"%Y%m%d%H%M")"

COMPILE_DTB="y"
DTBTOOL="dtbToolCM"
DTBTOOL_CMD="-2"

DEFCONFIG="lineage_klte_pn547_defconfig"

COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NEUTRAL="\033[0m"

if [ -z "$NUM_CPUS" ]; then
	NUM_CPUS=$(($(nproc) - 1))
fi

export CROSS_COMPILE="$TOOLCHAIN"


#####################
# internal functions
#####################

step2_make_config()
{
	echo -e $COLOR_GREEN"\n2 - make config\n"$COLOR_NEUTRAL
	echo

	mkdir -p out
	MAKESTRING="O=out ARCH=arm oldconfig"
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

	make -j$NUM_CPUS O=out CONFIG_NO_ERROR_ON_MISMATCH=y 2>&1 |tee ../compile.log

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

	rm ../dist/$FILENAME.zip &>/dev/null
	zip -r9 ../dist/$FILENAME.zip *

if [[ "$(ps -o comm= -p $PPID 2>/dev/null)" =~ (bash|zsh|sh) ]]; then
        while true; do
                adb start-server >/dev/null 2>&1
                STATE=$(adb get-state 2>&1)

                if [[ $STATE == "device" ]]; then
                        MODE=$(adb shell getprop sys.boot_completed 2>/dev/null)

                        if [[ $MODE -eq 1 ]]; then
                                echo -e "\a"
                                echo "Device is connected but not in recovery mode."
                                read -p "Press Enter to reboot to recovery mode..."
                                adb reboot recovery
                                echo "Rebooting to recovery mode..."
                                REBOOT=1
                        fi
                elif [[ $STATE == "recovery" ]]; then
                        echo "Device is in recovery mode."
                        break
                else
                        echo "No devices or emulators found. Retrying in 5 seconds..."
                        if [[ $REBOOT -ne 1 ]]; then
                                echo -e "\a"
                        fi
                        sleep 5
                fi
        done

        adb push ../dist/$FILENAME.zip /external_sd/
fi
}


################
# main function
################

if [ "$#" -eq 0 ]; then
    echo "Error: bbuild-anykernel.sh must be called with at least one argument." >&2
    echo "Try any of these: klte klteduos kltedv kltekor kltechn kltekdi klteactive kltespr" >&2
    exit 1
fi

step2_make_config
step3_compile
step4_prepare_anykernel
step5_create_anykernel_zip

echo "Done. Check 'dist' folder"
