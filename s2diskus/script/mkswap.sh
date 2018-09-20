adb root
adb wait-for-device
adb shell mkswap /dev/block/by-name/swap
adb shell swapon /dev/block/by-name/swap

