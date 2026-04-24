# VM2 全量重设方案（主流对标）

本文档定义 LPC VM 的新一代运行时（VM2）。目标不是在 VM1 上继续修补，而是采用主流脚本语言运行时的工程范式重新设计。

## 1. 总目标

- 对标 Lua / CPython / Ruby VM 的可维护性与可观测性。
- 去除 `exit(-1)` 风格的热路径终止，统一 RuntimeError 传播。
- 建立稳定 ABI（frame、call、bytecode、intrinsic）。
- 运行时内核、对象模型、哈希、标准库全部重设。

## 2. 非目标

- 不做 JIT（本阶段仅解释器）。
- 不追求 VM1 字节码 100% 内部结构兼容（可通过桥接层读取）。
- 不保留 VM1 的历史命名/拼写包袱（例如 `mappig_`）。

## 3. 模块边界（目录级）

建议新增 `vm2/` 子树，避免与 VM1 强耦合：

- `vm/include/vm2/core/`
  - `status.h`、`error.h`、`config.h`
- `vm/include/vm2/value/`
  - `value.h`、`type_tag.h`、`object.h`
- `vm/include/vm2/bytecode/`
  - `opcode.h`、`chunk.h`、`reader.h`、`verifier.h`
- `vm/include/vm2/runtime/`
  - `vm.h`、`thread.h`、`frame.h`、`executor.h`
- `vm/include/vm2/gc/`
  - `heap.h`、`barrier.h`、`collector.h`
- `vm/include/vm2/intrinsic/`
  - `registry.h`、`builtin_*.h`
- `vm/include/vm2/debug/`
  - `debug_info.h`、`debugger.h`

## 4. 值系统（Value）

采用主流“标签 + payload”模型，明确区分值与对象。

### 4.1 ValueTag

- `Nil`
- `Bool`
- `Int64`
- `Float64`
- `ObjRef`

### 4.2 ObjType

- `String`
- `Array`
- `Map`
- `Function`
- `Closure`
- `Module`
- `Class`
- `Instance`
- `NativeFn`

### 4.3 关键约束

- 不再使用 `type/subtype` 双语义混杂。
- `Class` 与 `Instance` 为独立对象类型，不再借用 `array+subtype`。
- 所有比较、truthy、打印规则由统一 helper 实现。

## 5. 字节码与调用 ABI

## 5.1 Chunk/FunctionProto

- `Chunk` 包含：
  - 常量池
  - 函数原型表
  - 指令流
  - 调试段（line table、local table）
- `FunctionProto` 包含：
  - `name`
  - `arity`
  - `max_stack`
  - `code_start/code_end`
  - `upvalue_desc`

## 5.2 调用约定

- 栈机模型（先延续当前编译器产物习惯）。
- `Frame` 固定字段：
  - `proto_id`
  - `ip`
  - `base`
  - `stack_top`
  - `closure`
- 禁止 frame 直接依赖 VM1 的 `call_info_t`。

## 5.3 指令集策略

- 采用 `OPCODE_V2_SPEC.md` 为约束。
- 每条指令必须在 verifier 有静态表：
  - `min_stack`
  - `delta`
  - `operand_width`
  - `may_throw`
  - `may_alloc`

## 6. 执行器（Executor）

- 结构：`decode -> validate -> execute`。
- 运行时错误通过 `RuntimeError` 返回，不得在 executor 直接 `exit`。
- 提供两种 dispatch：
  - `switch` 基线
  - 可选 computed-goto（编译器支持时）

## 7. 错误模型（主流化）

统一三层错误：

- `Status`：非热路径与初始化失败。
- `RuntimeError`：执行期异常（带 frame/line）。
- `Fatal`：不可恢复错误（OOM/内部断言）。

`RuntimeError` 最低字段：

- `code`
- `message`
- `module`
- `function`
- `line`
- `pc`

## 8. 哈希与容器（Map）

Map 设计对标主流实现：

- 算法：开放寻址（Robin Hood）或 SwissTable 风格。
- 哈希：
  - 字符串采用带种子的 SipHash-2-4（抗碰撞）
  - 数值采用稳定 mix（uint64）
  - 对象键采用 identity hash（对象头缓存）
- 每个 VM 实例持有随机 `hash_seed`。

约束：

- 哈希与相等策略成对定义（`Hasher + Equal`）。
- 严禁 32 位截断指针哈希。

## 9. GC（主流基线）

- 基线：增量三色 mark-sweep。
- 强制写屏障：old -> young 引用更新时记录。
- 根集：
  - 线程栈
  - frame 链
  - 全局模块表
  - intern 字符串表
  - upvalue 列表

可选阶段：分代（nursery + old）。

## 10. 内建函数（Native/Intrinsic）

不再让 efun 直接操纵旧栈细节。统一注册模型：

- `NativeFn(ctx, span<Value> args) -> Result<Value, RuntimeError>`

要求：

- 参数个数/类型检查在入口统一。
- 无裸 `exit`。
- 所有错误带 source/span（若可用）。

## 11. 调试器与可观测性

VM2 内建调试 API：

- `GetFrames()`
- `GetLocals(frame_id)`
- `SetBreakpoint(module, line)`
- `Step/Next/Continue`

调试数据来自 bytecode debug 段：

- `pc -> line`
- `pc -> local live range`

## 12. 迁移策略（大改但可控）

你决定大改，本方案采用“VM2 并行 + 默认切换”节奏：

1. **阶段 A（接口冻结）**
   - 冻结 VM2 类型、字节码、错误模型。
2. **阶段 B（执行器 MVP）**
   - 跑通 `load/store/const/call/return/jump`。
3. **阶段 C（对象与容器）**
   - `array/map/class/instance/index/field` 全接入。
4. **阶段 D（intrinsic/stdlib）**
   - 迁移 `print/puts/sleep/sizeof/random/keys/values/call_other`。
5. **阶段 E（切换默认）**
   - CLI 默认 VM2，VM1 仅保留 `--vm=legacy` 一段过渡期。

## 13. 完成定义（DoD）

- VM2 跑通当前 compiler golden 对应脚本入口。
- VM2 verifier 全通过，且错误可定位到模块/函数/行。
- 无热路径 `exit(-1)`。
- `--debug` 可断点/单步/看栈。
- `--self-check` 覆盖关键 opcode 语义。

## 14. 立即执行清单

1. 新建 `vm2` 目录树与核心头文件。
2. 落 `Value/Frame/RuntimeError` 基础类型。
3. 落 `BytecodeReader + Verifier`。
4. 落 `Executor` MVP 并接 CLI `--vm=next`。
5. 建立 VM1/VM2 对拍命令（同入口执行结果比对）。
