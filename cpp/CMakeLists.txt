cmake_minimum_required(VERSION 3.16)

project(integrated_inspection)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
option(BUILD_QT_HMI "Build Qt Widgets HMI" ON)

# Address Sanitizer 支持
if (ENABLE_ASAN)
    message(STATUS "BUILD WITH ADDRESS SANITIZER")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

# 从系统查找 OpenCV
find_package(OpenCV REQUIRED)

if (BUILD_QT_HMI)
    # Qt Widgets HMI，兼容 Qt 5 / Qt 6，不使用 QML
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()

# 复用 chip 工程里的 PP-OCRv5 后处理和图像工具
set(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../chip CACHE PATH "chip demo root")
add_subdirectory(${CHIP_ROOT}/3rdparty ${CMAKE_CURRENT_BINARY_DIR}/chip_3rdparty.out)
add_subdirectory(${CHIP_ROOT}/utils ${CMAKE_CURRENT_BINARY_DIR}/chip_utils.out)

# 第三方库根目录
set(THIRD_PARTY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party CACHE PATH "third party dependency root")

# ============================================================
# RGA 图像处理库
# ============================================================
set(RGA_PATH ${THIRD_PARTY_ROOT}/librga)
set(LIBRGA ${RGA_PATH}/Linux/aarch64/librga.a)
set(RGA_INCLUDES ${RGA_PATH}/include)
install(PROGRAMS ${RGA_PATH}/Linux/aarch64/librga.so DESTINATION lib)

# RGA handle 支持 (仅 rk3588)
if (TARGET_SOC STREQUAL "rk3588")
    add_definitions(-DLIBRGA_IM2D_HANDLE)
endif()

# RGA2 仅支持 4G 以下内存
if (ENABLE_DMA32 STREQUAL "TRUE")
    add_definitions(-DDMA_ALLOC_DMA32)
endif()

# DMA 内存分配器
include_directories(${THIRD_PARTY_ROOT}/allocator/dma)

# ============================================================
# RKNN NPU 运行时库
# ============================================================
if (TARGET_SOC STREQUAL "rk3588" OR TARGET_SOC STREQUAL "rk356x")
    set(RKNN_PATH ${THIRD_PARTY_ROOT}/rknpu2)
    set(LIBRKNNRT ${RKNN_PATH}/Linux/aarch64/librknnrt.so)
    set(LIBRKNNRT_INCLUDES ${RKNN_PATH}/include)
endif()
install(PROGRAMS ${LIBRKNNRT} DESTINATION lib)

# ============================================================
# 头文件包含目录
# ============================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${RGA_INCLUDES}
    ${LIBRKNNRT_INCLUDES}
)

# ============================================================
# 源文件定义
# ============================================================

# 公共模块源文件
set(COMMON_SRCS
    src/logger.cc
    src/thread_pool.cc
    src/ipc_manager.cc
    src/camera.cc
    src/model_engine.cc
    src/serial_port.cc
    src/qt_interface.cc
)

# OCR 引擎源文件
set(OCR_ENGINE_SRCS
    src/ocr_engine.cc
)

# 疲劳检测引擎源文件
set(FATIGUE_ENGINE_SRCS
    src/fatigue_engine.cc
)

# 缺陷检测引擎源文件 (预留)
set(DEFECT_ENGINE_SRCS
    src/defect_engine.cc
)

# ============================================================
# 可执行文件定义
# ============================================================

# 主进程
add_executable(main_process
    src/main.cc
    ${CHIP_ROOT}/cpp/rknpu2/ppocrv5.cc
    ${CHIP_ROOT}/cpp/postprocess.cc
    ${CHIP_ROOT}/cpp/clipper.cc
)

target_link_libraries(main_process
    imageutils
    fileutils
    ${LIBRGA}
    ${OpenCV_LIBS}
    ${LIBRKNNRT}
    dl
    pthread
)

target_include_directories(main_process PRIVATE
    ${CHIP_ROOT}/cpp
    ${CHIP_ROOT}/utils
    ${LIBRKNNRT_INCLUDES}
)

set_source_files_properties(
    ${CHIP_ROOT}/cpp/rknpu2/ppocrv5.cc
    PROPERTIES COMPILE_OPTIONS "-O0"
)

# OCR 子进程
add_executable(process_ocr
    src/process_ocr.cc
    ${COMMON_SRCS}
    ${OCR_ENGINE_SRCS}
)

target_link_libraries(process_ocr
    ${LIBRGA}
    ${OpenCV_LIBS}
    ${LIBRKNNRT}
    dl
    pthread
)

# 疲劳检测子进程
add_executable(process_fatigue
    src/process_fatigue.cc
    ${COMMON_SRCS}
    ${FATIGUE_ENGINE_SRCS}
)

target_link_libraries(process_fatigue
    ${LIBRGA}
    ${OpenCV_LIBS}
    ${LIBRKNNRT}
    dl
    pthread
)

# 缺陷检测子进程 (预留)
add_executable(process_defect
    src/process_defect.cc
    ${COMMON_SRCS}
    ${DEFECT_ENGINE_SRCS}
)

target_link_libraries(process_defect
    ${LIBRGA}
    ${OpenCV_LIBS}
    ${LIBRKNNRT}
    dl
    pthread
)

# 双模型摄像头模拟显示 demo
add_executable(two_model_camera_demo
    src/two_model_camera_demo.cc
)

target_link_libraries(two_model_camera_demo
    ${OpenCV_LIBS}
)

# 纯 OpenCV 摄像头采集服务，隔离模型库对 V4L2 的影响
add_executable(camera_capture_service
    src/camera_capture_service.cc
)

target_link_libraries(camera_capture_service
    ${OpenCV_LIBS}
)

if (BUILD_QT_HMI)
    # 工业视觉检测 HMI，仅展示和交互，真实数据通过 MainWindow 接口注入
    add_executable(integrated_inspection_hmi
        src/hmi_main.cc
        src/hmi_mainwindow.cc
        include/hmi_mainwindow.h
    )

    target_link_libraries(integrated_inspection_hmi
        Qt${QT_VERSION_MAJOR}::Widgets
        ${OpenCV_LIBS}
    )

    target_include_directories(integrated_inspection_hmi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OpenCV_INCLUDE_DIRS}
    )
endif()

# ============================================================
# 安装规则
# ============================================================

# 安装可执行文件
set(INSTALL_TARGETS main_process process_ocr process_fatigue process_defect two_model_camera_demo camera_capture_service)
if (BUILD_QT_HMI)
    list(APPEND INSTALL_TARGETS integrated_inspection_hmi)
endif()
install(TARGETS ${INSTALL_TARGETS} DESTINATION .)

# 安装模型文件
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../model/ocr/dataset.txt
    DESTINATION model/ocr
)

file(GLOB OCR_RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/ocr/*.rknn")
install(FILES ${OCR_RKNN_FILES} DESTINATION model/ocr)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../model/fatigue/dataset.txt
    DESTINATION model/fatigue
)

file(GLOB FATIGUE_RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/fatigue/*.rknn")
install(FILES ${FATIGUE_RKNN_FILES} DESTINATION model/fatigue)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../model/defect/dataset.txt
    DESTINATION model/defect
)

file(GLOB DEFECT_RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/defect/*.rknn")
install(FILES ${DEFECT_RKNN_FILES} DESTINATION model/defect)

# 创建日志目录
install(CODE "file(MAKE_DIRECTORY \${CMAKE_INSTALL_PREFIX}/logs)")
