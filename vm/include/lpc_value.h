#ifndef __LPC_VALUE__
#define __LPC_VALUE__
#include <cstring>
#include "lpc.h"
#include "type/lpc_array.h"
#include "type/lpc_string.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"
#include "type/lpc_closure.h"
#include "type/lpc_function.h"
#include "type/lpc_buffer.h"
#include "type/lpc_proto.h"
#include <cstring>

enum class value_type : lint8_t
{
    null_,
    byte_,
    int_,
    bool_,
    float_,
    buffer_,
    string_,
    array_,
    mapping_,
    object_,
    function_,
    closure_,
    proto_,
    class_,
    return_,
};

union lpc_gc_object_t
{
    gc_header head;
    lpc_string_t   str;
    lpc_array_t    arr;
    lpc_mapping_t  map;
    lpc_function_t fun;
    lpc_closure_t  clo;
    lpc_object_t   obj;
    lpc_buffer_t   buf;
    object_proto_t pro;
};

namespace nanbox {
    static constexpr luint64_t BASE       = 0x7FF8000000000000ULL;
    static constexpr luint64_t TAG_SHIFT  = 47;
    static constexpr luint64_t PAYLOAD    = 0x00007FFFFFFFFFFFULL;
    static constexpr luint64_t RETURN_BIT = 0x8000000000000000ULL;

    enum tag_t : luint64_t {
        null_      = 0,
        int_       = 1,
        undefined_ = 2,
        bool_      = 3,
        float_     = 4,
        string_    = 5,
        array_     = 6,
        class_     = 7,
        mapping_   = 8,
        object_    = 9,
        function_  = 10,
        closure_   = 11,
        proto_     = 12,
        buffer_    = 13,
        byte_      = 14,
        spare_     = 15,
    };
}

struct lpc_value_t
{
    luint64_t bits = nanbox::BASE;

    luint64_t tag() const {
        return (bits >> nanbox::TAG_SHIFT) & 0xF;
    }

    value_type type() const {
        switch (tag()) {
        case nanbox::null_:      return value_type::null_;
        case nanbox::int_:       return value_type::int_;
        case nanbox::undefined_: return value_type::int_;
        case nanbox::bool_:      return value_type::bool_;
        case nanbox::float_:     return value_type::float_;
        case nanbox::string_:    return value_type::string_;
        case nanbox::array_:     return value_type::array_;
        case nanbox::class_:     return value_type::array_;
        case nanbox::mapping_:   return value_type::mapping_;
        case nanbox::object_:    return value_type::object_;
        case nanbox::function_:  return value_type::function_;
        case nanbox::closure_:   return value_type::closure_;
        case nanbox::proto_:     return value_type::proto_;
        case nanbox::buffer_:    return value_type::buffer_;
        case nanbox::byte_:      return value_type::byte_;
        default:                 return value_type::null_;
        }
    }

    value_type subtype() const {
        if (bits & nanbox::RETURN_BIT) return value_type::return_;
        switch (tag()) {
        case nanbox::undefined_: return value_type::null_;
        case nanbox::null_:      return value_type::null_;
        case nanbox::class_:     return value_type::class_;
        default:                 return value_type::int_;
        }
    }

    void set_subtype(value_type st) {
        if (st == value_type::return_) {
            bits |= nanbox::RETURN_BIT;
        } else if (st == value_type::class_) {
            luint64_t p = bits & nanbox::PAYLOAD;
            bits = (bits & nanbox::RETURN_BIT) | nanbox::BASE | (nanbox::class_ << nanbox::TAG_SHIFT) | p;
        } else if (st == value_type::null_) {
            if (tag() == nanbox::int_ || tag() == nanbox::undefined_) {
                luint64_t p = bits & nanbox::PAYLOAD;
                bits = nanbox::BASE | (nanbox::undefined_ << nanbox::TAG_SHIFT) | p;
            } else {
                bits = nanbox::BASE | (nanbox::null_ << nanbox::TAG_SHIFT);
            }
        }
    }

    bool is_null() const { return tag() == nanbox::null_; }
    bool is_undefined() const { return tag() == nanbox::undefined_; }
    bool is_int() const { return tag() == nanbox::int_ || tag() == nanbox::undefined_; }
    bool is_float() const { return tag() == nanbox::float_; }
    bool is_bool() const { return tag() == nanbox::bool_; }
    bool is_number() const { return is_int() || is_float(); }
    bool is_string() const { return tag() == nanbox::string_; }
    bool is_array() const { return tag() == nanbox::array_ || tag() == nanbox::class_; }
    bool is_class() const { return tag() == nanbox::class_; }
    bool is_mapping() const { return tag() == nanbox::mapping_; }
    bool is_object() const { return tag() == nanbox::object_; }
    bool is_function() const { return tag() == nanbox::function_; }
    bool is_closure() const { return tag() == nanbox::closure_; }
    bool is_gc_type() const { return tag() >= nanbox::string_ && tag() <= nanbox::buffer_; }
    bool has_return_marker() const { return (bits & nanbox::RETURN_BIT) != 0; }

    bool is_falsy() const {
        if (is_null() || is_undefined()) return true;
        if (is_int()) return get_int() == 0;
        if (is_float()) return get_float() == 0.0f;
        return false;
    }

    int get_int() const {
        return static_cast<int>(static_cast<luint32_t>(bits));
    }

    float get_float() const {
        luint32_t fb = static_cast<luint32_t>(bits);
        float f;
        std::memcpy(&f, &fb, sizeof(f));
        return f;
    }

    bool get_bool() const { return (bits & 1) != 0; }

    lpc_gc_object_t *get_gcobj() const {
        return reinterpret_cast<lpc_gc_object_t *>(bits & nanbox::PAYLOAD);
    }

    void set_int(int val) {
        bits = nanbox::BASE | (nanbox::int_ << nanbox::TAG_SHIFT) | static_cast<luint64_t>(static_cast<luint32_t>(val));
    }

    void set_float(float val) {
        luint32_t fb;
        std::memcpy(&fb, &val, sizeof(fb));
        bits = nanbox::BASE | (nanbox::float_ << nanbox::TAG_SHIFT) | fb;
    }

    void set_bool(bool val) {
        bits = nanbox::BASE | (nanbox::bool_ << nanbox::TAG_SHIFT) | (val ? 1ULL : 0ULL);
    }

    void set_null() {
        bits = nanbox::BASE | (nanbox::null_ << nanbox::TAG_SHIFT);
    }

    void set_undefined() {
        bits = nanbox::BASE | (nanbox::undefined_ << nanbox::TAG_SHIFT);
    }

    void set_gcobj(nanbox::tag_t t, lpc_gc_object_t *obj) {
        bits = nanbox::BASE | (static_cast<luint64_t>(t) << nanbox::TAG_SHIFT) | (reinterpret_cast<luint64_t>(obj) & nanbox::PAYLOAD);
    }

    void set_string(lpc_gc_object_t *obj) { set_gcobj(nanbox::string_, obj); }
    void set_array(lpc_gc_object_t *obj) { set_gcobj(nanbox::array_, obj); }
    void set_class(lpc_gc_object_t *obj) { set_gcobj(nanbox::class_, obj); }
    void set_mapping(lpc_gc_object_t *obj) { set_gcobj(nanbox::mapping_, obj); }
    void set_object(lpc_gc_object_t *obj) { set_gcobj(nanbox::object_, obj); }
    void set_function(lpc_gc_object_t *obj) { set_gcobj(nanbox::function_, obj); }
    void set_closure(lpc_gc_object_t *obj) { set_gcobj(nanbox::closure_, obj); }

    static lpc_value_t make_null() {
        lpc_value_t v;
        v.bits = nanbox::BASE;
        return v;
    }

    static lpc_value_t make_undefined() {
        lpc_value_t v;
        v.bits = nanbox::BASE | (nanbox::undefined_ << nanbox::TAG_SHIFT);
        return v;
    }

    static lpc_value_t make_int(int val) {
        lpc_value_t v;
        v.set_int(val);
        return v;
    }

    static lpc_value_t make_float(float val) {
        lpc_value_t v;
        v.set_float(val);
        return v;
    }

    static lpc_value_t make_bool(bool val) {
        lpc_value_t v;
        v.set_bool(val);
        return v;
    }

    static lpc_value_t make_string(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_string(obj);
        return v;
    }

    static lpc_value_t make_array(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_array(obj);
        return v;
    }

    static lpc_value_t make_class(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_class(obj);
        return v;
    }

    static lpc_value_t make_mapping(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_mapping(obj);
        return v;
    }

    static lpc_value_t make_object(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_object(obj);
        return v;
    }

    static lpc_value_t make_function(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_function(obj);
        return v;
    }

    static lpc_value_t make_closure(lpc_gc_object_t *obj) {
        lpc_value_t v;
        v.set_closure(obj);
        return v;
    }
};

enum class ArithBinOp { Add, Sub, Mul, Div };

static inline bool arith_binop(lpc_value_t *val, lpc_value_t *v, ArithBinOp op)
{
    if (val->is_int()) {
        int rhs = v->is_int() ? v->get_int() : static_cast<int>(v->get_float());
        int lhs = val->get_int();
        int result;
        switch (op) {
        case ArithBinOp::Add: result = lhs + rhs; break;
        case ArithBinOp::Sub: result = lhs - rhs; break;
        case ArithBinOp::Mul: result = lhs * rhs; break;
        case ArithBinOp::Div: result = lhs / rhs; break;
        }
        val->set_int(result);
    } else if (val->is_float()) {
        float rhs = v->is_int() ? static_cast<float>(v->get_int()) : v->get_float();
        float lhs = val->get_float();
        float result;
        switch (op) {
        case ArithBinOp::Add: result = lhs + rhs; break;
        case ArithBinOp::Sub: result = lhs - rhs; break;
        case ArithBinOp::Mul: result = lhs * rhs; break;
        case ArithBinOp::Div: result = lhs / rhs; break;
        }
        val->set_float(result);
    } else if (val->is_null()) {
        if (v->is_int() || v->is_float()) {
            *val = *v;
        } else {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

#endif
