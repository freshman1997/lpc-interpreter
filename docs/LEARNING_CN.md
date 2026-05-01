# LPC Interpreter 学习文档

## 推荐阅读顺序

1. 从 `benchmarks/runtime/int_loop.lpc` 开始，理解最小 LPC 程序。
2. 看 `compiler/src/frontend/pipeline.cpp`，了解 AST 如何生成 MIR。
3. 看 `compiler/src/frontend/bytecode_writer.cpp`，理解 MIR 如何写成 NextVM 字节码。
4. 看 `include/lpc/bytecode/opcode.h`，建立 opcode 语义表。
5. 看 `vm/src/runtime/vm_dispatch.cpp`，跟一条字节码如何执行。
6. 看 `vm/src/cli/dap_server.cpp`，理解 VS Code 调试交互。

## 如何读一段程序

以：

```c
int main() {
    int n = 10;
    int i = 0;
    while (i < n) {
        i = i + 1;
    }
    return i;
}
```

为例，前端大致会生成：

```text
LoadConst 10
StoreLocal n
LoadConst 0
StoreLocal i
loop:
LoadLocal i
LoadLocal n
Lt
JumpIfFalse end
LoadLocal i
LoadConst 1
Add
StoreLocal i
Jump loop
end:
LoadLocal i
Return
```

bytecode writer 会进一步把部分连续 MIR 合成 superinstruction，减少 VM 调度成本。

## 调试学习路线

1. 在 VS Code 打开 `.lpc` 文件。
2. 在行号左侧设置断点。
3. 执行 `LPC: Debug Current File`。
4. 在 Debug Console 看 `print/puts/write` 输出。
5. 用 F10 单步，观察 Variables 中的局部变量、数组、mapping 和 class。

## 性能学习路线

常用命令：

```powershell
.\scripts\compare_runtime_benchmarks.ps1 -SkipBuild -Iterations 30 -Out log\runtime_compare_latest.json
```

查看 profile：

```powershell
$j = Get-Content log\runtime_compare_latest.json -Raw | ConvertFrom-Json
($j | Where-Object { $_.benchmark -eq "int_loop" -and $_.runtime -eq "lpc_vm" }).profile
```

读 profile 时优先看：

- 指令数是否下降。
- wall-clock median 是否下降。
- 新优化是否只改善一个 benchmark，却让其他 benchmark 明显退化。

## 修改 opcode 的 checklist

- 在 `opcode.h` 增加编号和编码注释。
- 在 bytecode writer 中输出新编码。
- 在 verifier 中检查 operand、栈效果和跳转目标。
- 在 VM dispatch 中实现语义。
- 在 profile 中增加可读名字。
- 跑 `.\run_lpc_tests.ps1`。
- 跑 benchmark，确认没有明显回退。
