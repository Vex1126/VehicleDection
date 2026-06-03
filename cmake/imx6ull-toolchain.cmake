set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(SDK_ROOT "/home/yyy/imx6ull-new-sdk/arm-buildroot-linux-gnueabihf_sdk-buildroot")

set(CMAKE_C_COMPILER   "${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${SDK_ROOT}/bin/arm-buildroot-linux-gnueabihf-g++")

set(CMAKE_SYSROOT "${SDK_ROOT}/arm-buildroot-linux-gnueabihf/sysroot")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
