adb root
adb wait-for-device
adb shell "echo 3 > /proc/sys/vm/drop_caches"
adb shell "/data/local/tmp/resume_from_disk -f /data/local/tmp/suspend.conf"

