<div align="center">

# 📷 QR Studio

### Real-Time QR Code Scanner Built with Qt6 & OpenCV

**A cross-platform desktop QR code scanning application with live camera feed, image import, and AI-powered detection visualization**

[![Qt](https://img.shields.io/badge/Qt-6.9-41CD52?style=flat-square&logo=qt)](https://www.qt.io/)
[![OpenCV](https://img.shields.io/badge/OpenCV-4.9-5C3EE8?style=flat-square&logo=opencv)](https://opencv.org/)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-MIT-blue?style=flat-square)](LICENSE)

---

</div>

## Overview

**QR Studio** is a modern desktop QR code scanner built with Qt 6 and OpenCV. It supports both real-time camera scanning and static image import, with real-time detection visualization showing bounding boxes and perspective-corrected quads.

### Key Features

- **Live Camera Scanning** — Real-time QR code detection from webcam feed
- **Image Import** — Scan QR codes from PNG/JPG files via file dialog
- **Detection Visualization** — Live overlay showing bounding boxes and perspective quads
- **Pipeline Info** — Real-time decode pipeline diagnostics
- **Compact Layout** — Responsive UI adapts to window size (< 760px triggers compact mode)
- **Async Processing** — Non-blocking decode via `QtConcurrent`
- **Multi-format Output** — Display decoded text with copy-to-clipboard

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    QML UI Layer                      │
│                                                     │
│  ┌───────────┐  ┌────────────┐  ┌───────────────┐  │
│  │  Camera   │  │  Image     │  │  Result       │  │
│  │  Preview  │  │  Picker    │  │  Display      │  │
│  │  + ROI    │  │  + File    │  │  + Copy       │  │
│  │  Overlay  │  │  Dialog    │  │  Button       │  │
│  └─────┬─────┘  └─────┬──────┘  └───────┬───────┘  │
│        │               │                 │           │
├────────┴───────────────┴─────────────────┴───────────┤
│              QRProcessor (C++ Backend)               │
│                                                     │
│  ┌──────────────┐  ┌────────────┐  ┌─────────────┐ │
│  │  OpenCV      │  │  quirc     │  │  Qt         │ │
│  │  Image Proc  │  │  QR Decode │  │  Concurrent │ │
│  │  - Grayscale │  │  - Identify│  │  - Async    │ │
│  │  - Threshold │  │  - Decode  │  │  - Future   │ │
│  │  - Contour   │  │  - Version │  │             │ │
│  └──────────────┘  └────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────┘
```

## Tech Stack

| Component | Technology |
|-----------|-----------|
| **UI Framework** | Qt 6.9 (QML + Quick) |
| **Language** | C++17 |
| **Image Processing** | OpenCV 4.9 |
| **QR Decoding** | quirc (lightweight C library) |
| **Async** | QtConcurrent |
| **Build** | CMake 3.22+ |
| **Camera** | Qt Multimedia |

## Project Structure

```
QRCodeProject/
├── CMakeLists.txt        # Build configuration
├── main.cpp              # App entry, QML registration
├── main.qml              # Full UI (camera, image, results)
├── QRProcessor.h         # C++ processor interface
├── QRProcessor.cpp       # OpenCV + quirc implementation
└── 3rdparty/
    └── quirc/            # QR decoding library
```

## Build

### Prerequisites

- Qt 6.9+ (MSVC 2022 kit recommended)
- OpenCV 4.9+
- CMake 3.22+
- quirc library ([source](https://github.com/nickoala/quirc))

### Steps

```bash
# Clone
git clone https://github.com/Dapper-YF/QRCodeProject.git
cd QRCodeProject

# Configure (update paths in CMakeLists.txt)
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Release

# Run
./build/Release/QRCodeProject.exe
```

### Configuration

Edit `CMakeLists.txt` to set your local paths:

```cmake
set(CMAKE_PREFIX_PATH "YOUR_QT_PATH")
set(OpenCV_DIR "YOUR_OPENCV_PATH")
# quirc path in add_library()
```

## How It Works

1. **Camera Mode** — Captures video frames → converts to `cv::Mat` → grayscale + threshold → quirc identification → decode → emit result
2. **Image Mode** — Loads file via `FileDialog` → same processing pipeline
3. **Detection Overlay** — Detected quads are projected back to QML coordinates and drawn as polygons
4. **Async Decode** — Heavy processing runs on `QtConcurrent::run()` to keep UI responsive

---

<div align="center">

---

</div>

<div align="center">

# 📷 QR Studio

### 基于 Qt6 和 OpenCV 的实时二维码扫描器

**跨平台桌面二维码扫描应用，支持实时摄像头、图片导入和检测可视化**

---

</div>

## 项目概述

**QR Studio** 是一个现代化的桌面二维码扫描器，使用 Qt 6 和 OpenCV 构建。支持实时摄像头扫描和静态图片导入，并实时显示检测可视化（边界框和透视矫正四边形）。

### 核心特色

- **实时摄像头扫描** — 从摄像头画面实时检测二维码
- **图片导入** — 通过文件对话框从 PNG/JPG 文件扫描二维码
- **检测可视化** — 实时叠加显示边界框和透视四边形
- **流水线信息** — 实时解码流水线诊断
- **紧凑布局** — 响应式 UI，窗口 < 760px 自动切换紧凑模式
- **异步处理** — 通过 `QtConcurrent` 实现非阻塞解码
- **多格式输出** — 显示解码文本，支持一键复制

## 技术栈

| 组件 | 技术 |
|------|------|
| **UI 框架** | Qt 6.9 (QML + Quick) |
| **语言** | C++17 |
| **图像处理** | OpenCV 4.9 |
| **二维码解码** | quirc（轻量级 C 库） |
| **异步** | QtConcurrent |
| **构建** | CMake 3.22+ |
| **摄像头** | Qt Multimedia |

## 构建说明

### 环境要求

- Qt 6.9+（推荐 MSVC 2022 套件）
- OpenCV 4.9+
- CMake 3.22+
- quirc 库（[源码](https://github.com/nickoala/quirc)）

### 构建步骤

```bash
# 克隆
git clone https://github.com/Dapper-YF/QRCodeProject.git
cd QRCodeProject

# 配置（在 CMakeLists.txt 中更新路径）
cmake -B build -G "Visual Studio 17 2022"

# 构建
cmake --build build --config Release

# 运行
./build/Release/QRCodeProject.exe
```

### 配置说明

在 `CMakeLists.txt` 中设置你的本地路径：

```cmake
set(CMAKE_PREFIX_PATH "YOUR_QT_PATH")
set(OpenCV_DIR "YOUR_OPENCV_PATH")
# quirc 路径在 add_library() 中
```

## 工作原理

1. **摄像头模式** — 捕获视频帧 → 转换为 `cv::Mat` → 灰度 + 二值化 → quirc 识别 → 解码 → 发送结果
2. **图片模式** — 通过 `FileDialog` 加载文件 → 相同处理流水线
3. **检测叠加** — 检测到的四边形投影回 QML 坐标并绘制为多边形
4. **异步解码** — 重处理在 `QtConcurrent::run()` 上运行，保持 UI 响应

---

<div align="center">

**Built with ❤️ for QR code enthusiasts**

**为二维码爱好者而生**

</div>
