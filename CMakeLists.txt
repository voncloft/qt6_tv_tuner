cmake_minimum_required(VERSION 3.16)
project(tv_tuner_gui VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

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

set(TV_TUNER_GUI_EXEC "\"${CMAKE_CURRENT_BINARY_DIR}/tv_tuner_gui\"")
set(TV_TUNER_GUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/tv-icon.svg")
configure_file(
    assets/tv_tuner_gui.desktop.in
    "${CMAKE_CURRENT_BINARY_DIR}/tv_tuner_gui.desktop"
    @ONLY
)

set(TV_TUNER_GUI_EXEC "tv_tuner_gui")
set(TV_TUNER_GUI_ICON "tv_tuner_gui")
configure_file(
    assets/tv_tuner_gui.desktop.in
    "${CMAKE_CURRENT_BINARY_DIR}/tv_tuner_gui-installed.desktop"
    @ONLY
)

install(TARGETS tv_tuner_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/tv_tuner_gui-installed.desktop"
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    RENAME tv_tuner_gui.desktop
)
install(
    FILES assets/tv-icon.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    RENAME tv_tuner_gui.svg
)
