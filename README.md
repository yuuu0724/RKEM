# RK3588 集成检测系统

本项目用于 RK3588 板端的芯片视觉检测和员工疲劳管理，集成 OCR 字符识别、缺陷检测、疲劳检测和 Qt HMI 界面。

## 功能

- 芯片字符识别：使用 `/dev/video21` 识别芯片型号。
- 芯片缺陷检测：识别 `划痕`、`引脚损坏` 等缺陷。
- 员工疲劳检测：使用 `/dev/video23` 检测闭眼、张嘴等疲劳状态。
- Qt HMI：显示摄像头预览、槽位检测结果、报警日志和员工管理入口。

## 编译

```bash
cd cpp
./build-linux.sh -t rk3588
```

编译完成后，程序和模型会安装到：

```bash
cpp/install/rk3588_linux
```

## 运行

进入安装目录：

```bash
cd cpp/install/rk3588_linux
```

启动 HMI：

```bash
./integrated_inspection_hmi
```

启动主检测程序：

```bash
./main_process --chip-camera /dev/video21 --fatigue-camera /dev/video23
```

无显示环境下可关闭 OpenCV 窗口：

```bash
./main_process --no-window
```

## 日志

日志位于运行目录下的 `logs/`：

- `hmi_alarm.log`：HMI 报警日志
- `ocr_results.log`：OCR 结果
- `defect_results.log`：缺陷结果
- `fatigue_results.log`：疲劳结果
- `debug.log`：调试日志

## 注意事项

- 需要在 RK3588 板端运行，并确保 `/dev/rknpu` 可访问。
- 默认芯片检测摄像头为 `/dev/video21`，疲劳检测摄像头为 `/dev/video23`。
- 摄像头不要被其他进程占用。
- 模型和标签文件位于 `model/ocr/`、`model/defect/`、`model/fatigue/`。
