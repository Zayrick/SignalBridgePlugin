# Agent Notes

`jom` is installed at:

```text
C:\Qt\Tools\QtCreator\bin\jom
```

Use `C:\Qt\Tools\QtCreator\bin\jom\jom.exe` when building CMake directories generated with `NMake Makefiles JOM`.

For the current Visual Studio generated Qt 6 debug build directory, build the plugin target with:

```text
cmake --build build\qt6-debug --config Debug --target SignalBridgePlugin
```

Do not use `jom` for Visual Studio generated build directories.

Current sandbox process startup is known to fail in this workspace with:

```text
windows sandbox: runner error: CreateProcessAsUserW failed: 1312
```

When this happens, do not retry the same sandboxed command repeatedly. Use the managed escalation flow for necessary local read/build/test commands instead.
