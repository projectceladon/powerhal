#mknod -m 0777 /dev/sda b 8 0
#mknod -m 0777 /dev/sda1 b 8 1
chcon u:r:su:s0 /data/image_saver
chcon u:object_r:zero_device:s0 /dev/snapshot
chcon u:object_r:zero_device:s0 /dev/watchdog
chcon u:object_r:zero_device:s0 /dev/ttyS2
chcon u:object_r:zero_device:s0 /dev/block/sda
chcon u:object_r:zero_device:s0 /dev/block/sda1
#mkdir /dev/img
#killall -9 watchdogd
chmod 0777 /data/cansend
#supress heartbeat 30min
/data/cansend slcan0 0000FFFF#01045555555555

#set cm wake up reason 1
#/data/cansend slcan0 0000FFFF#00015555555555
#ignore sus stat x3 time
#/data/cansend slcan0 0000FFFF#05035555555555
#set wakt up button as 0
#/data/cansend slcan0 0000FFFF#06005555555555 
#echo "x" > /sys/power/wake_lock
