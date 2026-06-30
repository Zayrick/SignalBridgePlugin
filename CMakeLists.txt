cmake_minimum_required(VERSION 3.16)

project(SignalBridgePlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party libraries as static libraries" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "Build hidapi hidtest utility" FORCE)
set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "Install hidapi targets" FORCE)
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "Build hidapi tests" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "Build QuickJS examples" FORCE)
set(QJS_BUILD_LIBC OFF CACHE BOOL "Build QuickJS libc support" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "Install QuickJS targets" FORCE)

set(_SIGNALBRIDGE_AUTOUIC ${CMAKE_AUTOUIC})
set(_SIGNALBRIDGE_AUTOMOC ${CMAKE_AUTOMOC})
set(_SIGNALBRIDGE_AUTORCC ${CMAKE_AUTORCC})
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
add_subdirectory(third_party/hidapi EXCLUDE_FROM_ALL)
add_subdirectory(third_party/quickjs EXCLUDE_FROM_ALL)
set(CMAKE_AUTOUIC ${_SIGNALBRIDGE_AUTOUIC})
set(CMAKE_AUTOMOC ${_SIGNALBRIDGE_AUTOMOC})
set(CMAKE_AUTORCC ${_SIGNALBRIDGE_AUTORCC})
unset(_SIGNALBRIDGE_AUTOUIC)
unset(_SIGNALBRIDGE_AUTOMOC)
unset(_SIGNALBRIDGE_AUTORCC)

set(TS_FILES SignalBridgePlugin_en_US.ts)

add_library(SignalBridgePlugin SHARED
  OpenRGB/RGBController/RGBController.cpp
  RGBController_SignalBridgeScript.cpp
  RGBController_SignalBridgeScript.h
  SignalBridgeScriptScanner.cpp
  SignalBridgeScriptScanner.h
  SignalBridgeScriptRuntime.cpp
  SignalBridgeScriptRuntime.h
  SignalBridgeRuntimeResources.qrc
  SignalBridgePlugin_global.h
  SignalBridgePlugin.cpp
  SignalBridgePlugin.h
  SignalBridgeHid.cpp
  SignalBridgeHid.h
  ${TS_FILES}
)

target_link_libraries(SignalBridgePlugin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets hidapi::hidapi qjs)

target_include_directories(SignalBridgePlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB/i2c_smbus
)

target_compile_definitions(SignalBridgePlugin PRIVATE
    SIGNALBRIDGEPLUGIN_LIBRARY
    SIGNALBRIDGEPLUGIN_VERSION="${PROJECT_VERSION}"
    SIGNALBRIDGEPLUGIN_COMMIT=""
)

if(COMMAND qt_create_translation)
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()
