# SignalBridge 插件

一个 OpenRGB 插件，为 OpenRGB 带来 SignalRGB JavaScript 设备插件的支持，使得可以通过 SignalRGB 的丰富脚本库控制 RGB 设备。

[![版本](https://img.shields.io/badge/版本-0.1.0-blue.svg)](https://github.com/Zayrick/SignalBridgePlugin)
[![许可证](https://img.shields.io/badge/许可证-GPL--2.0--or--later-green.svg)](LICENSE)

[中文文档](README.zh-CN.md) | [English](README.md)

## 功能特性

- **SignalRGB 脚本兼容性**: 在 OpenRGB 中执行 SignalRGB JavaScript 设备插件
- **内嵌 JavaScript 运行时**: 使用 QuickJS 实现快速、轻量级的脚本执行
- **自动设备发现**: 扫描并匹配 SignalRGB 脚本与已连接的 USB HID 设备
- **无缝集成**: 在 SignalRGB 和 OpenRGB 设备模型之间进行转换
- **配置管理**: 跨会话持久化设备特定的设置
- **实时控制**: 直接 USB HID 通信，实现低延迟的 RGB 更新
- **多线程架构**: 后台设备发现，不阻塞 OpenRGB 用户界面

## 系统要求

### 运行要求
- **OpenRGB**: 0.7 或更高版本（支持插件）
- **操作系统**: Windows 10/11（已测试）；Linux/macOS 尚未测试
- **SignalRGB 脚本**: 将 `.js` 脚本放入 OpenRGB 配置目录下的 `SignalBridge/scripts/`（Windows 默认路径：`%APPDATA%/OpenRGB/SignalBridge/scripts/`）

### 构建要求
- **CMake**: 3.16 或更高版本
- **Qt**: 5.15.2 或 6.11.1
- **编译器**: MSVC 2019/2022 或兼容的 C++17 编译器
- **Git**: 用于子模块依赖

## 从源码构建

### 1. 克隆仓库
```bash
git clone https://github.com/yourusername/SignalBridgePlugin.git
cd SignalBridgePlugin
git submodule update --init --recursive
```

### 2. 使用 CMake 配置
根据您的 Qt 版本和构建类型选择预设：

```bash
# Qt 6 Debug（推荐用于开发）
cmake --preset qt6-debug

# Qt 6 Release（推荐用于生产）
cmake --preset qt6-release

# Qt 5 备选方案
cmake --preset qt5-debug
cmake --preset qt5-release
```

### 3. 构建插件
```bash
# Qt 6 Debug
cmake --build build/qt6-debug --config Debug --target SignalBridgePlugin

# Qt 6 Release
cmake --build build/qt6-release --config Release --target SignalBridgePlugin
```

**备选方案：使用 JOM 快速并行构建**
```bash
# JOM 位于 C:\Qt\Tools\QtCreator\bin\jom\jom.exe
cmake -G "NMake Makefiles JOM" -B build/custom -DCMAKE_BUILD_TYPE=Release
cd build/custom
C:\Qt\Tools\QtCreator\bin\jom\jom.exe
```

### 4. 运行测试（可选）
```bash
# 运行所有测试
ctest --test-dir build/qt6-debug --output-on-failure

# 或直接运行测试可执行文件
./build/qt6-debug/Debug/SignalBridgeCoreTests.exe
```

## 安装

1. **按照上述说明构建插件**
2. **找到输出的 DLL 文件**: `build/qt6-release/Release/SignalBridgePlugin.dll`
3. **复制到 OpenRGB 插件目录**:
   ```
   %APPDATA%/OpenRGB/plugins/SignalBridgePlugin.dll
   ```
4. **重启 OpenRGB**
5. **启用插件**: 设置 → 插件 → 启用"Signal Bridge Plugin"

## 使用方法

### 初始设置
1. **放置 SignalRGB 脚本**：将 `.js` 脚本放入 OpenRGB 配置目录下的 `SignalBridge/scripts/`（Windows 默认路径：`%APPDATA%/OpenRGB/SignalBridge/scripts/`）
2. **启动 OpenRGB** 并导航到 **SignalBridge** 选项卡
3. **点击"扫描设备"** 以发现兼容的设备

### 设备发现
插件将：
- 扫描 OpenRGB 配置目录下 `SignalBridge/scripts/` 中的所有 `.js` 文件
- 提取设备元数据（供应商 ID、产品 ID、型号名称）
- 将脚本与已连接的 USB HID 设备进行匹配
- 为匹配的设备创建 OpenRGB 控制器

### 设备配置
- 兼容的设备会出现在 OpenRGB 的主设备列表中
- 通过标准的 OpenRGB 控制配置 RGB 设置
- 脚本特定的设置可在 SignalBridge 选项卡中访问
- 配置会自动保存和恢复

### 查看日志
SignalBridge 选项卡显示实时脚本执行日志，对调试设备问题很有帮助。

## 架构

### 核心组件

```
SignalBridgePlugin (OpenRGB 接口)
├── SignalBridgePluginCore (实现)
│   ├── DiscoveryService (设备发现)
│   │   ├── ScriptScanner (查找 .js 文件)
│   │   └── ScriptMetadataExtractor (解析脚本元数据)
│   ├── ControllerRegistry (管理控制器)
│   │   └── SignalBridgeController (每设备控制器)
│   │       ├── QuickJsRuntime (JavaScript 执行)
│   │       ├── EndpointSession (HID 设备句柄)
│   │       ├── TopologyMapper (SignalRGB → OpenRGB 转换)
│   │       └── FrameBuilder (颜色帧渲染)
│   ├── DeviceConfigStore (配置持久化)
│   └── SignalBridgeWidget (UI 组件)
```

### 发现管道
1. **ScriptScanner**: 在 OpenRGB 配置目录下的 `SignalBridge/scripts/` 中查找 `.js` 文件
2. **ScriptMetadataExtractor**: 创建最小化的 QuickJS 运行时以提取元数据
3. **DiscoveryService**: 协调扫描，将脚本与 HID 设备进行匹配
4. **ControllerRegistry**: 创建控制器，向 OpenRGB 注册

### JavaScript 运行时
- **SignalRgbRuntimeFactory**: 创建配置好的 QuickJS 运行时实例
- **RuntimeBindings**: 提供 SignalRGB API（`device.log()`、`device.read()`、`device.write()`）
- **BuiltinModules**: 解析 `src/runtime/signalrgb` 中自注册的原生 C++ 模块
- **ModuleLoader**: 将用户脚本和原生内置模块加载为 ES6 模块
- 脚本导出函数：`Name()`、`Initialize()`、`Render()`、`Shutdown()`

### 数据流
```
OpenRGB 颜色更新
    ↓
SignalBridgeController::DeviceUpdateLEDs()
    ↓
FrameBuilder (转换颜色)
    ↓
脚本 Render() 函数 (通过 QuickJS)
    ↓
device.write() 绑定
    ↓
HidBackend (hidapi)
    ↓
USB HID 设备
```

## 开发

### 项目结构
```
SignalBridgePlugin/
├── include/SignalBridge/    # 公共插件头文件
├── src/
│   ├── domain/              # 核心类型 (ControlParameters, DeviceRecords)
│   ├── runtime/             # QuickJS 集成、绑定
│   │   └── signalrgb/       # 自注册的原生 @SignalRGB/* 模块
│   ├── scanning/            # 脚本发现、元数据提取
│   ├── discovery/           # 设备发现协调
│   ├── openrgb/             # OpenRGB 集成（控制器、映射）
│   ├── hid/                 # HID 设备抽象（hidapi 包装器）
│   ├── config/              # 配置持久化
│   └── ui/                  # Qt 小部件
├── tests/                   # 单元测试
├── third_party/             # 依赖项（hidapi、quickjs）
└── OpenRGB/                 # OpenRGB 头文件（子模块）
```

### 代码规范
- **C++17** 标准
- **Qt 命名约定** 用于 Qt 类型（帕斯卡命名法：`QWidget`、`QString`）
- **蛇形命名法** 用于私有成员，带有尾部下划线（`resource_manager_`）
- **QuickJS 错误处理**: 每次运行时调用后检查 `JS_IsException()`
- **线程安全**: 访问共享控制器状态时锁定 `mutex_`
- **内存管理**: HidBackend 使用 `std::shared_ptr`，Qt 父子关系使用原始指针

### 运行测试
```bash
# 所有测试
ctest --test-dir build/qt6-debug --output-on-failure

# 特定测试
./build/qt6-debug/Debug/SignalBridgeCoreTests.exe
```

测试覆盖：
- HID 设备枚举
- 拓扑映射（SignalRGB 通道/LED → OpenRGB 区域/LED）
- 帧构建和颜色转换
- 配置持久化
- 路径规范化

## 已知问题

- **Windows 沙箱**: 可能会失败并显示错误 1312。如果遇到，请使用托管提升。
- **脚本兼容性**: 由于 API 差异，并非所有 SignalRGB 脚本都可能正常工作
- **平台支持**: 目前仅在 Windows 上测试；Linux/macOS 尚未测试

## 贡献

欢迎贡献！请：

1. Fork 仓库
2. 创建功能分支（`git checkout -b feature/amazing-feature`）
3. 提交更改（`git commit -m 'Add amazing feature'`）
4. 推送到分支（`git push origin feature/amazing-feature`）
5. 打开 Pull Request

### 开发指南
- 遵循现有的代码规范
- 为新功能添加测试
- 根据需要更新文档
- 确保在 Qt5 和 Qt6 上都能构建通过

## 许可证

本项目采用 GNU 通用公共许可证 v2.0 许可。详见 [LICENSE](LICENSE)。

## 致谢

- **OpenRGB**: 出色的 RGB 控制平台和插件 API
- **SignalRGB**: 全面的设备脚本库
- **QuickJS**: 轻量级的 JavaScript 引擎
- **hidapi**: 跨平台 HID 设备访问

## 参考资料

- [OpenRGB](https://openrgb.org/)
- [SignalRGB](https://signalrgb.com/)
- [QuickJS](https://bellard.org/quickjs/)
- [hidapi](https://github.com/libusb/hidapi)

## 支持

如有问题、疑问或建议：
- 在 [GitHub](https://github.com/yourusername/SignalBridgePlugin/issues) 上提交 Issue
- 查看 [AGENTS.md](AGENTS.md) 中的现有文档

---

**注意**: 本插件是一个独立项目，与 OpenRGB 或 SignalRGB 没有官方关联。
