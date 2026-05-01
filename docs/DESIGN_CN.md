# LPC Interpreter 设计文档

## 总览

当前工程由四层组成：

1. **编译前端**：lexer/parser/sema/MIR/bytecode writer。
2. **NextVM 字节码**：`.nb` 二进制模块，包含常量、函数、类信息、源码行表和调试信息。
3. **运行时 VM**：解释执行字节码，管理值栈、调用栈、数组、mapping、class、对象、GC 和热更新版本。
4. **工具链**：CLI、LSP、VS Code 插件、DAP 调试器、benchmark 脚本。

## 编译链路

源文件 `.lpc` 的主要路径是：

```text
source.lpc
  -> Lexer
  -> Parser / AST
  -> Sema
  -> MIR
  -> MIR optimization
  -> NextVM bytecode writer
  -> bin/*.nb
```

关键文件：

- `compiler/src/frontend/parser.cpp`
- `compiler/src/frontend/sema.cpp`
- `compiler/src/frontend/mir_opt.cpp`
- `compiler/src/frontend/bytecode_writer.cpp`
- `include/lpc/bytecode/opcode.h`

## VM 执行模型

VM 使用值栈和调用帧：

- `Value` 表示 int、float、nil、对象引用等。
- `Frame` 保存当前函数、PC、base、stack_top、对象和模块版本。
- 数组、mapping、class、object 由 VM 统一分配和 GC。
- 热更新通过 module version pin 保证执行中的帧不被替换。

关键文件：

- `vm/include/vm/runtime/vm.h`
- `vm/include/vm/runtime/frame.h`
- `vm/src/runtime/vm_dispatch.cpp`
- `vm/src/runtime/vm_alloc.cpp`
- `vm/src/runtime/vm_gc.cpp`
- `vm/src/runtime/vm_chunk_lifecycle.cpp`

## 字节码和 superinstruction

普通字节码是一条一条解释执行的。superinstruction 是把高频连续字节码合成一条，例如：

```text
LoadLocal i
LoadLocal n
Lt
JumpIfFalse exit
```

合成：

```text
JumpIfLocalLtFalse i, n, exit
```

它的目标是减少 dispatch 次数和栈操作，不改变 LPC 语义。新增 superinstruction 时必须同步更新：

- `include/lpc/bytecode/opcode.h`
- `compiler/src/frontend/bytecode_writer.cpp`
- `vm/src/runtime/verifier.cpp`
- `vm/src/runtime/vm_dispatch.cpp`
- `vm/src/runtime/vm_profile.cpp`

## 调试设计

VS Code 插件使用 DAP 调试：

- 插件命令 `Debug Current File` 会编译当前 `.lpc` 文件。
- VM 以 DAP 模式运行。
- breakpoint、step over、variables、evaluate、Debug Console 输出由 `vm/src/cli/dap_server.cpp` 提供。
- 行号映射来自 bytecode writer 输出的 line table。

## 当前边界

- `Switch` opcode 仍是保留项，VM 中尚未实现。
- `init_code` 只适合简单全局常量初始化。
- 运行时还不是完整游戏服务器框架：网络、定时器、协程/异步 IO、持久化、权限沙箱等需要宿主层继续接入。
