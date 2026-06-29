cmake_minimum_required(VERSION 3.16)

project(SignalBridgePlugin LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

set(TS_FILES SignalBridgePlugin_en_US.ts)

add_library(SignalBridgePlugin SHARED
  SignalBridgePlugin_global.h
  SignalBridgePlugin.cpp
  SignalBridgePlugin.h
  ${TS_FILES}
)

target_link_libraries(SignalBridgePlugin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

target_compile_definitions(SignalBridgePlugin PRIVATE SIGNALBRIDGEPLUGIN_LIBRARY)

if(COMMAND qt_create_translation)
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()
