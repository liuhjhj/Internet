cmake_minimum_required(VERSION 3.5)

project(Server LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)
link_libraries(ws2_32)
add_executable(Server
  main.cpp
  widget.cpp
  widget.h
  widget.ui
  server.cpp
  server.h
  thread.cpp
  thread.h
)

target_link_libraries(Server PRIVATE Qt5::Widgets)
