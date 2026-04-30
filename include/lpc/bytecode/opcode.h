#ifndef LPC_VM_BYTECODE_OPCODE_H
#define LPC_VM_BYTECODE_OPCODE_H

#include <cstdint>

namespace lpc {
// ============================================================================
// LPC NextVM Bytecode Instruction Set
// ============================================================================
//
// General encoding:
//   Each instruction starts with a 1-byte opcode (Op enum value).
//   Followed by zero or more parameter bytes depending on the opcode.
//
// Parameter types:
//   u8  = 1 byte unsigned
//   u16 = 2 bytes little-endian unsigned
//   i64 = 8 bytes little-endian signed (index into iconst pool)
//   f64 = 8 bytes little-endian IEEE754 (index into fconst pool)
//
// Stack notation:
//   [... before] -> [... after]
//   "val" means any Value is pushed/popped.
//   Numbers in parentheses indicate count of values.
//
// ObjRef encoding (uintptr_t stored in Value.as.obj):
//   String:    raw <  0x0800_0000_0   => index = (raw >> 1) - 1 into sconst[]
//   Function:  raw >= 0x0800_0000_0   => func_id = raw - 0x0800_0000_0  (1-based)
//   Array:     raw >= 0x1000_0000_0   => arr_id  = raw - 0x1000_0000_0  (1-based)
//   Mapping:   raw >= 0x2000_0000_0   => map_id  = raw - 0x2000_0000_0  (1-based)
//   Class:     raw >= 0x3000_0000_0   => cls_id  = raw - 0x3000_0000_0  (1-based)
//   Object:    raw >= 0x4000_0000_0   => obj_id  = raw - 0x4000_0000_0  (1-based)
//
// ============================================================================

enum class Op : std::uint8_t {

    // ----- Constants (3) -----

    // Load integer constant
    // Encoding: [opcode:u8] [iconst_index:u16]
    // iconst_index -> Chunk::iconst[index] -> push as Int64
    // Stack: [...] -> [int_val]
    // Size: 3 bytes
    LoadIConst = 1,

    // Load float constant
    // Encoding: [opcode:u8] [fconst_index:u16]
    // fconst_index -> Chunk::fconst[index] -> push as Float64
    // Stack: [...] -> [float_val]
    // Size: 3 bytes
    LoadFConst = 2,

    // Load string constant
    // Encoding: [opcode:u8] [sconst_index:u16]
    // sconst_index -> Chunk::sconst[index] -> push as ObjRef(String)
    // Stack: [...] -> [string_val]
    // Size: 3 bytes
    LoadSConst = 3,

    // ----- Arithmetic (8) -----

    // Integer addition
    // Encoding: [opcode:u8]
    // Pops 2 Int64, pushes Int64(a + b)
    // Stack: [..., a, b] -> [..., result]
    // Size: 1 byte
    Add = 4,

    // Integer subtraction
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a-b]
    // Size: 1 byte
    Sub = 5,

    // Integer multiplication
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a*b]
    // Size: 1 byte
    Mul = 6,

    // Integer division
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a/b]  (truncating, b!=0)
    // Size: 1 byte
    Div = 7,

    // Integer modulo
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a%b]
    // Size: 1 byte
    Mod = 8,

    // Integer negation
    // Encoding: [opcode:u8]
    // Stack: [..., a] -> [..., -a]
    // Size: 1 byte
    Neg = 9,

    // Increment local variable by 1 (in-place)
    // Encoding: [opcode:u8] [local_index:u16]
    // Reads locals[local_index], adds 1, writes back, pushes new value
    // Stack: [...] -> [new_val]
    // Size: 3 bytes
    Inc = 10,

    // Decrement local variable by 1 (in-place)
    // Encoding: [opcode:u8] [local_index:u16]
    // Reads locals[local_index], subtracts 1, writes back, pushes new value
    // Stack: [...] -> [new_val]
    // Size: 3 bytes
    Dec = 11,

    // ----- Bitwise (6) -----

    // Bitwise shift left
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a << b]
    // Size: 1 byte
    Shl = 12,

    // Bitwise shift right (arithmetic)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a >> b]
    // Size: 1 byte
    Shr = 13,

    // Bitwise AND
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a & b]
    // Size: 1 byte
    BitAnd = 14,

    // Bitwise OR
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a | b]
    // Size: 1 byte
    BitOr = 15,

    // Bitwise XOR
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., a ^ b]
    // Size: 1 byte
    BitXor = 16,

    // Bitwise NOT (complement)
    // Encoding: [opcode:u8]
    // Stack: [..., a] -> [..., ~a]
    // Size: 1 byte
    BitNot = 17,

    // ----- Comparison (6) -----

    // Equal (==)
    // Encoding: [opcode:u8]
    // Compares two Values for equality. Pushes Int64(1) or Int64(0)
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Eq = 18,

    // Not equal (!=)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Neq = 19,

    // Greater than (>)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Gt = 20,

    // Greater than or equal (>=)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Gte = 21,

    // Less than (<)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Lt = 22,

    // Less than or equal (<=)
    // Encoding: [opcode:u8]
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 1 byte
    Lte = 23,

    // ----- Logical (3) -----

    // Logical AND (short-circuit)
    // Encoding: [opcode:u8] [jump_offset:u16]
    // Pops a; if falsy, jumps by offset and pushes 0, else continues and
    // evaluates next expr (the compiler emits the RHS before the jump target).
    // Stack: [..., a, b] -> [..., int_result]  (if a is truthy, result = bool(b))
    //         [..., a]      -> [..., 0]          (if a is falsy, jumps)
    // Size: 3 bytes
    LogicAnd = 24,

    // Logical OR (short-circuit)
    // Encoding: [opcode:u8] [jump_offset:u16]
    // Pops a; if truthy, jumps by offset and pushes 1, else continues.
    // Stack: [..., a, b] -> [..., int_result]
    // Size: 3 bytes
    LogicOr = 25,

    // Logical NOT
    // Encoding: [opcode:u8]
    // Pushes Int64(1) if falsy, Int64(0) if truthy
    // Stack: [..., a] -> [..., int_result]
    // Size: 1 byte
    LogicNot = 26,

    // ----- Control Flow (5) -----

    // Return from current function
    // Encoding: [opcode:u8]
    // Pops return value, restores frame, pushes return value to caller's stack
    // Stack: [..., retval] -> [retval]  (in caller's frame)
    // Size: 1 byte
    Return = 27,

    // Jump (unconditional)
    // Encoding: [opcode:u8] [target_offset:u16]
    // target_offset is absolute byte offset into Chunk::code
    // Stack: unchanged
    // Size: 3 bytes
    Jump = 34,

    // Jump if top of stack is falsy (0, Nil, 0.0)
    // Encoding: [opcode:u8] [target_offset:u16]
    // Pops 1 value; if falsy, jumps to target_offset
    // Stack: [..., cond] -> [...]  (if falsy, jump)
    //        [..., cond] -> [...]  (if truthy, fall through)
    // Size: 3 bytes
    JumpIfFalse = 35,

    // Jump if top of stack is truthy
    // Encoding: [opcode:u8] [target_offset:u16]
    // Pops 1 value; if truthy, jumps to target_offset
    // Stack: [..., cond] -> [...]
    // Size: 3 bytes
    JumpIfTrue = 36,

    // Catch setup (try/catch)
    // Encoding: [opcode:u8] [catch_handler_offset:u16]
    // Sets the catch handler for the current frame. If a runtime error occurs,
    // execution jumps to catch_handler_offset instead of aborting.
    // catch_handler_offset = 0 means clear the catch handler.
    // Stack: unchanged
    // Size: 3 bytes
    Catch = 55,

    // ----- Local / Global / Upvalue Access (6) -----

    // Load local variable
    // Encoding: [opcode:u8] [local_index:u16]
    // Pushes the value of locals[local_index] onto the stack.
    // Locals are indexed: params first, then declared locals.
    //   locals[0..arity-1] = function parameters
    //   locals[arity..arity+nlocals-1] = local variables
    // Stack: [...] -> [..., val]
    // Size: 3 bytes
    LoadLocal = 28,

    // Store local variable
    // Encoding: [opcode:u8] [local_index:u16]
    // Pops top of stack, stores into locals[local_index], pushes the value back.
    // Stack: [..., val] -> [..., val]
    // Size: 3 bytes
    StoreLocal = 29,

    // Load global variable
    // Encoding: [opcode:u8] [global_index:u16]
    // Pushes Chunk::globals[global_index] onto the stack.
    // Stack: [...] -> [..., val]
    // Size: 3 bytes
    LoadGlobal = 30,

    // Store global variable
    // Encoding: [opcode:u8] [global_index:u16]
    // Pops top of stack, stores into Chunk::globals[global_index], pushes back.
    // Stack: [..., val] -> [..., val]
    // Size: 3 bytes
    StoreGlobal = 31,

    // Load upvalue
    // Encoding: [opcode:u8] [upvalue_index:u16]
    // Pushes the value captured by upvalues[upvalue_index].
    // UpvalueProto: source_kind=0(local), 1(upvalue); source_index=variable index
    // Stack: [...] -> [..., val]
    // Size: 3 bytes
    LoadUpvalue = 32,

    // Store upvalue
    // Encoding: [opcode:u8] [upvalue_index:u16]
    // Pops top of stack, stores into the upvalue, pushes back.
    // Stack: [..., val] -> [..., val]
    // Size: 3 bytes
    StoreUpvalue = 33,

    // ----- Function Calls (4) -----

    // Call a known function by ID (direct call)
    // Encoding: [opcode:u8] [func_id:u16] [argc:u16]
    // func_id is 1-based index into Chunk::functions.
    // argc = number of arguments (not including the function itself).
    // Pops argc args, creates new frame, executes function.
    // Stack: [..., arg0, arg1, ..., argN-1] -> [..., return_val]
    // Size: 5 bytes
    CallDirect = 37,

    // Call a value on the stack (indirect call, e.g. closure/function variable)
    // Encoding: [opcode:u8] [argc:u16]
    // argc = number of arguments (not including callee).
    // Pops callee + argc args. Callee must be ObjRef(Function) or Closure.
    // Stack: [..., arg0, ..., argN-1, callee] -> [..., return_val]
    // Size: 3 bytes
    CallValue = 38,

    // Call an efun (intrinsic/built-in function)
    // Encoding: [opcode:u8] [efun_index:u16] [argc:u16]
    // efun_index: 0=call_other, 1=print, 2=puts, 3=sleep, 4=sizeof,
    //   5=random, 6=keys, 7=values, 8=typeof, 9=to_string, 10=to_int,
    //   11=this_object, 12=clone_object, 13=destruct, 14=sprintf, 15=write,
    //   16=time, 17=member_array, 18=explode, 19=implode, 20=stringp,
    //   21=intp, 22=floatp, 23=arrayp, 24=mappingp, 25=objectp, 26=nullp,
    //   27=functionp, 28=to_float, 29=abs, 30=strlen, 31=map_delete,
    //   32=capitalize, 33=lower_case, 34=upper_case, 35=allocate,
    //   36=reverse, 37=min, 38=max, 39=sqrt, 40=ctime, 41=strsrch,
    //   42=replace_string, 43=sort_array
    // Void efuns (no return value, push Nil): 1,2,3,13,15,31
    // Stack: [..., arg0, ..., argN-1] -> [..., result]  (or [..., Nil] for void)
    // Size: 5 bytes
    CallIntrinsic = 39,

    // Call a virtual method on an object
    // Encoding: [opcode:u8] [name_sconst_index:u16] [argc:u16]
    // Looks up method by name in the object's class, calls it.
    // Stack: [..., obj, arg0, ..., argN-1] -> [..., return_val]
    //        or [..., obj, arg0, ..., argN-1] -> [..., Nil] if not found
    // Size: 5 bytes
    CallVirtual = 40,

    // ----- Function / Closure (1) -----

    // Load function reference as ObjRef
    // Encoding: [opcode:u8] [func_id:u16]
    // func_id is 1-based index into Chunk::functions.
    // Creates an ObjRef(Function) handle and pushes it.
    // Stack: [...] -> [..., func_ref]
    // Size: 3 bytes
    LoadFunc = 41,

    // ----- Object / Class (3) -----

    // Create a new class instance
    // Encoding: [opcode:u8] [class_index:u16]
    // class_index is 0-based index into Chunk::classes.
    // Allocates an object with class_index's field count, pushes ObjRef(Object).
    // Stack: [...] -> [..., obj_ref]
    // Size: 3 bytes
    NewClass = 42,

    // Store value into object's field
    // Encoding: [opcode:u8] [field_index:u16]
    // Pops value and object. Stores value into object's field at field_index.
    // Stack: [..., obj, val] -> [..., val]
    // Size: 3 bytes
    SetClassField = 43,

    // Load value from object's field
    // Encoding: [opcode:u8] [field_index:u16]
    // Pops object, pushes object's field at field_index.
    // Stack: [..., obj] -> [..., field_val]
    // Size: 3 bytes
    LoadClassField = 44,

    // ----- Collection Creation (2) -----

    // Create a new array with N elements from the stack
    // Encoding: [opcode:u8] [count:u16]
    // Pops `count` values, creates array, pushes ObjRef(Array).
    // First popped value becomes arr[count-1], last popped becomes arr[0].
    // Stack: [..., v0, v1, ..., vN-1] -> [..., arr_ref]
    // Size: 3 bytes
    NewArray = 45,

    // Create a new mapping with N pairs from the stack
    // Encoding: [opcode:u8] [pair_count:u16]
    // Pops 2*pair_count values (key-value pairs), creates mapping.
    // Pairs are popped in order: last pair first, first pair last.
    // Stack: [..., k0, v0, k1, v1, ..., kN-1, vN-1] -> [..., map_ref]
    // Size: 3 bytes
    NewMapping = 46,

    // ----- Index Operations (3) -----

    // Read element by index (array/string/mapping)
    // Encoding: [opcode:u8]
    // Pops key and container. For arrays: key must be Int64 index.
    //   For strings: key is Int64, returns 1-char string or BoundsError.
    //   For mappings: key can be Int64/ObjRef/Bool, returns value or Nil.
    // Stack: [..., container, key] -> [..., element_val]
    // Size: 1 byte
    Index = 47,

    // Write element by index (array/mapping)
    // Encoding: [opcode:u8]
    // Pops key, container, and value. Stores value at container[key].
    //   For arrays: key is Int64 index (BoundsError if out of range).
    //   For mappings: key can be Int64/ObjRef/Bool; updates existing or inserts.
    // Stack: [..., container, key, val] -> [..., val]
    // Size: 1 byte
    StoreIndex = 48,

    // Atomic index update (inc/dec or store with side effects)
    // Encoding: [opcode:u8] [sub_op:u8] [flags:u8]
    //   sub_op: 21 = increment, 22 = decrement, other = store
    //   flags: bit0 = 1 for prefix (return new val), 0 for postfix (return old val)
    //
    // If sub_op is 21 or 22 (inc/dec):
    //   Pops key and container. Increments/decrements container[key] in place.
    //   Stack: [..., container, key] -> [..., result_val]
    //   Size: 3 bytes
    //
    // If sub_op is other (store):
    //   Pops val, key, container. Stores val at container[key].
    //   Stack: [..., container, key, val] -> [..., val]
    //   Size: 3 bytes
    Upset = 49,

    // ----- Array Slice (1) -----

    // Extract sub-array [start, end)
    // Encoding: [opcode:u8]
    // Pops end, start, container. Creates new array from arr[start..end-1].
    // Stack: [..., container, start, end] -> [..., sub_arr_ref]
    // Size: 1 byte
    SubArr = 50,

    // ----- Stack Manipulation (2) -----

    // Pop and discard top of stack
    // Encoding: [opcode:u8]
    // Stack: [..., val] -> [...]
    // Size: 1 byte
    Pop = 51,

    // Duplicate top of stack
    // Encoding: [opcode:u8]
    // Stack: [..., val] -> [..., val, val]
    // Size: 1 byte
    Dup = 52,

    // ----- Iteration (2) -----

    // Foreach step 1: initialize iterator over array or mapping
    // Encoding: [opcode:u8] [iter_local:u16] [jump_end:u16]
    // Pops container. Stores iterator state in iter_local.
    // If container is empty, jumps to jump_end (absolute offset).
    // iter_local format: stores {container, index} packed into two locals.
    // Stack: [..., container] -> [...]  (sets up iterator state)
    // Size: 5 bytes
    ForeachStep1 = 53,

    // Foreach step 2: advance iterator, get next element
    // Encoding: [opcode:u8] [value_local:u16] [jump_end:u16]
    // Reads iterator state from previous ForeachStep1 locals.
    // If more elements: stores next value into value_local, falls through.
    // If no more elements: jumps to jump_end (absolute offset).
    // Stack: unchanged  (writes to locals)
    // Size: 5 bytes
    ForeachStep2 = 54,

    // ----- Reserved (1) -----

    // Reserved for future switch/table dispatch
    // Not generated by current compiler.
    // Size: variable (not defined)
    Switch = 56,
};

// Total: 37 opcodes (1..56, with gaps for removed/deprecated values)

} // namespace lpc

namespace lpc { namespace vm { using Op = lpc::Op; } }

#endif
