#include "runtime/verifier.h"

#include "opcode.h"

namespace vm {

static bool NeedU16(OpCode op) {
    switch (op) {
    case OpCode::op_load_global:
    case OpCode::op_load_local:
    case OpCode::op_store_global:
    case OpCode::op_store_local:
    case OpCode::op_load_func:
    case OpCode::op_load_iconst:
    case OpCode::op_load_fconst:
    case OpCode::op_load_sconst:
    case OpCode::op_set_upvalue:
    case OpCode::op_get_upvalue:
    case OpCode::op_new_class:
    case OpCode::op_set_class_field:
    case OpCode::op_load_class_field:
        return true;
    default:
        return false;
    }
}

static bool NeedU32(OpCode op) {
    switch (op) {
    case OpCode::op_test:
    case OpCode::op_test_not:
    case OpCode::op_new_array:
    case OpCode::op_new_mapping:
    case OpCode::op_goto:
    case OpCode::op_catch:
        return true;
    default:
        return false;
    }
}

static bool IsValidOpcodeValue(unsigned char raw) {
    return raw <= static_cast<unsigned char>(OpCode::op_catch);
}

VerifyResult VerifyV1Bytecode(const object_proto_t &proto) {
    VerifyResult r;
    if (!proto.instructions || proto.instruction_size <= 0) {
        // allow stub modules without executable body
        return r;
    }

    lint32_t pc = 0;
    while (pc < proto.instruction_size) {
        unsigned char raw = static_cast<unsigned char>(proto.instructions[pc++]);
        if (!IsValidOpcodeValue(raw)) {
            r.ok = false;
            r.message = "invalid opcode value";
            r.offset = pc - 1;
            return r;
        }

        OpCode op = static_cast<OpCode>(raw);

        if (op == OpCode::op_call) {
            if (pc >= proto.instruction_size) {
                r.ok = false;
                r.message = "truncated op_call type";
                r.offset = pc - 1;
                return r;
            }
            unsigned char type = static_cast<unsigned char>(proto.instructions[pc++]);
            if (type > 3) {
                r.ok = false;
                r.message = "invalid op_call type";
                r.offset = pc - 1;
                return r;
            }
            if (type != 0) {
                if (pc + 2 > proto.instruction_size) {
                    r.ok = false;
                    r.message = "truncated op_call index";
                    r.offset = pc - 1;
                    return r;
                }
                pc += 2;
                if (type == 1) {
                    if (pc + 1 > proto.instruction_size) {
                        r.ok = false;
                        r.message = "truncated efun argc";
                        r.offset = pc - 1;
                        return r;
                    }
                    pc += 1;
                }
            }
            continue;
        }

        if (op == OpCode::op_call_virtual) {
            if (pc + 4 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated op_call_virtual operands";
                r.offset = pc - 1;
                return r;
            }
            pc += 4;
            continue;
        }

        if (op == OpCode::op_upset) {
            if (pc + 1 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated op_upset sub-op";
                r.offset = pc - 1;
                return r;
            }
            unsigned char sub = static_cast<unsigned char>(proto.instructions[pc++]);
            if (!IsValidOpcodeValue(sub)) {
                r.ok = false;
                r.message = "invalid op_upset sub-op";
                r.offset = pc - 1;
                return r;
            }
            continue;
        }

        if (op == OpCode::op_foreach_step2) {
            if (pc + 1 + 4 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated op_foreach_step2 operands";
                r.offset = pc - 1;
                return r;
            }
            pc += 1 + 4;
            continue;
        }

        if (op == OpCode::op_switch) {
            if (pc + 2 + 4 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated op_switch operands";
                r.offset = pc - 1;
                return r;
            }
            pc += 2 + 4;
            continue;
        }

        if (NeedU16(op)) {
            if (pc + 2 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated u16 operand";
                r.offset = pc - 1;
                return r;
            }
            pc += 2;
            continue;
        }

        if (NeedU32(op)) {
            if (pc + 4 > proto.instruction_size) {
                r.ok = false;
                r.message = "truncated u32 operand";
                r.offset = pc - 1;
                return r;
            }
            pc += 4;
            continue;
        }
    }

    return r;
}

} // namespace vm
