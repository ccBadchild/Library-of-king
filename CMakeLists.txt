cmake_minimum_required(VERSION 3.16)
project(RemoteDesktopQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT WIN32)
  message(FATAL_ERROR "This project currently supports Windows only.")
endif()

# Qt 5.15 默认安装位置，可按实际环境通过 -DQt5_DIR 覆盖。
if(NOT DEFINED Qt5_DIR)
  set(Qt5_DIR "D:/Qt/5.15/5.15.2/msvc2019_64/lib/cmake/Qt5" CACHE PATH "Qt5 cmake directory")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets Network Gui Core)

add_executable(RemoteControl
  src/app/main.cpp
  src/app/app_logger.cpp
  src/app/mainwindow.cpp
  src/app/video_render_widget.cpp
  src/app/software_render_widget.cpp
  src/app/server_controller.cpp
  src/app/client_controller.cpp
  src/app/capture_worker.cpp
  src/app/tcp_server_worker.cpp
  src/app/tcp_client_worker.cpp
  src/app/mainwindow.h
  src/app/video_render_widget.h
  src/app/software_render_widget.h
  src/app/server_controller.h
  src/app/client_controller.h
  src/app/capture_worker.h
  src/app/tcp_server_worker.h
  src/app/tcp_client_worker.h
  src/app/protocol_qt.h
  src/app/app_logger.h
)

target_link_libraries(RemoteControl PRIVATE
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::Network
)

target_compile_definitions(RemoteControl PRIVATE UNICODE _UNICODE)
if(MSVC)
  target_compile_options(RemoteControl PRIVATE /utf-8)
endif()
