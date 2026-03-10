cmake_minimum_required(VERSION 3.16)
project(tv_tuner_gui VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)

add_executable(tv_tuner_gui
    src/main.cpp
    src/DisplayTheme.cpp
    src/MainWindow.cpp
    src/TvGuideDialog.cpp
    include/DisplayTheme.h
    include/MainWindow.h
    include/TvGuideDialog.h
    resources.qrc
)

target_include_directories(tv_tuner_gui PRIVATE include)
target_compile_definitions(tv_tuner_gui PRIVATE
    TV_TUNER_GUI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TV_TUNER_GUI_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(tv_tuner_gui PRIVATE Qt6::Widgets)
target_link_libraries(tv_tuner_gui PRIVATE Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network)
