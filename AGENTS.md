# AGENTS.md

This file provides guidance to AI when working with code in this repository.

## Project Overview

SignalBridgePlugin is an OpenRGB plugin that brings support for SignalRGB JavaScript device plugins to OpenRGB. It embeds QuickJS to execute SignalRGB device scripts, translates between the SignalRGB and OpenRGB device models, and manages USB HID device access.

## Build System

CMake with presets for Qt5/Qt6 and Debug/Release configurations.

### Configure
```bash
cmake --preset qt6-debug       # Qt 6.11.1 Debug
cmake --preset qt6-release     # Qt 6.11.1 Release
cmake --preset qt5-debug       # Qt 5.15.2 Debug
cmake --preset qt5-release     # Qt 5.15.2 Release
```

### Build
```bash
cmake --build build/qt6-debug --config Debug --target SignalBridgePlugin
```

For Visual Studio-generated builds, specify `--config Debug` or `--config Release`.

### Test
```bash
ctest --test-dir build/qt6-debug --output-on-failure
```

Or run the test executable directly:
```bash
./build/qt6-debug/Debug/SignalBridgeCoreTests.exe
```

### Alternative: JOM
JOM is installed at `C:\Qt\Tools\QtCreator\bin\jom\jom.exe` and can be used for faster parallel builds with NMake Makefiles JOM generator.

## Architecture

### Plugin Shell and Core Separation
- [SignalBridgePlugin](include/SignalBridge/SignalBridgePlugin.h) implements the OpenRGB plugin interface (OpenRGBPluginInterface)
- [SignalBridgePluginCore](src/SignalBridgePlugin.cpp#L14-L214) contains the actual implementation and is hidden from the plugin API
- Core uses Qt signals to communicate with the plugin shell across thread boundaries

### Discovery Pipeline
Device discovery runs in a background thread and follows this flow:
1. **ScriptScanner** - finds user-provided `.js` files in OpenRGB's configuration directory under `SignalBridge/scripts/` (default Windows path: `%APPDATA%/OpenRGB/SignalBridge/scripts/`)
2. **ScriptMetadataExtractor** - creates minimal QuickJS runtimes to extract script metadata (vendor, product, model)
3. **DiscoveryService** - orchestrates the scan, matches scripts to connected HID devices, creates controllers
4. **ControllerRegistry** - owns SignalBridgeController instances, registers them with OpenRGB's ResourceManager

Each discovery run has a generation counter; stale callbacks from previous runs are ignored.

### JavaScript Runtime
- **SignalRgbRuntimeFactory** - creates QuickJsRuntime instances configured for different purposes (scan, validation, device control)
- **QuickJsRuntime** - wraps QuickJS engine, owns JSContext and JSRuntime
- **RuntimeBindings** - provides SignalRGB API to scripts (`export function Name()`, `device.log()`, `device.read()`, etc.)
- **BuiltinModules** - ES6 module loader for built-in modules
- **ModuleLoader** - loads user scripts as ES modules

Scripts are loaded as ES modules and must export functions like `Name()`, `Initialize()`, `Render()`, `Shutdown()`.

### Controller Lifecycle
- **SignalBridgeController** - subclass of OpenRGB's RGBController, owns a QuickJsRuntime and EndpointSession
- **EndpointSession** - manages HID device handles for primary device and endpoints (extra HID interfaces)
- **TopologyMapper** - translates SignalRGB device topology (channels/LEDs) to OpenRGB zones/LEDs
- **FrameBuilder** - converts OpenRGB color arrays to HID packets via script's `Render()` function

When OpenRGB calls `DeviceUpdateLEDs()`, the controller:
1. Passes LED colors to FrameBuilder
2. FrameBuilder calls the script's `Render()` function
3. Script writes HID packets via `device.write()`
4. HidBackend sends packets to USB devices

### Configuration
- **DeviceConfigStore** - persists device configurations to OpenRGB settings
- Configuration is keyed by `{vid:pid:path}|{script_key}` to support multiple scripts per device
- Properties from script's `DefaultComponentSettings` are stored/restored
- Config changes trigger `ApplyConfiguration()` in controllers

### HID Abstraction
- **HidBackend** - wraps hidapi, provides device enumeration and I/O
- Handles are shared pointers with automatic cleanup
- USB device paths are normalized for matching against script metadata

### Source Structure
- `src/domain/` - core domain types (ControlParameters, DeviceRecords, ScriptTypes, PathUtils)
- `src/runtime/` - QuickJS integration and SignalRGB API bindings
- `src/scanning/` - script discovery and metadata extraction
- `src/discovery/` - device discovery orchestration
- `src/openrgb/` - OpenRGB integration (controllers, topology mapping, frame building)
- `src/hid/` - HID device access
- `src/config/` - configuration persistence
- `src/ui/` - Qt widgets for plugin UI

## Testing

Tests are in [tests/SignalBridgeCoreTests.cpp](tests/SignalBridgeCoreTests.cpp). The test suite focuses on core functionality without OpenRGB dependencies:
- HidBackend device enumeration
- TopologyMapper channel/LED translation
- FrameBuilder color frame building
- DeviceConfigStore persistence
- PathUtils normalization

Tests are built when `SIGNALBRIDGE_BUILD_TESTS=ON` (default). They use Qt Test framework.

## Known Issues

### Sandbox Process Failure
Windows sandbox may fail with `CreateProcessAsUserW failed: 1312`. When this happens, don't retry sandboxed commands repeatedly. Use the managed escalation flow instead.

## Code Conventions

- C++20 standard
- Qt naming conventions for Qt types (PascalCase with prefixes: QWidget, QString)
- Snake_case for private member variables with trailing underscore (`resource_manager_`)
- Use OpenRGB's RGBController API patterns for LED/zone updates
- QuickJS error handling: check `JS_IsException()` after every runtime call
- Lock `mutex_` when accessing controller state from multiple threads
- Use `std::shared_ptr` for HidBackend, raw pointers for Qt parent-child relationships
