# 集成检测系统

基于 RK3588 NPU 的多功能检测系统，集成 OCR 文字识别、疲劳检测和芯片缺陷检测。

## 功能特性

- **OCR 文字识别** - NPU Core 0，支持中文/英文识别
- **疲劳检测** - NPU Core 1，检测眼睛/嘴巴状态
- **缺陷检测** - NPU Core 2，预留接口
- **串口通信** - 预留串口接口
- **QT 界面** - 预留 QT 接口

## 工程结构

```
integrated-inspection/
├── model/                      # 模型文件
│   ├── ocr/                    # OCR 模型
│   ├── fatigue/                # 疲劳检测模型
│   └── defect/                 # 缺陷检测模型(预留)
├── cpp/                        # C++ 源码
│   ├── include/                # 头文件
│   ├── src/                    # 源文件
│   ├── CMakeLists.txt          # 构建配置
│   └── build-linux.sh          # 编译脚本
├── third_party/                # 第三方库
├── logs/                       # 日志输出
└── README.md
```

## 编译

```bash
cd cpp
./build-linux.sh -t rk3588
```

编译选项：
- `-t <target>` - 目标芯片 (rk3588/rk356x)
- `-b <type>` - 构建类型 (Debug/Release)
- `-m` - 启用 Address Sanitizer
- `-d` - 启用 DMA32

## 运行

当前 `main_process` 已整合为单进程多线程程序：

- `/dev/video21`：同一帧同时送入 OCR 模型和 defect 模型，判断芯片型号、划痕、引脚损坏。
- `/dev/video23`：送入 fatigue 模型，判断人员疲劳。
- 输入帧统一预处理为 `640x640` 灰度图；模型需要 3 通道时由灰度图转换为 RGB 输入。
- RK3588 NPU core 分配：OCR 使用 core0，fatigue 使用 core1，defect 使用 core2。
- 终端每 2 秒打印一次芯片良品/次品状态和疲劳状态。

```bash
cd install/rk3588_linux

# 运行整合主程序；启动后按提示输入待检测芯片型号
./main_process

# 无桌面或 SSH 环境下禁用 OpenCV 窗口，只保留终端输出
./main_process --no-window

# 指定摄像头
./main_process --chip-camera /dev/video21 --fatigue-camera /dev/video23
```

运行后输入型号，例如：

```text
请输入待检测芯片型号: STM32F103C8T6
```

终端输出示例：

```text
[CHIP] 良品 expected=STM32F103C8T6 ocr=STM32F103C8T6 defect=0
[FATIGUE] 未疲劳 reason=正常
```

当型号不匹配、检测到 `scratch`、`pin_damage` 时，芯片判定为次品，并打印原因。
当 fatigue 模型检测到 `close_eye` 或 `open_mouth` 时，打印疲劳状态。

历史单独子进程仍保留用于调试：

# 单独运行 OCR
./process_ocr --camera /dev/video21

# 单独运行疲劳检测
./process_fatigue --camera /dev/video23
```

参数说明：
- `--camera <device>` - 所有进程共用的摄像头设备或 RTSP URL
- `--ocr-camera <device>` - OCR 摄像头设备，默认 `/dev/video21`
- `--fatigue-camera <device>` - 疲劳检测摄像头设备，默认 `/dev/video23`
- `--defect-camera <device>` - 缺陷检测摄像头设备，默认 `/dev/video0`
- `--no-ocr` - 禁用 OCR
- `--no-fatigue` - 禁用疲劳检测
- `--enable-defect` - 启用缺陷检测
- `--no-popup` - 禁用桌面弹窗通知
- `--no-window` - 禁用 OpenCV 摄像头窗口
- `--show-window` - 启用 OpenCV 摄像头窗口

注意事项：

- 必须在 RK3588 板端运行，并确保 `rknpu` 驱动已加载，`/dev/rknpu` 可访问。
- 确认摄像头节点存在：`ls -l /dev/video21 /dev/video23`。
- 如果窗口初始化失败或通过 SSH 运行，使用 `--no-window`。
- `video21` 和 `video23` 不要被其他进程占用；必要时先停止旧的 `process_ocr`、`process_fatigue`、`process_defect`。
- 当前 defect 类别来自 `model/defect/dataset.txt`：`pin_damage`、`scratch`。
- 当前 fatigue 类别来自 `model/fatigue/dataset.txt`：`close_eye`、`open_eye`、`open_mouth`。

## 进程架构

```
主进程 (main_process)
├── OCR 子进程 (process_ocr) - NPU Core 0
├── 疲劳检测子进程 (process_fatigue) - NPU Core 1
└── 缺陷检测子进程 (process_defect) - NPU Core 2
```

进程间通过 Unix Domain Socket 通信。

## 日志

日志文件位于 `logs/` 目录：
- `ocr_results.log` - OCR 识别结果
- `fatigue_results.log` - 疲劳检测结果
- `defect_results.log` - 缺陷检测结果
- `debug.log` - 调试日志

## QT 接口

系统预留 QT 界面接口，详见 `include/qt_interface.h`。

## 串口接口

系统预留串口通信接口，详见 `include/serial_port.h`。
