# SignalBridge Plugin

An OpenRGB plugin that runs user-supplied SignalRGB-compatible JavaScript device plugins in OpenRGB.

[![Version](https://img.shields.io/badge/version-0.1.0-blue.svg)](https://github.com/Zayrick/SignalBridgePlugin)
[![License](https://img.shields.io/badge/license-GPL--2.0--or--later-green.svg)](LICENSE)

[中文文档](README.zh-CN.md) | [English](README.md)

## Compatibility Notice

SignalBridge is an independent compatibility project. It was developed from publicly available SignalRGB developer documentation and open-source device plugin material, not by reverse engineering, decompiling, modifying, or extracting source code from the SignalRGB application.

This repository does not include or redistribute SignalRGB application binaries, proprietary source code, paid content, effect libraries, artwork, or device scripts. Users are responsible for providing scripts they are legally permitted to use and for complying with the applicable script licenses and terms.

SignalRGB and OpenRGB names are used only to describe compatibility with their respective ecosystems. This project is not affiliated with, endorsed by, sponsored by, or officially supported by SignalRGB, Whirlwind Virtual Realities Inc., OpenRGB, or their contributors.

## Features

- **SignalRGB-Compatible Script Runtime**: Execute user-supplied JavaScript device plugins within OpenRGB
- **Embedded JavaScript Runtime**: Uses QuickJS for fast, lightweight script execution
- **Automatic Device Discovery**: Scans and matches compatible scripts to connected USB HID devices
- **Model Translation**: Translates between compatible script device definitions and OpenRGB devices
- **Configuration Management**: Persists device-specific settings across sessions
- **Real-time Control**: Direct USB HID communication for low-latency RGB updates
- **Multi-threaded Architecture**: Background discovery without blocking OpenRGB UI

## System Requirements

### Runtime Requirements
- **OpenRGB**: A host matching the selected plugin API (API 4 for the existing presets, API 5 for the `*-api5-*` presets)
- **Operating System**: Windows 10/11 (tested); Linux/macOS have not been tested
- **Device Scripts**: Place compatible `.js` scripts that you are permitted to use in OpenRGB's configuration directory under `SignalBridge/scripts/` (default Windows path: `%APPDATA%/OpenRGB/SignalBridge/scripts/`)

### Build Requirements
- **CMake**: 3.16 or later
- **Qt**: 5.15.2 or 6.11.1
- **Compiler**: MSVC 2019/2022 or compatible C++17 compiler
- **Git**: For submodule dependencies

## Building from Source

### 1. Clone the Repository
```bash
git clone https://github.com/Zayrick/SignalBridgePlugin.git
cd SignalBridgePlugin
git submodule update --init --recursive
```

### 2. Configure with CMake
Choose a preset based on your OpenRGB plugin API, Qt version, and build type. The existing presets build the API 4 plugin against `deps/OpenRGB` (API 4 remains the default):

```bash
# OpenRGB plugin API 4, Qt 6
cmake --preset qt6-debug
cmake --preset qt6-release

# OpenRGB plugin API 4, Qt 5
cmake --preset qt5-debug
cmake --preset qt5-release
```

The API 5 presets build against `deps/OpenRGB_API5`:

```bash
# OpenRGB plugin API 5, Qt 6
cmake --preset qt6-api5-debug
cmake --preset qt6-api5-release

# OpenRGB plugin API 5, Qt 5
cmake --preset qt5-api5-debug
cmake --preset qt5-api5-release
```

For a custom configure command, set `SIGNALBRIDGE_OPENRGB_API_VERSION` to `4` or `5`; omitting it selects API 4.

### 3. Build the Plugin
```bash
# For Qt 6 Debug
cmake --build build/qt6-debug --config Debug --target SignalBridgePlugin

# For Qt 6 Release
cmake --build build/qt6-release --config Release --target SignalBridgePlugin

# For OpenRGB API 5 with Qt 6 Release
cmake --build build/qt6-api5-release --config Release --target SignalBridgePlugin
```

**Alternative: Fast Parallel Builds with JOM**
```bash
# JOM is located at C:\Qt\Tools\QtCreator\bin\jom\jom.exe
cmake -G "NMake Makefiles JOM" -B build/custom -DCMAKE_BUILD_TYPE=Release
cd build/custom
C:\Qt\Tools\QtCreator\bin\jom\jom.exe
```

### 4. Run Tests (Optional)
```bash
# Run all tests
ctest --test-dir build/qt6-debug -C Debug --output-on-failure

# Or run the test executable directly
./build/qt6-debug/Debug/SignalBridgeCoreTests.exe
```

## Installation

1. **Build the plugin** following the instructions above
2. **Locate the output DLL**: `build/qt6-release/Release/SignalBridgePlugin.dll` for API 4, or `build/qt6-api5-release/Release/SignalBridgePlugin.dll` for API 5
3. **Copy to OpenRGB plugins directory**:
   ```
   %APPDATA%/OpenRGB/plugins/SignalBridgePlugin.dll
   ```
4. **Restart OpenRGB**
5. **Enable the plugin**: Settings → Plugins → Enable "Signal Bridge Plugin"

## Usage

### Initial Setup
1. **Place compatible device scripts** that you are permitted to use in OpenRGB's configuration directory under `SignalBridge/scripts/` (default Windows path: `%APPDATA%/OpenRGB/SignalBridge/scripts/`)
2. **Launch OpenRGB** and navigate to the **SignalBridge** tab
3. **Click "Scan Devices"** to discover compatible devices

### Device Discovery
The plugin will:
- Scan all `.js` files in OpenRGB's `SignalBridge/scripts/` configuration directory
- Extract device metadata (vendor ID, product ID, model name)
- Match scripts against connected USB HID devices
- Create OpenRGB controllers for matched devices

### Script Use
- SignalBridge does not download, bundle, or grant rights to third-party device scripts.
- Use only scripts you own, authored yourself, or are otherwise licensed or permitted to use.
- Do not redistribute third-party scripts with this project unless their license or owner allows it.

### Device Configuration
- Compatible devices appear in OpenRGB's main device list
- Configure RGB settings through standard OpenRGB controls
- Script-specific settings are available in the SignalBridge tab
- Configurations are automatically saved and restored

### Viewing Logs
The SignalBridge tab displays real-time script execution logs, useful for debugging device issues.

## Architecture

### Core Components

```
SignalBridgePlugin (OpenRGB Interface)
├── SignalBridgePluginCore (Implementation)
│   ├── DiscoveryService (Device Discovery)
│   │   ├── ScriptScanner (Find .js files)
│   │   └── ScriptMetadataExtractor (Parse script metadata)
│   ├── ControllerRegistry (Manage Controllers)
│   │   └── SignalBridgeController (Per-device controller)
│   │       ├── QuickJsRuntime (JavaScript execution)
│   │       ├── EndpointSession (HID device handles)
│   │       ├── TopologyMapper (SignalRGB → OpenRGB translation)
│   │       └── FrameBuilder (Color frame rendering)
│   ├── DeviceConfigStore (Configuration persistence)
│   └── SignalBridgeWidget (UI components)
```

### Discovery Pipeline
1. **ScriptScanner**: Finds `.js` files in OpenRGB's `SignalBridge/scripts/` configuration directory
2. **ScriptMetadataExtractor**: Creates minimal QuickJS runtimes to extract metadata
3. **DiscoveryService**: Orchestrates scanning, matches scripts to HID devices
4. **ControllerRegistry**: Creates controllers, registers with OpenRGB

### JavaScript Runtime
- **SignalRgbRuntimeFactory**: Creates configured QuickJS runtime instances
- **RuntimeBindings**: Provides SignalRGB API (`device.log()`, `device.read()`, `device.write()`)
- **BuiltinModules**: Resolves native C++ modules self-registered from `src/runtime/signalrgb`
- **ModuleLoader**: Loads user scripts and native built-ins as ES6 modules
- Scripts export functions: `Name()`, `Initialize()`, `Render()`, `Shutdown()`

### Data Flow
```
OpenRGB Color Update
    ↓
SignalBridgeController::DeviceUpdateLEDs()
    ↓
FrameBuilder (convert colors)
    ↓
Script Render() function (via QuickJS)
    ↓
device.write() bindings
    ↓
HidBackend (hidapi)
    ↓
USB HID Device
```

## Development

### Project Structure
```
SignalBridgePlugin/
├── include/SignalBridge/    # Public plugin headers
├── src/
│   ├── domain/              # Core types (ControlParameters, DeviceRecords)
│   ├── runtime/             # QuickJS integration, bindings
│   │   └── signalrgb/       # Self-registering native @SignalRGB/* modules
│   ├── scanning/            # Script discovery, metadata extraction
│   ├── discovery/           # Device discovery orchestration
│   ├── openrgb/             # OpenRGB integration (controllers, mapping)
│   ├── hid/                 # HID device abstraction (hidapi wrapper)
│   ├── config/              # Configuration persistence
│   └── ui/                  # Qt widgets
├── tests/                   # Unit tests
└── deps/                    # Dependency submodules (CSerialPort, hidapi, OpenRGB, quickjs)
```

### Code Conventions
- **C++17** standard
- **Qt naming conventions** for Qt types (PascalCase: `QWidget`, `QString`)
- **Snake_case** for private members with trailing underscore (`resource_manager_`)
- **QuickJS error handling**: Check `JS_IsException()` after every runtime call
- **Thread safety**: Lock `mutex_` when accessing shared controller state
- **Memory management**: `std::shared_ptr` for HidBackend, raw pointers for Qt parent-child

### Running Tests
```bash
# All tests
ctest --test-dir build/qt6-debug --output-on-failure

# Specific test
./build/qt6-debug/Debug/SignalBridgeCoreTests.exe
```

Tests cover:
- HID device enumeration
- Topology mapping (SignalRGB channels/LEDs → OpenRGB zones/LEDs)
- Frame building and color conversion
- Configuration persistence
- Path normalization

## Known Issues

- **Windows Sandbox**: May fail with error 1312. Use managed escalation if encountered.
- **Script Compatibility**: Not all third-party device scripts may work due to API differences
- **Platform Support**: Tested on Windows only; Linux/macOS have not been tested

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines
- Follow existing code conventions
- Add tests for new functionality
- Update documentation as needed
- Ensure builds pass on both Qt5 and Qt6

## License

SignalBridgePlugin source code is licensed under the GNU General Public License v2.0 or later. See [LICENSE](LICENSE) for details.

Third-party components, OpenRGB headers/source files, QuickJS, hidapi, and user-supplied device scripts remain under their own licenses or terms. This project license does not grant rights to redistribute SignalRGB software, SignalRGB paid content, or third-party scripts.

## Acknowledgments

- **OpenRGB**: For the excellent RGB control platform and plugin API
- **SignalRGB**: For the public developer documentation and open-source device plugin ecosystem
- **QuickJS**: For the lightweight JavaScript engine
- **hidapi**: For cross-platform HID device access

## References

- [OpenRGB](https://openrgb.org/)
- [SignalRGB](https://signalrgb.com/)
- [QuickJS](https://bellard.org/quickjs/)
- [hidapi](https://github.com/libusb/hidapi)

## Support

For issues, questions, or suggestions:
- Open an issue on [GitHub](https://github.com/Zayrick/SignalBridgePlugin/issues)
- Check existing documentation in [AGENTS.md](AGENTS.md)

---

**Note**: SignalBridgePlugin is an independent, unofficial compatibility project. SignalRGB and OpenRGB are trademarks or project names of their respective owners.
