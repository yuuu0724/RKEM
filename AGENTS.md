# Repository Guidelines

## Project Structure & Module Organization

This repository is the RK3588 integrated inspection system. C++ sources live in `cpp/src/`, public headers in `cpp/include/`, and build configuration in `cpp/CMakeLists.txt`. Runtime RKNN assets are grouped under `model/ocr/`, `model/fatigue/`, and `model/defect/`. Third-party Rockchip libraries and allocators are under `third_party/`. Project notes and execution plans belong in `docs/`; runtime logs are written to `logs/` or `cpp/install/.../logs/`.

## Build, Test, and Development Commands

Build and install all target binaries for RK3588:

```bash
cd cpp
./build-linux.sh -t rk3588
```

Do not build or modify a standalone `integrated_inspection_hmi` target. The field UI, detection workflow, serial communication, and upload logic must be implemented in `main_process`; treat `main_process` as the only integrated Qt HMI entry point.

Build only the integrated Qt HMI entry from an existing build tree:

```bash
cmake --build build/build_rk3588_linux --target main_process -j2
```

Run the integrated detector on the target board:

```bash
cd cpp/install/rk3588_linux
./main_process --chip-camera /dev/video21 --fatigue-camera /dev/video23
```

Run the integrated Qt HMI:

```bash
./main_process
```

## Coding Style & Naming Conventions

Use C++17. Follow existing lowercase file names with `.cc` and `.h`, such as `ocr_engine.cc` and `ipc_manager.h`. Use 4-space indentation. Keep user-facing HMI text in Chinese. Prefer explicit Qt signal/slot names such as `updateKpiData(...)` and `startInspectionRequested(...)`. Avoid broad refactors and generated-file churn.

## Testing Guidelines

There is no centralized automated test suite. Validate by building the target you changed and running it on RK3588 hardware. For camera paths, confirm `/dev/video21` and `/dev/video23` exist and are not occupied. For UI startup checks without a display, run:

```bash
timeout 3 ./main_process -platform offscreen
```

## Commit & Pull Request Guidelines

Use concise imperative commit messages, for example `Add Qt HMI template controls`. Pull requests should list affected modules, build commands run, hardware used, camera/model assumptions, and screenshots for UI changes. Link related issues or task notes when available.

## Security & Configuration Tips

Do not commit build trees, install outputs, logs, or temporary device files. Keep model label changes synchronized with the corresponding `model/*/dataset.txt` and README notes.
