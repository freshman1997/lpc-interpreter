# LPC Interpreter 使用文档

## 构建

```powershell
cmake --build build --config Release
```

只构建编译器和 VM：

```powershell
cmake --build build --config Release --target lpc_compiler lpc_vm
```

## 编译 LPC 文件

```powershell
.\build\compiler\lpc_compiler.exe .\test_array.lpc --workspace-root . --out-root .\bin
```

输出文件会写入 `bin` 目录，扩展名为 `.nb`。

## 运行

```powershell
.\build\vm\lpc_vm.exe run test_array --bytecode-root .\bin
```

带 profile：

```powershell
.\build\vm\lpc_vm.exe run test_array --bytecode-root .\bin --profile
```

Release 运行模式会关闭每条指令前的调试/安全检查，适合线上或 benchmark：

```powershell
.\build\vm\lpc_vm.exe run test_array --bytecode-root .\bin --release
```

默认不加 `--release` 时会启用这些检查，方便调试和定位字节码问题。`debug` 命令和 DAP 调试路径会保留调试检查。

## 测试

```powershell
.\run_lpc_tests.ps1
```

## benchmark

```powershell
.\scripts\compare_runtime_benchmarks.ps1 -SkipBuild -Iterations 30 -VmRelease -Out log\runtime_compare_latest.json
```

## VS Code 调试

1. 安装 `vscode_plugin` 打包出的 VSIX。
2. 打开项目目录。
3. 打开 `.lpc` 文件。
4. 在行号左侧设置断点。
5. 执行命令 `LPC: Debug Current File`。
6. 使用 F10 单步，Variables 查看变量，Debug Console 查看输出。

如果修改了插件源码，需要重新打包安装：

```powershell
.\scripts\package_vscode_plugin.ps1
```

## 业务代码建议

- 把复杂初始化放在 `main()` 或明确的初始化函数中，不要依赖复杂全局表达式。
- 当前适合写同步业务逻辑、规则计算、数据结构操作、热更新脚本验证。
- 网络连接、定时器、异步任务、数据库、日志、权限隔离等应由 C++/宿主服务器提供，再通过 efun 或宿主 API 暴露给 LPC。
