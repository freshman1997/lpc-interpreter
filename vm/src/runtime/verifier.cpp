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

static lint32_t ReadU32Le(const char *p) {
    return static_cast<lint32_t>(
        (static_cast<unsigned char>(p[0])) |
        (static_cast<unsigned char>(p[1]) << 8) |
        (static_cast<unsigned char>(p[2]) << 16) |
        (static_cast<unsigned char>(p[3]) << 24));
}

static lint32_t ReadU16Le(const char *p) {
    return static_cast<lint32_t>(
        (static_cast<unsigned char>(p[0])) |
        (static_cast<unsigned char>(p[1]) << 8));
}

static bool IsValidCodeTarget(lint32_t target, lint32_t code_size) {
    return target >= 0 && target <= code_size;
}

static const function_proto_t *FindFunctionAtOffset(const object_proto_t &proto, lint32_t offset) {
    if (!proto.func_table || proto.nfunction <= 0) {
        return nullptr;
    }
    for (lint32_t i = 0; i < proto.nfunction; ++i) {
        const function_proto_t &fn = proto.func_table[i];
        if (offset >= fn.fromPC && offset < fn.toPC) {
            return &fn;
        }
    }
    return nullptr;
}

static bool IsValidIndexedOperand(const object_proto_t &proto, const function_proto_t *fn, OpCode op, lint32_t operand, std::string *message) {
    switch (op) {
    case OpCode::op_load_global:
    case OpCode::op_store_global:
        if (operand < 0 || operand >= proto.nvariable) {
            if (message) *message = "global index out of range";
            return false;
        }
        return true;
    case OpCode::op_load_local:
    case OpCode::op_store_local:
        if (!fn) {
            if (message) *message = "local access outside function";
            return false;
        }
        if (operand < 0 || operand >= fn->nlocal) {
            if (message) *message = "local index out of range";
            return false;
        }
        return true;
    case OpCode::op_load_func:
        if (operand < 0 || operand >= proto.nfunction) {
            if (message) *message = "function index out of range";
            return false;
        }
        return true;
    case OpCode::op_load_iconst:
        if (operand < 0 || operand >= proto.niconst) {
            if (message) *message = "int const index out of range";
            return false;
        }
        return true;
    case OpCode::op_load_fconst:
        if (operand < 0 || operand >= proto.nfconst) {
            if (message) *message = "float const index out of range";
            return false;
        }
        return true;
    case OpCode::op_load_sconst:
        if (operand < 0 || operand >= proto.nsconst) {
            if (message) *message = "string const index out of range";
            return false;
        }
        return true;
    case OpCode::op_set_upvalue:
    case OpCode::op_get_upvalue:
        if (!fn) {
            if (message) *message = "upvalue access outside function";
            return false;
        }
        if (operand < 0 || operand >= fn->nupvalue) {
            if (message) *message = "upvalue index out of range";
            return false;
        }
        return true;
    case OpCode::op_new_class:
        if (operand < 0 || operand >= proto.nclass) {
            if (message) *message = "class index out of range";
            return false;
        }
        return true;
    case OpCode::op_set_class_field:
    case OpCode::op_load_class_field:
        if (operand < 0) {
            if (message) *message = "class field index out of range";
            return false;
        }
        return true;
    default:
        return true;
    }
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

        const lint32_t op_offset = pc - 1;
        OpCode op = static_cast<OpCode>(raw);
        const function_proto_t *fn = FindFunctionAtOffset(proto, op_offset);

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
                lint32_t call_index = ReadU16Le(proto.instructions + pc);
                if (type == 2 || type == 3) {
                    if (call_index < 0 || call_index >= proto.nfunction) {
                        r.ok = false;
                        r.message = "call function index out of range";
                        r.offset = pc;
                        return r;
                    }
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
            pc += 1;
            lint32_t target = ReadU32Le(proto.instructions + pc);
            if (!IsValidCodeTarget(target, proto.instruction_size)) {
                r.ok = false;
                r.message = "invalid op_foreach_step2 target";
                r.offset = pc;
                return r;
            }
            pc += 4;
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
            lint32_t operand = ReadU16Le(proto.instructions + pc);
            std::string msg;
            if (!IsValidIndexedOperand(proto, fn, op, operand, &msg)) {
                r.ok = false;
                r.message = msg;
                r.offset = pc;
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
            if (op == OpCode::op_test ||
                op == OpCode::op_test_not ||
                op == OpCode::op_goto ||
                op == OpCode::op_catch) {
                lint32_t target = ReadU32Le(proto.instructions + pc);
                if (!IsValidCodeTarget(target, proto.instruction_size)) {
                    r.ok = false;
                    r.message = "invalid jump target";
                    r.offset = pc;
                    return r;
                }
            }
            pc += 4;
            continue;
        }
    }

    return r;
}

} // namespace vm
