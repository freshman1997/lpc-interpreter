#include <iostream>
#include <string>
#include <cstring>

#include "opcode.h"
#include "lpc.h"
#include "lpc_value.h"
#include "type/lpc_object.h"
#include "type/lpc_proto.h"
#include "type/lpc_array.h"
#include "runtime/interpreter.h"
#include "runtime/stack.h"
#include "memory/memory.h"
#include "type/lpc_string.h"
#include "runtime/vm.h"

static void RuntimePanic(lpc_vm_t *lvm, const std::string &msg)
{
    std::cout << msg;
    if (lvm) {
        std::cout << " [at " << lvm->current_frame_string() << "]";
    }
    std::cout << std::endl;
    if (lvm && lvm->non_fatal_mode()) {
        lvm->set_last_error(msg + " [at " + lvm->current_frame_string() + "]\n" + lvm->traceback_string());
        throw vm_abort_signal();
    }
    if (lvm) {
        lvm->traceback();
        std::cout.flush();
    }
    exit(-1);
}

#define ERROR(msg) RuntimePanic(lvm, (msg))

static inline luint16_t ReadU16(const char *&pc)
{
    luint16_t v = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    return v;
}

static inline luint32_t ReadU32(const char *&pc)
{
    luint32_t v = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    return v;
}

enum class EvalAction
{
    None,
    ReloadFrame,
    Stop,
};

struct CatchContext {
    bool active = false;
    const char *end_pc = nullptr;
    bool prev_non_fatal = false;
};

enum class NumericBinOp
{
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Shl,
    Shr,
    BitAnd,
    BitOr,
    BitXor,
};

enum class CompareOp
{
    Eq,
    Neq,
    Gt,
    Gte,
    Lt,
    Lte,
};

static EvalAction HandleOpNumericBinary(lpc_vm_t *lvm, lpc_stack_t *sk, NumericBinOp op)
{
    lpc_value_t *v1 = sk->pop();
    lpc_value_t *v2 = sk->pop();
    if (!v1 || !v2) {
        ERROR("error found: stack underflow on binary operation!");
    }

    if (op == NumericBinOp::Mod) {
        if (v1->is_int() && v2->is_int()) {
            if (v1->get_int() == 0) ERROR("error found: division by zero!");
            v2->set_int(v2->get_int() % v1->get_int());
        } else {
            ERROR("error found: only integer number can use % operator!");
        }
        sk->push(v2);
        return EvalAction::None;
    }

    if (op == NumericBinOp::Shl || op == NumericBinOp::Shr || op == NumericBinOp::BitAnd ||
        op == NumericBinOp::BitOr || op == NumericBinOp::BitXor) {
        if (v1->is_int() && v2->is_int()) {
            int a = v2->get_int(), b = v1->get_int();
            if (op == NumericBinOp::Shl) {
                if (b < 0 || b >= 32) ERROR("error found: shift amount out of range!");
                v2->set_int(a << b);
            } else if (op == NumericBinOp::Shr) {
                if (b < 0 || b >= 32) ERROR("error found: shift amount out of range!");
                v2->set_int(a >> b);
            } else if (op == NumericBinOp::BitAnd) v2->set_int(a & b);
            else if (op == NumericBinOp::BitOr) v2->set_int(a | b);
            else v2->set_int(a ^ b);
        } else {
            if (op == NumericBinOp::Shl) ERROR("error found on oper << !");
            if (op == NumericBinOp::Shr) ERROR("error found on oper >> !");
            if (op == NumericBinOp::BitAnd) ERROR("error found on oper & !");
            if (op == NumericBinOp::BitOr) ERROR("error found on oper | !");
            ERROR("error found on oper ^ !");
        }
        sk->push(v2);
        return EvalAction::None;
    }

    if (op == NumericBinOp::Add && (v1->is_string() || v2->is_string())) {
        auto val_to_str = [](lpc_value_t *val) -> std::string {
            if (val->is_string()) {
                lpc_string_t *s = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
                return std::string(s->get_str(), s->get_size());
            } else if (val->is_int()) {
                return std::to_string(val->get_int());
            } else if (val->is_float()) {
                return std::to_string(val->get_float());
            } else if (val->is_null()) {
                return "0";
            } else if (val->is_bool()) {
                return val->get_bool() ? "1" : "0";
            }
            return "";
        };
        std::string result = val_to_str(v2) + val_to_str(v1);
        lpc_string_t *ls = lvm->get_alloc()->allocate_string(result.c_str(), true);
        lpc_value_t val;
        val.set_string(reinterpret_cast<lpc_gc_object_t *>(ls));
        sk->push_value(val);
        return EvalAction::None;
    }

    if (v1->is_int() && v2->is_int()) {
        int a = v2->get_int(), b = v1->get_int();
        if (op == NumericBinOp::Div && b == 0) ERROR("error found: division by zero!");
        if (op == NumericBinOp::Add) v2->set_int(a + b);
        else if (op == NumericBinOp::Sub) v2->set_int(a - b);
        else if (op == NumericBinOp::Mul) v2->set_int(a * b);
        else v2->set_int(a / b);
    } else {
        float left = v2->is_float() ? v2->get_float() : static_cast<float>(v2->get_int());
        float right = v1->is_float() ? v1->get_float() : static_cast<float>(v1->get_int());
        if (!v1->is_number() || !v2->is_number()) {
            ERROR("unsupport opertion!!");
        }
        if (op == NumericBinOp::Div && right == 0.0f) ERROR("error found: division by zero!");
        if (op == NumericBinOp::Add) v2->set_float(left + right);
        else if (op == NumericBinOp::Sub) v2->set_float(left - right);
        else if (op == NumericBinOp::Mul) v2->set_float(left * right);
        else v2->set_float(left / right);
    }

    sk->push(v2);
    return EvalAction::None;
}

static EvalAction HandleOpCompare(lpc_vm_t *lvm, lpc_stack_t *sk, CompareOp op)
{
    lpc_value_t *val1 = sk->pop();
    lpc_value_t *val2 = sk->pop();
    if (!val1 || !val2) {
        ERROR("error found: stack underflow on compare operation!");
    }
    lpc_value_t result;

    if (op == CompareOp::Eq || op == CompareOp::Neq) {
        if (val1->is_int() && val2->is_int()) {
            result.set_int((op == CompareOp::Eq)
                ? (val1->get_int() == val2->get_int())
                : (val1->get_int() != val2->get_int()));
        } else if (val1->is_float() && val2->is_float()) {
            result.set_int((op == CompareOp::Eq)
                ? (val1->get_float() == val2->get_float())
                : (val1->get_float() != val2->get_float()));
        } else if (val1->is_int() && val2->is_float()) {
            result.set_int((op == CompareOp::Eq)
                ? (val1->get_int() == val2->get_float())
                : (val1->get_int() != val2->get_float()));
        } else if (val1->is_float() && val2->is_int()) {
            result.set_int((op == CompareOp::Eq)
                ? (val1->get_float() == val2->get_int())
                : (val1->get_float() != val2->get_int()));
        } else if (val1->is_string() && val2->is_string()) {
            lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(val1->get_gcobj());
            lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(val2->get_gcobj());
            result.set_int((op == CompareOp::Eq)
                ? (str1->get_hash() == str2->get_hash() && strcmp(str1->get_str(), str2->get_str()) == 0)
                : (str1->get_hash() != str2->get_hash() || strcmp(str1->get_str(), str2->get_str()) != 0));
        } else {
            result.set_int((op == CompareOp::Eq)
                ? (val1->get_gcobj() == val2->get_gcobj())
                : (val1->get_gcobj() != val2->get_gcobj()));
        }
        sk->push_value(result);
        return EvalAction::None;
    }

    if (val1->is_int() && val2->is_int()) {
        int a = val2->get_int(), b = val1->get_int();
        if (op == CompareOp::Gt) result.set_int(a > b);
        else if (op == CompareOp::Gte) result.set_int(a >= b);
        else if (op == CompareOp::Lt) result.set_int(a < b);
        else result.set_int(a <= b);
    } else if (val1->is_float() && val2->is_float()) {
        float a = val2->get_float(), b = val1->get_float();
        if (op == CompareOp::Gt) result.set_int(a > b);
        else if (op == CompareOp::Gte) result.set_int(a >= b);
        else if (op == CompareOp::Lt) result.set_int(a < b);
        else result.set_int(a <= b);
    } else if (val1->is_int() && val2->is_float()) {
        float a = val2->get_float(); int b = val1->get_int();
        if (op == CompareOp::Gt) result.set_int(a > b);
        else if (op == CompareOp::Gte) result.set_int(a >= b);
        else if (op == CompareOp::Lt) result.set_int(a < b);
        else result.set_int(a <= b);
    } else if (val1->is_float() && val2->is_int()) {
        int a = val2->get_int(); float b = val1->get_float();
        if (op == CompareOp::Gt) result.set_int(a > b);
        else if (op == CompareOp::Gte) result.set_int(a >= b);
        else if (op == CompareOp::Lt) result.set_int(a < b);
        else result.set_int(a <= b);
    } else {
        ERROR("cant compare non-number types");
    }

    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpCall(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    lint8_t type = *(pc++);
    if (type != 0) {
        luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
        pc += 2;
        ci->savepc = pc;
        if (type == 3) {
            lvm->new_frame(ci->cur_obj, idx);
            return EvalAction::ReloadFrame;
        }
        if (type == 2) {
            lvm->new_frame(lvm->get_sfun_object(), idx);
            return EvalAction::ReloadFrame;
        }
        if (type == 1) {
            lint8_t nargs = *(pc++);
            efun_t *efuns = lvm->get_efuns();
            efuns[idx](lvm, nargs);
            return EvalAction::None;
        }
        return EvalAction::None;
    }

    lpc_value_t *val = sk->pop();
    if (!val) {
        ERROR("error found: stack underflow on function call!");
    }
    if (!val->is_function() && !val->is_closure()) {
        ERROR("error found on calling a function!");
    }

    lpc_function_t *f = reinterpret_cast<lpc_function_t *>(val->get_gcobj());
    if (f->idx < 0) {
        ERROR("error found: not a function object!");
    }

    ci->savepc = pc;
    lvm->new_frame(f->owner ? f->owner : ci->cur_obj, f->idx, false, f);
    return EvalAction::ReloadFrame;
}

static EvalAction HandleOpCallVirtual(lpc_vm_t *lvm, call_info_t *ci, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    luint16_t idx1 = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    object_proto_t *proto = ci->cur_obj->get_proto();
    if (proto->ninherit <= idx) {
        ERROR("cant index inherit table!!!");
    }

    ci->savepc = pc;
    lpc_value_t tmp;
    tmp.set_string(reinterpret_cast<lpc_gc_object_t *>(proto->inherits[idx]));
    lpc_object_t *father = lvm->find_oject(&tmp);
    call_info_t *call = lvm->new_frame(father, idx1);
    call->cur_obj = ci->cur_obj;
    call->father = father->get_proto();
    call->inherit_offset = proto->inherit_offsets[idx];
    return EvalAction::ReloadFrame;
}

static EvalAction HandleOpReturn(lpc_vm_t *lvm, call_info_t *ci)
{
    if (ci->call_other || ci->call_init) {
        lvm->pop_frame();
        return EvalAction::Stop;
    }

    lvm->pop_frame();
    return lvm->get_call_info() ? EvalAction::ReloadFrame : EvalAction::Stop;
}

static EvalAction HandleOpTest(lpc_vm_t *lvm, lpc_stack_t *sk, const char *start, const char *&pc)
{
    lpc_value_t *val = sk->pop();
    if (!val) {
        ERROR("error found: stack underflow on test!");
    }
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    if (val->is_falsy()) {
        pc = start + idx;
    }
    return EvalAction::None;
}

static EvalAction HandleOpGoto(const char *start, const char *&pc)
{
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    pc = start + idx;
    return EvalAction::None;
}

static EvalAction HandleOpCatch(const char *start, const char *&pc, CatchContext &ctx)
{
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    ctx.active = true;
    ctx.end_pc = start + idx;
    ctx.prev_non_fatal = false;
    return EvalAction::None;
}

static EvalAction HandleOpSwitch(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *start, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t *val = sk->pop();
    if (!val) {
        ERROR("error found: stack underflow on switch!");
    }
    lint32_t hashKey;
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        hashKey = str->get_hash();
    } else if (val->is_int()) {
        hashKey = val->get_int();
    } else {
        ERROR("error found: only string or number can be calculated a hash number!");
    }

    object_proto_t *proto = ci->cur_obj->get_proto();
    std::unordered_map<lint32_t, lint32_t> &map = (*proto->lookup_table)[idx];
    if (map.count(hashKey)) {
        pc = start + map[hashKey];
    } else if (proto->defaults->count(idx)) {
        pc = start + (*proto->defaults)[idx];
    } else {
        luint32_t idx1 = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
        pc = start + idx1;
    }

    return EvalAction::None;
}

static EvalAction HandleOpIndex(lpc_vm_t *lvm, lpc_stack_t *sk)
{
    lpc_value_t *key = sk->pop();
    lpc_value_t *con = sk->pop();
    if (!key || !con) {
        ERROR("error found: stack underflow on index!");
    }
    if (con->is_mapping()) {
        lpc_mapping_t *mapping = reinterpret_cast<lpc_mapping_t *>(con->get_gcobj());
        lpc_value_t *val = mapping->get_value(key);
        if (!val) {
            lpc_value_t undef;
            undef.set_undefined();
            sk->push_value(undef);
        } else {
            sk->push(val);
        }
        return EvalAction::None;
    }

    if (con->is_string()) {
        if (!key->is_int()) {
            ERROR("Only integer can index a string!!");
        }
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(con->get_gcobj());
        int idx = key->get_int();
        if (idx < 0 || idx >= str->get_size()) {
            std::string msg = "string index out of bounds: " + std::to_string(idx);
            ERROR(msg);
        }
        char buf[2] = { static_cast<char>(str->get(idx)), '\0' };
        lpc_string_t *result = lvm->get_alloc()->allocate_string(buf, true);
        lpc_value_t val;
        val.set_string(reinterpret_cast<lpc_gc_object_t *>(result));
        sk->push_value(val);
        return EvalAction::None;
    }

    if (con->is_array()) {
        if (!key->is_int()) {
            ERROR("Only integer can index an array!!");
        }

        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->get_gcobj());
        if (arr->get_size() <= key->get_int() || key->get_int() < 0) {
            std::string msg = "Negative indexing key: " + std::to_string(key->get_int());
            ERROR(msg);
        }

        lpc_value_t *val = arr->get(key->get_int());
        sk->push(val);
        return EvalAction::None;
    }

    std::string msg = "error type to index: " + std::to_string((int)con->type());
    ERROR(msg);
    return EvalAction::Stop;
}

static EvalAction HandleOpStoreIndex(lpc_vm_t *lvm, lpc_stack_t *sk)
{
    lpc_value_t *key = sk->pop();
    lpc_value_t *con = sk->pop();
    lpc_value_t *val = sk->pop();
    if (!key || !con || !val) {
        ERROR("error found: stack underflow on store index!");
    }

    if (con->is_array()) {
        if (!key->is_int()) {
            ERROR("Only integer can index an array!!");
        }
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->get_gcobj());
        int idx = key->get_int();
        if (idx < 0 || idx >= arr->get_size()) {
            ERROR("array index out of bounds in store");
        }
        arr->set(val, idx);
        lvm->gc_write_barrier(con->get_gcobj(), val);
        sk->push(val);
        return EvalAction::None;
    }

    if (con->is_mapping()) {
        lpc_mapping_t *mapping = reinterpret_cast<lpc_mapping_t *>(con->get_gcobj());
        mapping->upset(key, val, OpCode::op_upset);
        lvm->gc_write_barrier(con->get_gcobj(), val);
        sk->push(val);
        return EvalAction::None;
    }

    ERROR("error type to store index");
    return EvalAction::Stop;
}

static EvalAction HandleOpNewArray(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    lpc_array_t *arr = lvm->get_alloc()->allocate_array(idx);
    for (int i = idx; i >= 1; --i) {
        arr->set(sk->pop(), i - 1);
    }
    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpSubArr(lpc_vm_t *lvm, lpc_stack_t *sk)
{
    lpc_value_t *end = sk->pop();
    lpc_value_t *con = sk->pop();
    lpc_value_t *start = sk->pop();
    if (!end || !con || !start) {
        ERROR("error found: stack underflow on sub array!");
    }
    if (!con->is_array()) {
        ERROR("only array can be sub!!");
    }

    lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->get_gcobj());
    if (start->get_int() < 0 || start->get_int() > end->get_int() ||
        start->get_int() >= arr->get_size() || end->get_int() >= arr->get_size()) {
        ERROR("negtive param to sub an array!!");
    }

    lpc_array_t *tmp = nullptr;
    if (start->get_int() == end->get_int()) {
        tmp = lvm->get_alloc()->allocate_array(0);
    } else {
        tmp = lvm->get_alloc()->allocate_array(end->get_int() - start->get_int() + 1);
        for (int i = start->get_int(), j = 0; i <= end->get_int(); ++i, ++j) {
            tmp->set(arr->get(i), j);
        }
    }

    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(tmp));
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpNewMapping(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;
    lpc_mapping_t *map = lvm->get_alloc()->allocate_mapping();
    for (int i = idx; i > 0; i--) {
        lpc_value_t *v = sk->pop();
        lpc_value_t *k = sk->pop();
        map->set(k, v);
    }
    lpc_value_t result;
    result.set_mapping(reinterpret_cast<lpc_gc_object_t *>(map));
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpUpset(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    OpCode op = (OpCode)*(pc++);
    lpc_value_t *k = sk->pop();
    lpc_value_t *con = sk->pop();
    lpc_value_t *v = nullptr;
    if (!k || !con) {
        ERROR("error found: stack underflow on upset!");
    }
    if (op != OpCode::op_inc && op != OpCode::op_dec && op != OpCode::op_minus) {
        v = sk->pop();
        if (!v) {
            ERROR("error found: stack underflow on upset value!");
        }
    }

    if (con->is_array()) {
        if (!k->is_int()) {
            ERROR("error found: only integer number can index a array object!");
        }

        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->get_gcobj());
        bool res = arr->upset(v, k->get_int(), op);
        if (!res) {
            ERROR("upset array failed!!");
        }
        return EvalAction::None;
    }

    if (con->is_mapping()) {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(con->get_gcobj());
        bool res = map->upset(k, v, op);
        if (!res) {
            ERROR("upset mapping failed!!");
        }
        return EvalAction::None;
    }

    std::string msg = "error type to index: " + std::to_string((int)con->type());
    ERROR(msg);
    return EvalAction::Stop;
}

static EvalAction HandleOpNewClass(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    class_proto_t &cl = ci->cur_obj->get_proto()->class_table[idx];
    lpc_array_t *clazz = lvm->get_alloc()->allocate_array(cl.nfield);
    lpc_value_t result;
    result.set_class(reinterpret_cast<lpc_gc_object_t *>(clazz));
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpSetClassField(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t *clazz = sk->pop();
    lpc_value_t *val = sk->pop();
    if (!clazz || !val) {
        ERROR("error found: stack underflow on class field store!");
    }
    if (!clazz->is_class()) {
        ERROR("error found: not a class object!");
    }

    lpc_array_t &arr = clazz->get_gcobj()->arr;
    if (arr.get_size() <= idx) {
        ERROR("error found: can not set class field!");
    }
    arr.set(val, idx);
    lvm->gc_write_barrier(clazz->get_gcobj(), val);
    return EvalAction::None;
}

static EvalAction HandleOpLoadClassField(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t *clazz = sk->pop();
    if (!clazz) {
        ERROR("error found: stack underflow on class field load!");
    }
    if (!clazz->is_class()) {
        ERROR("error found: not a class object!");
    }
    lpc_array_t &arr = clazz->get_gcobj()->arr;
    if (arr.get_size() <= idx) {
        ERROR("error found: can not set class field!");
    }
    lpc_value_t result = *arr.get(idx);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpBinaryLogicAnd(lpc_stack_t *sk)
{
    lpc_value_t *val1 = sk->pop();
    lpc_value_t *val2 = sk->pop();
    if (!val1 || !val2) {
        lpc_value_t result;
        result.set_int(0);
        sk->push_value(result);
        return EvalAction::None;
    }
    lpc_value_t result;
    result.set_int(!val1->is_falsy() && !val2->is_falsy() ? 1 : 0);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpBinaryLogicOr(lpc_stack_t *sk)
{
    lpc_value_t *val1 = sk->pop();
    lpc_value_t *val2 = sk->pop();
    if (!val1 || !val2) {
        lpc_value_t result;
        result.set_int(0);
        sk->push_value(result);
        return EvalAction::None;
    }
    lpc_value_t result;
    result.set_int(!val1->is_falsy() || !val2->is_falsy() ? 1 : 0);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpLogicNot(lpc_stack_t *sk)
{
    lpc_value_t *val1 = sk->pop();
    if (!val1) {
        lpc_value_t result;
        result.set_int(1);
        sk->push_value(result);
        return EvalAction::None;
    }
    lpc_value_t result;
    result.set_int(val1->is_falsy() ? 1 : 0);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpOr(lpc_stack_t *sk)
{
    lpc_value_t *val1 = sk->pop();
    lpc_value_t *val2 = sk->pop();
    if (!val1 || !val2) {
        lpc_value_t result;
        result.set_int(0);
        sk->push_value(result);
        return EvalAction::None;
    }
    if (val1->is_falsy()) {
        sk->push(val2);
    } else {
        sk->push(val1);
    }
    return EvalAction::None;
}

static EvalAction HandleCatchAbort(lpc_stack_t *sk)
{
    lpc_value_t result;
    result.set_int(1);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpInc(lpc_stack_t *sk)
{
    lpc_value_t *val = sk->top();
    if (val->is_int()) {
        val->set_int(val->get_int() + 1);
    } else if (val->is_float()) {
        val->set_float(val->get_float() + 1);
    }
    return EvalAction::None;
}

static EvalAction HandleOpDec(lpc_stack_t *sk)
{
    lpc_value_t *val = sk->top();
    if (val->is_int()) {
        val->set_int(val->get_int() - 1);
    } else if (val->is_float()) {
        val->set_float(val->get_float() - 1);
    }
    return EvalAction::None;
}

static EvalAction HandleOpMinus(lpc_stack_t *sk)
{
    lpc_value_t *val = sk->top();
    if (val->is_int()) {
        val->set_int(-val->get_int());
    } else if (val->is_float()) {
        val->set_float(-val->get_float());
    }
    return EvalAction::None;
}

static EvalAction HandleOpSetUpvalue(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t *val = sk->pop();
    if (!val) {
        ERROR("error found: stack underflow on set upvalue!");
    }
    call_info_t *cur = lvm->get_call_info();
    if (!cur || !cur->callee || cur->callee->header.type != (lint8_t)value_type::closure_) {
        ERROR("error found: not a closure object to set upvalue!");
    }

    lpc_closure_t *cl = reinterpret_cast<lpc_closure_t *>(cur->callee);
    cl->set(idx, val);
    return EvalAction::None;
}

static EvalAction HandleOpGetUpvalue(lpc_vm_t *lvm, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    call_info_t *cur = lvm->get_call_info();
    if (!cur || !cur->callee || cur->callee->header.type != (lint8_t)value_type::closure_) {
        ERROR("error found: not a closure object to get upvalue!");
    }

    lpc_closure_t *cl = reinterpret_cast<lpc_closure_t *>(cur->callee);
    lpc_value_t *v = cl->get(idx);
    if (!v) {
        ERROR("error found: invalid upvalue index!");
    }
    sk->push(v);
    return EvalAction::None;
}

static EvalAction HandleOpForeachStep1(lpc_vm_t *lvm, lpc_stack_t *sk)
{
    lpc_value_t *val = sk->top();
    if (!val->is_array() && !val->is_mapping()) {
        std::string msg = "cant traverse type: " + std::to_string((lint32_t)val->type());
        ERROR(msg);
    }
    lpc_value_t iter;
    iter.set_int(0);
    sk->push_value(iter);
    return EvalAction::None;
}

static EvalAction HandleOpForeachStep2(lpc_vm_t *lvm, lpc_stack_t *sk, const char *start, const char *&pc)
{
    lpc_value_t *iter = sk->pop();
    if (!iter) {
        ERROR("error found: stack underflow on foreach iterator!");
    }
    lpc_value_t *val = sk->top();
    if (!val) {
        ERROR("error found: stack underflow on foreach container!");
    }
    lint8_t sz = *(pc++);
    luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 4;

    if (!val->is_array() && !val->is_mapping()) {
        ERROR("only array or mapping container can be traversed!!");
    }

    if (val->is_array() && sz != 1) {
        ERROR("not array container to traverse!!");
    }

    if (val->is_mapping() && sz != 2) {
        ERROR("not mapping container to traverse!!");
    }

    lint32_t index = iter->get_int();
    iter->set_int(index + 1);

    if (val->is_mapping()) {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(val->get_gcobj());
        if (index >= map->get_size()) {
            sk->pop();
            pc = start + idx;
            map->reset_iterator();
            return EvalAction::None;
        }
        bucket_t *b = map->iterate(index);
        sk->push(iter);
        sk->push(&b->pair[1]);
        sk->push(&b->pair[0]);
        return EvalAction::None;
    }

    lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->get_gcobj());
    if (index >= arr->get_size()) {
        sk->pop();
        pc = start + idx;
        return EvalAction::None;
    }

    sk->push(iter);
    lpc_value_t *field = arr->get(index);
    sk->push(field);
    return EvalAction::None;
}

static EvalAction HandleOpLoad0(lpc_stack_t *sk)
{
    lpc_value_t result;
    result.set_int(0);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpLoad1(lpc_stack_t *sk)
{
    lpc_value_t result;
    result.set_int(1);
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpBinaryNot(lpc_vm_t *lvm, lpc_stack_t *sk)
{
    lpc_value_t *v1 = sk->top();
    if (!v1) {
        ERROR("error found: stack underflow on bitwise not!");
    }
    if (v1->is_int()) {
        v1->set_int(~v1->get_int());
    } else {
        ERROR("error found on oper ~ !");
    }
    return EvalAction::None;
}

static EvalAction HandleOpLoadGlobal(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    if (idx >= ci->cur_obj->get_proto()->nvariable || idx < 0) {
        std::string msg = "invalid number to indexing glable varibal: " + std::to_string(idx);
        ERROR(msg);
    }
    lpc_value_t *val = &ci->cur_obj->get_locals()[idx + ci->inherit_offset];
    sk->push(val);
    return EvalAction::None;
}

static EvalAction HandleOpStoreGlobal(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    if (idx >= ci->cur_obj->get_proto()->nvariable || idx < 0) {
        std::string msg = "invalid number to indexing glable varibal: " + std::to_string(idx);
        ERROR(msg);
    }
    lpc_value_t *val = &ci->cur_obj->get_locals()[idx + ci->inherit_offset];
    lpc_value_t *val1 = sk->pop();
    if (!val1) {
        ERROR("error found: stack underflow on store global!");
    }
    *val = *val1;
    return EvalAction::None;
}

static EvalAction HandleOpLoadLocal(lpc_vm_t *lvm, call_info_t *ci, function_proto_t *fun, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    if (idx >= fun->nlocal || idx < 0) {
        std::string msg = "invalid number to indexing local varibal: " + std::to_string(idx);
        ERROR(msg);
    }
    sk->push(ci->base + idx);
    return EvalAction::None;
}

static EvalAction HandleOpStoreLocal(lpc_vm_t *lvm, call_info_t *ci, function_proto_t *fun, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    if (idx >= fun->nlocal || idx < 0) {
        std::string msg = "invalid number to indexing local varibal: " + std::to_string(idx);
        ERROR(msg);
    }
    lpc_value_t *val = ci->base + idx;
    lpc_value_t *val1 = sk->pop();
    if (!val1) {
        ERROR("error found: stack underflow on store local!");
    }
    *val = *val1;
    return EvalAction::None;
}

static EvalAction HandleOpLoadIConst(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t result;
    if (ci->father) {
        if ((ci->father->niconst <= idx || idx < 0)) {
            ERROR("error found: int const index over range!!!");
        }
        result.set_int(ci->father->iconst[idx].item.number);
    } else {
        if ((ci->cur_obj->get_proto()->niconst <= idx || idx < 0)) {
            ERROR("error found: int const index over range!!!");
        }
        result.set_int(ci->cur_obj->get_proto()->iconst[idx].item.number);
    }
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpLoadFConst(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t result;
    if (ci->father) {
        if ((ci->father->nfconst <= idx || idx < 0)) {
            ERROR("error found: float const index over range!!!");
        }
        result.set_float(ci->father->fconst[idx].item.real);
    } else {
        if ((ci->cur_obj->get_proto()->nfconst <= idx || idx < 0)) {
            ERROR("error found: float const index over range!!!");
        }
        result.set_float(ci->cur_obj->get_proto()->fconst[idx].item.real);
    }
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpLoadSConst(lpc_vm_t *lvm, call_info_t *ci, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    lpc_value_t result;
    if (ci->father) {
        if ((ci->father->nsconst <= idx || idx < 0)) {
            ERROR("error found: string const index over range!!!");
        }
        lpc_string_t *s = ci->father->sconst[idx].item.str;
        result.set_string(reinterpret_cast<lpc_gc_object_t *>(s));
    } else {
        if ((ci->cur_obj->get_proto()->nsconst <= idx || idx < 0)) {
            ERROR("error found: string const index over range!!!");
        }
        lpc_string_t *s = ci->cur_obj->get_proto()->sconst[idx].item.str;
        result.set_string(reinterpret_cast<lpc_gc_object_t *>(s));
    }
    sk->push_value(result);
    return EvalAction::None;
}

static EvalAction HandleOpLoadFunc(lpc_vm_t *lvm, call_info_t *ci, function_proto_t *fun, lpc_stack_t *sk, const char *&pc)
{
    luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
    pc += 2;
    object_proto_t *proto = ci->cur_obj->get_proto();
    if (proto->nfunction <= idx || idx < 0) {
        ERROR("error found: function index over range!!!");
    }

    lpc_value_t result;
    function_proto_t *f = &proto->func_table[idx];
    if (f->nupvalue > 0) {
        lpc_closure_t *cl = lvm->get_alloc()->allocate_closure(f, ci->cur_obj);
        cl->idx = idx;
        for (int ui = 0; ui < f->nupvalue; ++ui) {
            lint16_t skind = f->upvalue_source_kind ? f->upvalue_source_kind[ui] : upvalue_from_parent_local;
            lint16_t sidx = f->upvalue_source_index ? f->upvalue_source_index[ui] : -1;
            lpc_value_t bound;
            bound.set_null();

            if (skind == upvalue_from_parent_local) {
                if (sidx >= 0 && sidx < fun->nlocal) {
                    bound = *(ci->base + sidx);
                }
            } else {
                if (ci->callee && ci->callee->header.type == (lint8_t)value_type::closure_) {
                    lpc_closure_t *parent = reinterpret_cast<lpc_closure_t *>(ci->callee);
                    lpc_value_t *pv = parent->get(sidx);
                    if (pv) {
                        bound = *pv;
                    }
                }
            }
            cl->set(ui, &bound);
        }
        result.set_closure(reinterpret_cast<lpc_gc_object_t *>(cl));
    } else {
        lpc_function_t *func = lvm->get_alloc()->allocate_function(f, ci->cur_obj, idx);
        result.set_function(reinterpret_cast<lpc_gc_object_t *>(func));
    }
    sk->push_value(result);
    return EvalAction::None;
}

void vm::eval(lpc_vm_t *lvm)
{
    call_info_t *ci = nullptr;
    lpc_stack_t *sk = lvm->get_stack();
    function_proto_t *fun = nullptr;
    const char *pc = nullptr;
    const char *start = nullptr;
    CatchContext catch_ctx;

    auto reload_frame = [&]() -> bool {
        ci = lvm->get_call_info();
        if (!ci) {
            return false;
        }
        const function_proto_t *ff = lvm->get_frame_function(ci);
        fun = const_cast<function_proto_t *>(ff);
        start = lvm->get_frame_code_base(ci);
        if (!ci->call_init && fun) {
            start += fun->fromPC;
        }
        pc = ci->savepc;
        catch_ctx.active = false;
        catch_ctx.end_pc = nullptr;
        catch_ctx.prev_non_fatal = false;
        return fun && start && pc;
    };

    if (!reload_frame()) {
        return;
    }

    for (;;) {
        EvalAction action = EvalAction::None;
        if (lvm->is_debug() && !lvm->check_run()) {
            ci->savepc = pc;
            break;
        }

        try {
            OpCode op = (OpCode)*(pc++);
            if (lvm->is_profiling()) {
                lvm->get_profiler()->CountOp(op);
            }
            switch (op)
            {
        case OpCode::op_load_global: {
            action = HandleOpLoadGlobal(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_store_global: {
            action = HandleOpStoreGlobal(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_load_local: {
            action = HandleOpLoadLocal(lvm, ci, fun, sk, pc);
            break;
        }
        case OpCode::op_store_local: {
            action = HandleOpStoreLocal(lvm, ci, fun, sk, pc);
            break;
        }
        case OpCode::op_load_iconst: {
            action = HandleOpLoadIConst(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_load_fconst: {
            action = HandleOpLoadFConst(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_load_sconst: {
            action = HandleOpLoadSConst(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_load_func: {
            action = HandleOpLoadFunc(lvm, ci, fun, sk, pc);
            break;
        }
        case OpCode::op_load_0: {
            action = HandleOpLoad0(sk);
            break;
        }
        case OpCode::op_load_1: {
            action = HandleOpLoad1(sk);
            break;
        }
        case OpCode::op_add: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Add);
            break;
        }
        case OpCode::op_sub: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Sub);
            break;
        }
        case OpCode::op_mul: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Mul);
            break;
        }
        case OpCode::op_div: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Div);
            break;
        }
        case OpCode::op_mod: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Mod);
            break;
        }
        case OpCode::op_binary_lm: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Shl);
            break;
        }
        case OpCode::op_binary_rm: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::Shr);
            break;
        }
        case OpCode::op_binary_and: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::BitAnd);
            break;
        }
        case OpCode::op_binary_or: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::BitOr);
            break;
        }
        case OpCode::op_binary_not: {
            action = HandleOpBinaryNot(lvm, sk);
            break;
        }
        case OpCode::op_binary_xor: {
            action = HandleOpNumericBinary(lvm, sk, NumericBinOp::BitXor);
            break;
        }
        case OpCode::op_inc: {
            action = HandleOpInc(sk);
            break;
        }
        case OpCode::op_dec: {
            action = HandleOpDec(sk);
            break;
        }
        case OpCode::op_minus: {
            action = HandleOpMinus(sk);
            break;
        }

        case OpCode::op_cmp_and: {
            action = HandleOpBinaryLogicAnd(sk);
            break;
        }
        case OpCode::op_cmp_or: {
            action = HandleOpBinaryLogicOr(sk);
            break;
        }
        case OpCode::op_cmp_not: {
            action = HandleOpLogicNot(sk);
            break;
        }
        case OpCode::op_cmp_eq: {
            action = HandleOpCompare(lvm, sk, CompareOp::Eq);
            break;
        }
        case OpCode::op_cmp_neq: {
            action = HandleOpCompare(lvm, sk, CompareOp::Neq);
            break;
        }
        case OpCode::op_cmp_gt: {
            action = HandleOpCompare(lvm, sk, CompareOp::Gt);
            break;
        }
        case OpCode::op_cmp_gte: {
            action = HandleOpCompare(lvm, sk, CompareOp::Gte);
            break;
        }
        case OpCode::op_cmp_lt: {
            action = HandleOpCompare(lvm, sk, CompareOp::Lt);
            break;
        }
        case OpCode::op_cmp_lte: {
            action = HandleOpCompare(lvm, sk, CompareOp::Lte);
            break;
        }
        case OpCode::op_or: {
            action = HandleOpOr(sk);
            break;
        }
        case OpCode::op_test: {
            action = HandleOpTest(lvm, sk, start, pc);
            break;
        }
        case OpCode::op_test_not: {
            lpc_value_t *val = sk->pop();
            if (!val) {
                ERROR("error found: stack underflow on test_not!");
            }
            luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
            pc += 4;
            if (!val->is_falsy()) {
                pc = start + idx;
            }
            action = EvalAction::None;
            break;
        }
        case OpCode::op_index: {
            action = HandleOpIndex(lvm, sk);
            break;
        }
        case OpCode::op_new_array: {
            action = HandleOpNewArray(lvm, sk, pc);
            break;
        }
        case OpCode::op_sub_arr: {
            action = HandleOpSubArr(lvm, sk);
            break;
        }
        case OpCode::op_new_mapping: {
            action = HandleOpNewMapping(lvm, sk, pc);
            break;
        }
        case OpCode::op_upset: {
            action = HandleOpUpset(lvm, sk, pc);
            break;
        }
        case OpCode::op_call: {
            action = HandleOpCall(lvm, ci, sk, pc);
            if (action != EvalAction::None) {
                break;
            }
            break;
        }
        case OpCode::op_call_virtual: {
            action = HandleOpCallVirtual(lvm, ci, pc);
            if (action != EvalAction::None) {
                break;
            }
            break;
        }
        case OpCode::op_return: {
            action = HandleOpReturn(lvm, ci);
            if (action != EvalAction::None) {
                break;
            }
            break;
        }
        case OpCode::op_set_upvalue: {
            action = HandleOpSetUpvalue(lvm, sk, pc);
            break;
        }
        case OpCode::op_get_upvalue: {
            action = HandleOpGetUpvalue(lvm, sk, pc);
            break;
        }
        case OpCode::op_new_class: {
            action = HandleOpNewClass(lvm, ci, sk, pc);
            break;
        }
        case OpCode::op_set_class_field: {
            action = HandleOpSetClassField(lvm, sk, pc);
            break;
        }
        case OpCode::op_load_class_field: {
            action = HandleOpLoadClassField(lvm, sk, pc);
            break;
        }
        case OpCode::op_goto: {
            action = HandleOpGoto(start, pc);
            break;
        }
        case OpCode::op_switch: {
            action = HandleOpSwitch(lvm, ci, sk, start, pc);
            break;
        }
        case OpCode::op_foreach_step1: {
            action = HandleOpForeachStep1(lvm, sk);
            break;
        }
        case OpCode::op_foreach_step2: {
            action = HandleOpForeachStep2(lvm, sk, start, pc);
            break;
        }
        case OpCode::op_pop: {
            if (!sk->pop()) {
                ERROR("error found: stack underflow on pop!");
            }
            action = EvalAction::None;
            break;
        }
        case OpCode::op_store_index: {
            action = HandleOpStoreIndex(lvm, sk);
            break;
        }
        case OpCode::op_dup: {
            lpc_value_t *val = sk->top();
            if (!val) {
                ERROR("error found: stack underflow on dup!");
            }
            sk->push_value(*val);
            action = EvalAction::None;
            break;
        }
        case OpCode::op_catch: {
            action = HandleOpCatch(start, pc, catch_ctx);
            catch_ctx.prev_non_fatal = lvm->non_fatal_mode();
            lvm->set_non_fatal_mode(true);
            break;
        }
        default:
            std::string msg = "unkown instrucion found: " + std::to_string((int)op);
            ERROR(msg);
            break;
        }
        } catch (const vm_abort_signal &) {
            if (catch_ctx.active && catch_ctx.end_pc) {
                pc = catch_ctx.end_pc;
                lvm->set_non_fatal_mode(catch_ctx.prev_non_fatal);
                catch_ctx.active = false;
                catch_ctx.end_pc = nullptr;
                catch_ctx.prev_non_fatal = false;
                HandleCatchAbort(sk);
                continue;
            }
            return;
        }

        if (action == EvalAction::ReloadFrame) {
            if (!reload_frame()) {
                return;
            }
            continue;
        }
        if (action == EvalAction::Stop) {
            return;
        }

        if (catch_ctx.active && catch_ctx.end_pc && pc >= catch_ctx.end_pc) {
            lvm->set_non_fatal_mode(catch_ctx.prev_non_fatal);
            catch_ctx.active = false;
            catch_ctx.end_pc = nullptr;
            catch_ctx.prev_non_fatal = false;
            lpc_value_t ok;
            ok.set_int(0);
            sk->push_value(ok);
        }
    }

    if (catch_ctx.active) {
        lvm->set_non_fatal_mode(catch_ctx.prev_non_fatal);
    }
}
