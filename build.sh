export ARCH=arm64
export CROSS_COMPILE=~/android/kernel/toolchain/google-64-4.9/bin/aarch64-linux-android-
make mrproper
make lineage_p1a42_defconfig
make -j10
