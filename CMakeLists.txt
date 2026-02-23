cmake_minimum_required(VERSION 3.16)
project(tv_tuner_gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

add_executable(tv_tuner_gui
    src/main.cpp
    src/MainWindow.cpp
    include/MainWindow.h
)

target_include_directories(tv_tuner_gui PRIVATE include)
target_link_libraries(tv_tuner_gui PRIVATE Qt6::Widgets)
target_link_libraries(tv_tuner_gui PRIVATE Qt6::Multimedia Qt6::MultimediaWidgets)
