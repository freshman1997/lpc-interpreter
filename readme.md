## LPC 解释器

一个 LPC 语言的完整实现，包含编译器、虚拟机、语言服务协议（LSP）和 VS Code 扩展。

### 项目结构

```
lpc-interpreter/
├── compiler/          前端编译器（词法→语法→语义→MIR→MIR优化→字节码→验证）
├── vm/                虚拟机（NaN-boxing 值系统、栈式字节码执行、GC、调试器、热重载）
├── lsp/               语言服务协议（JSON-RPC、诊断、补全）
├── vscode_plugin/     VS Code 扩展（语法高亮、代码片段、DAP 调试器集成）
├── include/lpc/       编译器与 VM 共享头文件（操作码定义、退出码）
├── lpc_src/           LPC 源码（回归测试 + 示例程序）
├── benchmarks/        运行时基准测试（LPC / JS / Lua / Python 对比）
├── scripts/           构建与工具脚本
├── docs/              设计文档
└── scripts/           构建与工具脚本
```

### 语法示例

```c
inherit "3.txt";

string msg = "hello";

static void swap(int *arr, int i, int j)
{
    mixed t = arr[i];
    arr[i] = arr[j];
    arr[j] = t;
}

static void quick_sort(int *arr, int l, int r)
{
    if (l >= r) return;

    int i = l, j = r;
    int p = arr[i];
    while (i < j) {
        while(i < j && arr[j] >= p) --j;
        while(i < j && arr[i] <= p) ++i;
        if (i < j) {
            swap(arr, i, j);
        }
    }

    arr[l] = arr[i];
    arr[i] = p;

    quick_sort(arr, l, i - 1);
    quick_sort(arr, i + 1, r);
}

void main()
{
    int *arr = [3, 1, 2, 0, 7, 8, 4, 5, 100, 78, 6, 0, 111, 222, 5];
    quick_sort(arr, 0, sizeof(arr) - 1);
    puts(arr);

    fun f = test1;
    mapping m = {"hello": "world", "f": f};
    m["aaa"]++;
    m["f"]();
    puts(m);
}
```

一个文件即为一个对象，所有对象以 `"char/user"`, `"module/template/mail"` 这种路径形式引用。

### 生命周期回调

每个对象（模块）可以定义以下生命周期回调函数：

| 回调 | 触发时机 | 说明 |
|---|---|---|
| `create()` | 对象创建时 | 入口模块：`main()` 之前执行；`clone_object()` 克隆时立即执行 |
| `on_loadin()` | 模块加载时 | 入口模块：`create()` 之后、`main()` 之前执行 |
| `on_destruct()` | 对象销毁时 | `destruct()` 调用时，在对象标记为已销毁之前执行 |

生命周期函数必须是无参的（`arity == 0`），否则调用会被静默跳过。回调索引存储在字节码 section `0x07`（LIFECYCLE）中，旧版读取器会自动跳过该 section（前向兼容）。

### 编译器

编译器采用多阶段流水线：

| 阶段 | 说明 |
|---|---|
| Lexer | 词法分析，生成 Token 流 |
| Preprocessor | `#include`、`#define`、`#if`/`#elif`/`#else`/`#endif` |
| Parser | 递归下降解析，生成 AST |
| Sema | 语义分析，类型检查与装饰 |
| MIR | 中间表示生成 |
| MIR Opt | 优化：常量折叠、常量传播、死代码消除、CFG 跳转链优化、catch/foreach 守卫、不可达代码删除、冗余 load-store 消除 |
| Bytecode Writer | 从 MIR 生成字节码 |
| Verifier | 字节码验证 |

支持的类型：`void`、`int`、`float`、`string`、`object`、`mapping`、`mixed`、`fun`（闭包）及用户定义 `class`。

修饰符：`static`、`private`、`public`、`nomask`。

### 虚拟机

- **NaN-boxing 值表示**：8 字节单值，内联存储 `int`（±70 万亿范围）、`float`、`bool`、`nil`，超标整数自动装箱
- **栈式字节码执行**：Release 模式下启用快速路径（整型比较融合、`LoadClassField` 句柄缓存、`CallDirect`/`Return` 快速路径等）
- **对象池 + 标记-清除 GC**：带阈值自适应和完整根集标记
- **定时器系统**：`call_later` / `cancel_timer`，基于二叉堆调度，支持配额和模块级清理
- **热重载**：模块版本管理，支持运行时替换对象代码
- **DAP 调试器**：支持 launch 和 attach 模式，断点、单步、变量查看
- **跨平台**：Windows / Linux OS 抽象层

#### 内置函数（Efuns）

| 类别 | 函数 |
|---|---|
| 输出 | `print`, `puts`, `write`, `sprintf` |
| 类型检查 | `typeof`, `stringp`, `intp`, `floatp`, `arrayp`, `mappingp`, `objectp`, `nullp`, `functionp` |
| 类型转换 | `to_string`, `to_int`, `to_float` |
| 集合操作 | `sizeof`, `keys`, `values`, `member_array`, `allocate`, `reverse`, `sort_array`, `map_delete` |
| 字符串 | `strlen`, `capitalize`, `lower_case`, `upper_case`, `explode`, `implode`, `strsrch`, `replace_string`, `regexp`, `regex_replace` |
| 数学 | `abs`, `min`, `max`, `sqrt`, `random` |
| 时间 | `time`, `ctime` |
| 对象 | `this_object`, `clone_object`, `destruct`, `call_other`, `instanceof`, `create`†, `on_loadin`†, `on_destruct`† |
| 定时器 | `call_later`, `cancel_timer`, `timer_exists`, `pending_timers`, `timer_info`, `timer_clear_module`, `timer_stats` |
| 其他 | `sleep`, `getenv`, `regex_stats` |

† 生命周期回调，非 efun，由 VM 在特定时机自动调用。

### 构建

需要 CMake 3.16+，支持 MinGW / MSVC / GCC：

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

产出三个可执行文件：
- `lpc-compiler` — 编译器，将 `.lpc` 编译为字节码
- `lpc-vm` — 虚拟机，加载并执行字节码
- `lpc-lsp` — 语言服务协议服务器

### 测试

执行完整回归测试（当前 55 个）：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_lpc_tests.ps1
```

默认根目录是脚本所在目录，可通过参数覆盖：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_lpc_tests.ps1 -Root (Get-Location).Path
```

测试会自动编译每个 `.lpc` 再用 VM 运行，最后输出 `Pass: x / y`。

### VS Code 扩展

`vscode_plugin/` 提供完整 IDE 集成：

- 语法高亮（TextMate 语法）与代码片段
- 一键编译 (`Ctrl+Alt+B`)、运行、调试 (`Ctrl+Alt+D`)
- DAP 调试器（launch / attach 模式，断点、变量查看）
- LSP 补全、诊断、`->` 成员补全（沿继承链）
- 配置项：`lpc.compilerPath`、`lpc.vmPath`、`lpc.lspPath`、`lpc.outRoot`、`lpc.includeDirs` 等

### 基准测试

```powershell
scripts\benchmark_vm.ps1
```

4 组基准（`int_loop`、`fib_rec`、`array_churn`、`mapping_churn`），含 JavaScript / Lua / Python 对比实现。

VM 支持 `--repeat N` 参数进行进程内多次执行以减少启动开销。

### 脚本

| 脚本 | 说明 |
|---|---|
| `scripts/benchmark_vm.ps1` | 运行基准测试 |
| `scripts/compare_runtime_benchmarks.ps1` | 对比基准测试结果 |
| `scripts/package_vscode_plugin.ps1` | 打包 VS Code 扩展 |
| `scripts/security_scan.ps1` | 安全扫描 |
