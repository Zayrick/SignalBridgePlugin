cmake_minimum_required(VERSION 3.16)

project(SignalBridgePlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party libraries as static libraries" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "Build hidapi hidtest utility" FORCE)
set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "Install hidapi targets" FORCE)
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "Build hidapi tests" FORCE)
set(CSERIALPORT_ENABLE_UTF8 ON CACHE BOOL "Use UTF-8 strings from CSerialPort" FORCE)
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
add_subdirectory(third_party/CSerialPort EXCLUDE_FROM_ALL)
set(CMAKE_AUTOUIC ${_SIGNALBRIDGE_AUTOUIC})
set(CMAKE_AUTOMOC ${_SIGNALBRIDGE_AUTOMOC})
set(CMAKE_AUTORCC ${_SIGNALBRIDGE_AUTORCC})
unset(_SIGNALBRIDGE_AUTOUIC)
unset(_SIGNALBRIDGE_AUTOMOC)
unset(_SIGNALBRIDGE_AUTORCC)

file(GLOB SIGNALBRIDGE_SIGNALRGB_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/signalrgb/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/signalrgb/*.h"
)

set(SIGNALBRIDGE_CORE_SOURCES
  OpenRGB/RGBController/RGBController.cpp
  src/config/DeviceConfigStore.cpp
  src/config/DeviceConfigStore.h
  src/domain/ControlParameters.cpp
  src/domain/ControlParameters.h
  src/domain/ColorFrame.h
  src/domain/DeviceRecords.cpp
  src/domain/DeviceRecords.h
  src/domain/PathUtils.cpp
  src/domain/PathUtils.h
  src/domain/ScriptTypes.h
  src/hid/HidBackend.cpp
  src/hid/HidBackend.h
  src/hid/HidTypes.h
  src/openrgb/FrameBuilder.cpp
  src/openrgb/FrameBuilder.h
  src/openrgb/TopologyMapper.cpp
  src/openrgb/TopologyMapper.h
  src/runtime/BuiltinModules.cpp
  src/runtime/BuiltinModules.h
  src/runtime/ModuleLoader.cpp
  src/runtime/ModuleLoader.h
  src/runtime/QuickJsRuntime.cpp
  src/runtime/QuickJsRuntime.h
  src/runtime/QuickJsValue.cpp
  src/runtime/QuickJsValue.h
  src/runtime/RuntimeBindingUtils.cpp
  src/runtime/RuntimeBindingUtils.h
  src/runtime/RuntimeBindings.cpp
  src/runtime/RuntimeBindings.h
  src/runtime/SignalRgbRuntimeFactory.cpp
  src/runtime/SignalRgbRuntimeFactory.h
  src/serial/SerialBackend.cpp
  src/serial/SerialBackend.h
  src/serial/SerialTypes.h
  ${SIGNALBRIDGE_SIGNALRGB_SOURCES}
)

set(SIGNALBRIDGE_COMMON_LIBS Qt${QT_VERSION_MAJOR}::Widgets hidapi::hidapi qjs libcserialport)
set(SIGNALBRIDGE_COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenRGB/i2c_smbus
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CSerialPort/include
)

add_library(SignalBridgePlugin SHARED)

target_sources(SignalBridgePlugin PRIVATE
  include/SignalBridge/SignalBridgePlugin.h
  include/SignalBridge/SignalBridgePlugin_global.h
  src/SignalBridgePlugin.cpp
  src/discovery/ControllerRegistry.cpp
  src/discovery/ControllerRegistry.h
  src/discovery/DiscoveryService.cpp
  src/discovery/DiscoveryService.h
  src/openrgb/EndpointSession.cpp
  src/openrgb/EndpointSession.h
  src/openrgb/SignalBridgeController.cpp
  src/openrgb/SignalBridgeController.h
  src/scanning/ScriptMetadataExtractor.cpp
  src/scanning/ScriptMetadataExtractor.h
  src/scanning/ScriptScanner.cpp
  src/scanning/ScriptScanner.h
  src/ui/DeviceConfigPageFactory.cpp
  src/ui/DeviceConfigPageFactory.h
  src/ui/LogBuffer.cpp
  src/ui/LogBuffer.h
  src/ui/SignalBridgeWidget.cpp
  src/ui/SignalBridgeWidget.h
  ${SIGNALBRIDGE_CORE_SOURCES}
)

target_link_libraries(SignalBridgePlugin PRIVATE ${SIGNALBRIDGE_COMMON_LIBS})

target_include_directories(SignalBridgePlugin PRIVATE ${SIGNALBRIDGE_COMMON_INCLUDES})

target_compile_definitions(SignalBridgePlugin PRIVATE
    SIGNALBRIDGEPLUGIN_LIBRARY
    SIGNALBRIDGEPLUGIN_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(SignalBridgePlugin PRIVATE /FS)
endif()

option(SIGNALBRIDGE_BUILD_TESTS "Build SignalBridge focused core tests" ON)

if(SIGNALBRIDGE_BUILD_TESTS)
    enable_testing()

    add_executable(SignalBridgeCoreTests)
    target_sources(SignalBridgeCoreTests PRIVATE
        tests/SignalBridgeCoreTests.cpp
        ${SIGNALBRIDGE_CORE_SOURCES}
    )
    target_link_libraries(SignalBridgeCoreTests PRIVATE ${SIGNALBRIDGE_COMMON_LIBS})
    if(MSVC)
        target_compile_options(SignalBridgeCoreTests PRIVATE /FS)
    endif()
    target_include_directories(SignalBridgeCoreTests PRIVATE ${SIGNALBRIDGE_COMMON_INCLUDES})
    add_test(
        NAME SignalBridgeCoreTests
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>$<SEMICOLON>$ENV{PATH}"
            $<TARGET_FILE:SignalBridgeCoreTests>
    )
endif()
