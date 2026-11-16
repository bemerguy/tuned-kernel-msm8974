### AnyKernel3 Ramdisk Mod Script
## osm0sis @ xda-developers

### AnyKernel setup
# global properties
properties() { '
kernel.string=TUNED by fbs @ xda-developers
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=kltexx
device.name2=kltelra
device.name3=kltetmo
device.name4=kltecan
device.name5=klteatt
device.name6=klteub
device.name7=klteacg
device.name8=klte
device.name9=kltekor
device.name10=klteskt
device.name11=kltektt
device.name12=kltekdi
device.name13=kltedv
device.name14=kltespr
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; } # end properties


### AnyKernel install
## boot files attributes
boot_attributes() {
set_perm_recursive 0 0 755 644 $RAMDISK/*;
set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
} # end attributes

# boot shell variables
BLOCK=/dev/block/platform/msm_sdcc.1/by-name/boot;
IS_SLOT_DEVICE=0;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

# import functions/variables and setup patching - see for reference (DO NOT REMOVE)
. tools/ak3-core.sh;

# boot install
# The installed kernel is a zImage, which magiskboot treats as "zImage +
# compressed payload": on unpack it guesses the payload format by searching
# for compression magics (a false match aborts with "Splitting image
# failed"), and on repack it would recompress our new zImage inside the OLD
# kernel's decompressor (or silently keep the old kernel if it doesn't fit).
# So unpack a copy of the partition with the zImage magic (0x24) cleared:
# magiskboot then sees a raw kernel and writes our zImage as-is.
REALBLOCK=$BLOCK;
dd if=$REALBLOCK of=$AKHOME/boot-src.img bs=1048576 || abort "Dumping image failed. Aborting...";
pagesize=$(od -An -tu4 -j36 -N4 $AKHOME/boot-src.img | tr -d ' ');
if [ "$(od -An -tx1 -j$((pagesize + 36)) -N4 $AKHOME/boot-src.img | tr -d ' \n')" = "18286f01" ]; then
  printf '\0\0\0\0' | dd of=$AKHOME/boot-src.img bs=1 seek=$((pagesize + 36)) conv=notrunc 2>/dev/null;
fi;
BLOCK=$AKHOME/boot-src.img;

dump_boot; # use split_boot to skip ramdisk unpack, e.g. for devices with init_boot ramdisk

# CPU overclock opt-out.  Some S5s can't run above the stock 2457.6 MHz, so
# give the user 5 seconds to press Volume UP; that adds cpu_oc=0 to the
# kernel command line, which drops the overclocked steps.  Read the raw
# input events (struct input_event on 32-bit ARM is 16 bytes; EV_KEY=1,
# KEY_VOLUMEUP=115, value 1 = press) so this doesn't depend on getevent.
volup_pressed() {
  local ev pid found=1;
  rm -rf $AKHOME/ev; mkdir -p $AKHOME/ev;
  for ev in /dev/input/event*; do
    cat $ev > $AKHOME/ev/${ev##*/} 2>/dev/null &
  done;
  for i in $(seq 1 50); do
    usleep 100000;
    if cat $AKHOME/ev/* 2>/dev/null | od -An -v -tx1 -w16 | grep -q " 01 00 73 00 01 00 00 00$"; then
      found=0;
      break;
    fi;
  done;
  for pid in $(jobs -p); do kill $pid 2>/dev/null; done;
  wait 2>/dev/null;
  rm -rf $AKHOME/ev;
  return $found;
}

ui_print " " "*** Press Volume UP within 5 seconds to DISABLE CPU overclocking ***" "    (max 2457.6 MHz instead of 2764.8 MHz)" " ";
if volup_pressed; then
  ui_print "- CPU overclocking DISABLED (cpu_oc=0)";
  patch_cmdline "cpu_oc=" "cpu_oc=0";
else
  ui_print "- CPU overclocking enabled";
  patch_cmdline "cpu_oc=" "";
fi;

BLOCK=$REALBLOCK;
write_boot; # use flash_boot to skip ramdisk repack, e.g. for devices with init_boot ramdisk
## end boot install


## init_boot files attributes
#init_boot_attributes() {
#set_perm_recursive 0 0 755 644 $RAMDISK/*;
#set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
#} # end attributes

# init_boot shell variables
#BLOCK=init_boot;
#IS_SLOT_DEVICE=1;
#RAMDISK_COMPRESSION=auto;
#PATCH_VBMETA_FLAG=auto;

# reset for init_boot patching
#reset_ak;

# init_boot install
#dump_boot; # unpack ramdisk since it is the new first stage init ramdisk where overlay.d must go

#write_boot;
## end init_boot install


## vendor_kernel_boot shell variables
#BLOCK=vendor_kernel_boot;
#IS_SLOT_DEVICE=1;
#RAMDISK_COMPRESSION=auto;
#PATCH_VBMETA_FLAG=auto;

# reset for vendor_kernel_boot patching
#reset_ak;

# vendor_kernel_boot install
#split_boot; # skip unpack/repack ramdisk, e.g. for dtb on devices with hdr v4 and vendor_kernel_boot

#flash_boot;
## end vendor_kernel_boot install


## vendor_boot files attributes
#vendor_boot_attributes() {
#set_perm_recursive 0 0 755 644 $RAMDISK/*;
#set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
#} # end attributes

# vendor_boot shell variables
#BLOCK=vendor_boot;
#IS_SLOT_DEVICE=1;
#RAMDISK_COMPRESSION=auto;
#PATCH_VBMETA_FLAG=auto;

# reset for vendor_boot patching
#reset_ak;

# vendor_boot install
#dump_boot; # use split_boot to skip ramdisk unpack, e.g. for dtb on devices with hdr v4 but no vendor_kernel_boot

#write_boot; # use flash_boot to skip ramdisk repack, e.g. for dtb on devices with hdr v4 but no vendor_kernel_boot
## end vendor_boot install

