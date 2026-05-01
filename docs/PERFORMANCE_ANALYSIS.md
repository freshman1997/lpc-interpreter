# VM Performance Analysis

> 本文记录当前工作区的性能现状。数据会随机器负载、编译器和脚本版本波动，判断趋势时请优先看 30 次以上的 median。

## 测量命令

```powershell
.\scripts\compare_runtime_benchmarks.ps1 -SkipBuild -Iterations 30 -VmRelease -Out log\runtime_compare_latest.json
```

## 当前结果

最近一次 30 次迭代的 median wall-clock time：

| benchmark | lpc_vm | python | node |
| --- | ---: | ---: | ---: |
| int_loop | 58.423 ms | 117.896 ms | 42.136 ms |
| fib_rec | 44.723 ms | 26.752 ms | 39.398 ms |
| array_churn | 38.791 ms | 32.288 ms | 43.746 ms |
| mapping_churn | 46.622 ms | 37.474 ms | 52.510 ms |

## 已完成优化

- 局部变量自增/自减使用 `IncLocal` / `DecLocal`，避免无意义的栈流量。
- 同模块同版本内部调用跳过重复 hot-reload pin/unpin。
- 小数组、小 mapping 使用 inline storage，降低小对象堆分配。
- mapping hash 简化为更轻的整数混合。
- `NewArray` 直接构造 `LpcArray`，避免临时 vector 和二次拷贝。
- 提高 GC 初始阈值，减少小 benchmark 中的过早 GC。
- 增加多条 bytecode superinstruction：
  - `JumpIfLocalLtFalse`
  - `AddLocalLocalToLocal`
  - `IncLocalAndJump`
  - `JumpIfLocalIConstLteFalse`
  - `LoadLocalDec`
  - `LoadLocalSubIConst`
  - `LoadLocalAddIConst`
  - `AddLocalIndexIConstToLocal`
  - `AddLocalIndexLocalToLocal`
  - `AddLocalLocalIncJumpIfLocalLt`
  - `AddLocalIConstToLocal`
  - `SubLocalIConstToLocal`

## 当前热点

`int_loop` 已经从原先约 14M 指令压到约 6M 指令。继续大幅提升需要更结构化的方案：

- 针对整个循环体做 trace/JIT，或者在解释器内引入更激进的 loop superinstruction。
- 减少每条指令前的 debugger / GC / module-version 检查成本。
- 优化函数调用协议，特别是递归函数的 frame 创建和返回路径。

## 风险提示

- superinstruction 必须同时更新 opcode 定义、writer、verifier、VM dispatch 和 profile 名称。
- 跳转偏移使用“相对下一条指令”的 `i16`，不是绝对 PC。
- 当前全局初始化 `init_code` 执行器只支持简单常量初始化；复杂初始化应放进 `main()` 或业务函数中执行。
- `--release` 会关闭每条指令前的调试/安全检查；线上和 benchmark 建议使用，开发调试保持默认模式。
