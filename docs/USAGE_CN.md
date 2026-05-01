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

也可以直接指定目录，编译器会递归遍历所有 `.lpc` 文件，并按目录内的相对路径输出：

```powershell
.\build\compiler\lpc_compiler.exe .\mudlib --out-root .\bin
```

例如 `mudlib\zone\npc\mob.lpc` 会生成 `bin\zone\npc\mob.nb`。如果需要以项目根目录作为相对根，可以显式指定：

```powershell
.\build\compiler\lpc_compiler.exe .\mudlib --workspace-root . --out-root .\bin
```

这样会生成 `bin\mudlib\zone\npc\mob.nb`。

## 运行

```powershell
.\build\vm\lpc_vm.exe run test_array --bytecode-root .\bin
```

运行目录编译产物时，入口模块和入口函数都由命令行指定，不再默认读取 `bin\entry.txt`：

```powershell
.\build\vm\lpc_vm.exe run --bytecode-root .\bin --module zone/npc/mob --function main
```

第二个位置参数也可以作为入口函数：

```powershell
.\build\vm\lpc_vm.exe run zone/npc/mob main --bytecode-root .\bin
```

VM 会先执行该模块的全局初始化 init code，然后调用指定函数。`--function` 不传时默认调用 `main`。

运行时可以传入可选环境参数，LPC 代码用 `getenv()` 读取整张 mapping，或用 `getenv("name")` 读取单个值：

```powershell
.\build\vm\lpc_vm.exe run --bytecode-root .\bin --module zone/npc/mob --env shard=1 --env mode=dev
```

```c
int main() {
    mapping env = getenv();
    string shard = getenv("shard");
    return 0;
}
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

### 调试启动方式

`LPC: Debug Current File` 会先编译当前文件，然后按“当前文件相对工作区路径”推导入口模块。例如：

```text
base\module\hero\proto.lpc
```

会按模块名：

```text
base/module/hero/proto
```

启动 VM。默认入口函数是 `main`。因此如果当前文件没有 `main`，会出现：

```text
entry function not found
```

这种文件通常是业务模块、协议转发模块或被其他模块调用的库模块。调试时需要提供一个无参入口函数来触发你想看的业务函数：

```c
int proto_cs_activate_hero(int hero_id)
{
    return "module/hero/main"->activate_hero(hero_id);
}

int main()
{
    return proto_cs_activate_hero(1);
}
```

然后在 `proto_cs_activate_hero` 或被调用的 `activate_hero` 里打断点，再执行 `LPC: Debug Current File`。

也可以不用 `main`，在 `launch.json` 里指定调试入口函数：

```json
{
  "type": "lpc",
  "request": "launch",
  "name": "Debug hero proto",
  "program": "${workspaceFolder}/base/module/hero/proto.lpc",
  "entryModule": "base/module/hero/proto",
  "entryFunction": "debug"
}
```

对应 LPC：

```c
int debug()
{
    return proto_cs_activate_hero(1);
}
```

注意：当前 VM 启动入口函数必须是无参函数。带参数的业务函数需要由无参 `main`、`debug` 或其他入口函数包一层调用。

如果修改了插件源码，需要重新打包安装：

```powershell
.\scripts\package_vscode_plugin.ps1
```

## 业务代码建议

- 把复杂初始化放在 `main()` 或明确的初始化函数中，不要依赖复杂全局表达式。
- 当前适合写同步业务逻辑、规则计算、数据结构操作、热更新脚本验证。
- 网络连接、定时器、异步任务、数据库、日志、权限隔离等应由 C++/宿主服务器提供，再通过 efun 或宿主 API 暴露给 LPC。
