cmake_minimum_required(VERSION 3.22)
project(QRCodeProject LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the CMAKE_PREFIX_PATH to help find Qt and other packages
list(PREPEND CMAKE_PREFIX_PATH "YOUR_QT_PATH")

# Use the concrete OpenCV package config to avoid toolset auto-detection issues.
set(OpenCV_DIR "YOUR_OPENCV_PATH")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Vulkan headers are optional for this project; skip probing to keep configure output clean.
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Concurrent Qml Quick QuickControls2)
find_package(OpenCV 4.9.0 REQUIRED)

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
endif()

# --- quirc ---
add_library(quirc STATIC
    "YOUR_QUIRC_PATH/quirc.c"
    "YOUR_QUIRC_PATH/decode.c"
    "YOUR_QUIRC_PATH/identify.c"
    "YOUR_QUIRC_PATH/version_db.c"
)
target_include_directories(quirc PUBLIC "YOUR_QUIRC_PATH")
# --- end quirc ---

qt_add_executable(QRCodeProject
    MANUAL_FINALIZATION
    main.cpp
    QRProcessor.h
    QRProcessor.cpp
)

qt_add_qml_module(QRCodeProject
    URI com.qrcode.app
    VERSION 1.0
    QML_FILES main.qml
)

target_include_directories(QRCodeProject PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(QRCodeProject
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    ${OpenCV_LIBS}
    quirc
)

# Qt for iOS and Android
qt_import_qml_plugins(QRCodeProject)
qt_finalize_executable(QRCodeProject)
