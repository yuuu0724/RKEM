#!/bin/bash

set -e

echo "$0 $@"

# ============================================================
# 解析命令行参数
# ============================================================
while getopts ":t:b:md" opt; do
  case $opt in
    t)
      TARGET_SOC=$OPTARG
      ;;
    b)
      BUILD_TYPE=$OPTARG
      ;;
    m)
      ENABLE_ASAN=ON
      export ENABLE_ASAN=TRUE
      ;;
    d)
      ENABLE_DMA32=ON
      export ENABLE_DMA32=TRUE
      ;;
    :)
      echo "Option -$OPTARG requires an argument."
      exit 1
      ;;
    ?)
      echo "Invalid option: -$OPTARG index:$OPTIND"
      ;;
  esac
done

if [ -z ${TARGET_SOC} ] ; then
  echo "$0 -t <target> "
  echo ""
  echo "    -t : target (rk356x/rk3588)"
  echo "    -b : build_type(Debug/Release)"
  echo "    -m : enable address sanitizer, build_type need set to Debug"
  echo "    -d : enable dma32"
  echo "such as: $0 -t rk3588 "
  echo ""
  exit -1
fi

# Debug / Release
if [[ -z ${BUILD_TYPE} ]];then
    BUILD_TYPE=Release
fi

# Address Sanitizer
if [[ -z ${ENABLE_ASAN} ]];then
    ENABLE_ASAN=OFF
fi

# DMA32 支持
if [[ -z ${ENABLE_DMA32} ]];then
    ENABLE_DMA32=OFF
fi

# ============================================================
# 验证目标 SoC
# ============================================================
case ${TARGET_SOC} in
    rk356x)
        ;;
    rk3588)
        ;;
    rk3576)
        TARGET_SOC="rk3576"
        ;;
    rk3566)
        TARGET_SOC="rk356x"
        ;;
    rk3568)
        TARGET_SOC="rk356x"
        ;;
    rk3562)
        TARGET_SOC="rk356x"
        ;;
    *)
        echo "Invalid target: ${TARGET_SOC}"
        echo "Valid target: rk3562,rk3566,rk3568,rk3576,rk3588"
        exit -1
        ;;
esac

# ============================================================
# 设置编译器和路径
# ============================================================
GCC_COMPILER=aarch64-linux-gnu
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )
INSTALL_DIR=${ROOT_PWD}/install/${TARGET_SOC}_linux
BUILD_DIR=${ROOT_PWD}/build/build_${TARGET_SOC}_linux

echo "==================================="
echo "TARGET_SOC=${TARGET_SOC}"
echo "INSTALL_DIR=${INSTALL_DIR}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "ENABLE_ASAN=${ENABLE_ASAN}"
echo "ENABLE_DMA32=${ENABLE_DMA32}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "==================================="

# ============================================================
# 创建构建目录
# ============================================================
if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

if [[ -d "${INSTALL_DIR}" ]]; then
  rm -rf ${INSTALL_DIR}
fi

# ============================================================
# CMake 配置和编译
# ============================================================
cd ${BUILD_DIR}
cmake ../.. \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DENABLE_DMA32=${ENABLE_DMA32} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}

make -j$(nproc)
make install

echo ""
echo "==================================="
echo "Build completed successfully!"
echo "Install directory: ${INSTALL_DIR}"
echo "==================================="
