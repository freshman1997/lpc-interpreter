#include "vm/runtime/vm.h"
#include "vm/value/objref.h"
#include "vm/value/mapping.h"
#include "vm/value/lpc_array.h"
#include "vm/value/hash.h"
#include "vm/runtime/efun.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <cmath>
#include <ctime>
#include <array>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>

using namespace lpc::vm;

static std::string FormatValue(const Value &v, Vm &vm, bool nested = false, int depth = 0) {
    const Chunk &chunk = vm.BoundChunk();
    const std::vector<std::string> &string_heap = vm.string_heap();
    if (depth > 4) return "...";

    switch (v.Tag()) {
    case ValueTag::Nil: return "0";
    case ValueTag::Bool: return v.AsBool() ? "1" : "0";
    case ValueTag::Int64: return std::to_string(vm.GetI64(v));
    case ValueTag::BoxedInt: return std::to_string(vm.GetI64(v));
    case ValueTag::Float64: {
        std::ostringstream oss;
        oss << v.AsF64();
        return oss.str();
    }
    case ValueTag::ObjRef: {
        std::uintptr_t raw = v.AsObj();
        if (raw > 0 && raw < kFuncBase) {
            if (raw & 1) {
                std::uint32_t sidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (sidx < chunk.sconst.size()) {
                    return nested ? ("\"" + chunk.sconst[sidx] + "\"") : chunk.sconst[sidx];
                }
            } else {
                std::uint32_t hidx = static_cast<std::uint32_t>(raw >> 1) - 1;
                if (hidx < string_heap.size()) {
                    return nested ? ("\"" + string_heap[hidx] + "\"") : string_heap[hidx];
                }
            }
        }
        if (raw >= kArrayBase && raw < kMappingBase) {
            std::size_t sz = vm.GetArraySize(v);
            std::string out = "({";
            for (std::size_t i = 0; i < sz; ++i) {
                if (i > 0) out += ", ";
                out += FormatValue(vm.GetArrayElement(v, static_cast<std::int64_t>(i)), vm, true, depth + 1);
            }
            out += "})";
            return out;
        }
        if (raw >= kMappingBase && raw < kClassBase) {
            auto pairs = vm.GetMappingPairs(v);
            std::string out = "([";
            for (std::size_t i = 0; i < pairs.size(); ++i) {
                if (i > 0) out += ", ";
                out += FormatValue(pairs[i].first, vm, true, depth + 1);
                out += ": ";
                out += FormatValue(pairs[i].second, vm, true, depth + 1);
            }
            out += "])";
            return out;
        }
        if (raw >= kClassBase && raw < kObjectBase) {
            const auto &names = vm.GetObjectFieldNames(v);
            std::string out = "class {";
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) out += ", ";
                out += names[i];
                out += ": ";
                out += FormatValue(vm.GetObjectField(v, names[i]), vm, true, depth + 1);
            }
            out += "}";
            return out;
        }
        if (raw >= kObjectBase) return "<object:" + std::to_string(DecodeObjectId(v)) + ">";
        return "<ref>";
    }
    case ValueTag::Closure: return "<closure>";
    }
    return "<unknown>";
}

static std::string FormatCtimeSafe(std::time_t t) {
    std::array<char, 64> buf{};
#if defined(_WIN32)
    if (ctime_s(buf.data(), buf.size(), &t) != 0) return "";
    std::string s(buf.data());
#elif defined(__unix__) || defined(__APPLE__)
    if (ctime_r(&t, buf.data()) == nullptr) return "";
    std::string s(buf.data());
#else
    char *ct = std::ctime(&t);
    std::string s(ct ? ct : "");
#endif
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static void IntrinsicPrint(Vm &vm, const std::vector<Value> &args, const Chunk &chunk,
                            const std::vector<std::string> &string_heap) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += " ";
        out += FormatValue(args[i], vm);
    }
    out += "\n";
    vm.EmitOutput(out);
}

static void IntrinsicPuts(Vm &vm, const std::vector<Value> &args, const Chunk &chunk,
                           const std::vector<std::string> &string_heap) {
    if (!args.empty()) {
        vm.EmitOutput(FormatValue(args[0], vm) + "\n");
    }
}

static LpcArray *GetArrayObjRef(const Value &v,
                                                std::vector<LpcArray> &arrays_) {
    std::size_t arr_id = DecodeArrayId(v);
    if (arr_id == 0 || arr_id > arrays_.size()) return nullptr;
    return &arrays_[arr_id - 1];
}

static Mapping *GetMappingObjRef(
    const Value &v,
    std::vector<Mapping> &mappings_) {
    std::size_t map_id = DecodeMappingId(v);
    if (map_id == 0 || map_id > mappings_.size()) return nullptr;
    return &mappings_[map_id - 1];
}

Value Vm::DispatchIntrinsic(std::uint16_t efun_idx, std::uint8_t argc) {
    if (argc > value_stack_.size()) {
        return Value::Nil();
    }

    const Efun efun = static_cast<Efun>(efun_idx);
    if (efun == Efun::ThisObject && argc == 0) {
        if (current_object_id_ > 0 && current_object_id_ <= objects_.size()) {
            return MakeObjectHandle(current_object_id_);
        }
        return Value::Nil();
    }
    if (efun == Efun::Typeof && argc == 1) {
        Value v = value_stack_.back();
        value_stack_.pop_back();
        return MakeI64(static_cast<std::int64_t>(v.Tag()));
    }
    if (efun == Efun::ToInt && argc == 1) {
        Value v = value_stack_.back();
        value_stack_.pop_back();
        if (v.IsBool()) return MakeI64(v.AsI64());
        if (v.Tag() == ValueTag::Int64) return v;
        if (v.IsFloat64()) return MakeI64(static_cast<std::int64_t>(v.AsF64()));
        if (v.IsNil()) return MakeI64(0);
        return MakeI64(0);
    }
    if (efun == Efun::ToString && argc == 1) {
        Value v = value_stack_.back();
        value_stack_.pop_back();
        std::string s;
        if (v.Tag() == ValueTag::Int64) {
            s = std::to_string(GetI64(v));
        } else if (v.IsBool()) {
            s = v.AsI64() ? "1" : "0";
        } else if (v.IsFloat64()) {
            s = std::to_string(v.AsF64());
        } else if (v.IsNil()) {
            s = "0";
        } else if (v.IsObjRef()) {
            s = ResolveObjRefStringOnly(v);
        }
        if (!s.empty()) {
            return InternString(s);
        }
        return Value::Nil();
    }
    if (efun == Efun::Instanceof && argc == 2) {
        Value rhs = value_stack_.back();
        value_stack_.pop_back();
        Value lhs = value_stack_.back();
        value_stack_.pop_back();
        if (!IsClassObjRef(lhs)) return MakeI64(0);
        std::uint16_t cur = 0;
        if (!ResolveClassTemplateIndex(lhs, &cur)) return MakeI64(0);
        std::string target_name;
        if (rhs.IsObjRef() && IsStringObjRefFull(rhs)) {
            target_name = ResolveObjRefStringOnly(rhs);
        } else if (rhs.Tag() == ValueTag::Int64) {
            std::uint16_t idx = static_cast<std::uint16_t>(GetI64(rhs));
            if (idx < BoundChunk().classes.size()) target_name = BoundChunk().classes[idx].name;
        }
        if (target_name.empty()) return MakeI64(0);
        while (cur != 0xFFFF && cur < BoundChunk().classes.size()) {
            if (BoundChunk().classes[cur].name == target_name) return MakeI64(1);
            cur = BoundChunk().classes[cur].parent_class_idx;
        }
        return MakeI64(0);
    }

    std::vector<Value> args(argc);
    for (std::uint8_t i = argc; i > 0; --i) {
        args[i - 1] = value_stack_.back();
        value_stack_.pop_back();
    }

    switch (efun) {
    case Efun::CallOther: {
        return Value::Nil();
    }
    case Efun::Print: IntrinsicPrint(*this, args, BoundChunk(), string_heap_); break;
    case Efun::Puts: IntrinsicPuts(*this, args, BoundChunk(), string_heap_); break;
    case Efun::Sleep: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            std::this_thread::sleep_for(std::chrono::milliseconds(GetI64(args[0])));
        }
        return Value::Nil();
    }
    case Efun::Sizeof: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) {
                    std::string s = ResolveObjRefStringOnly(args[0]);
                    return MakeI64(static_cast<std::int64_t>(s.size()));
                }
                auto *arr = GetArrayObjRef(args[0], arrays_);
                if (arr) return MakeI64(static_cast<std::int64_t>(arr->Size()));
                auto *map = GetMappingObjRef(args[0], mappings_);
                if (map) return MakeI64(static_cast<std::int64_t>(map->Size()));
            }
            if (args[0].Tag() == ValueTag::Int64) {
                return MakeI64(GetI64(args[0]) == 0 ? 0 : 1);
            }
        }
        return MakeI64(0);
    }
    case Efun::Random: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64 && GetI64(args[0]) > 0) {
            std::int64_t n = GetI64(args[0]);
            thread_local std::mt19937_64 rng{static_cast<std::uint64_t>(std::time(nullptr))};
            std::uniform_int_distribution<std::int64_t> dist(0, n - 1);
            return MakeI64(dist(rng));
        }
        return MakeI64(0);
    }
    case Efun::Keys: {
        if (!args.empty()) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                std::vector<Value> keys;
                map->ForEachEntry([&](const Value &k, const Value &) {
                    keys.push_back(k);
                });
                return AllocateArrayHandle(std::move(keys));
            }
        }
        return Value::Nil();
    }
    case Efun::Values: {
        if (!args.empty()) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                std::vector<Value> vals;
                map->ForEachEntry([&](const Value &, const Value &v) {
                    vals.push_back(v);
                });
                return AllocateArrayHandle(std::move(vals));
            }
        }
        return Value::Nil();
    }
    case Efun::Typeof: {
        if (!args.empty()) {
            return MakeI64(static_cast<std::int64_t>(args[0].Tag()));
        }
        return MakeI64(0);
    }
    case Efun::ToString: {
        if (!args.empty()) {
            std::string s;
            if (args[0].Tag() == ValueTag::Int64) {
                s = std::to_string(GetI64(args[0]));
            } else if (args[0].IsBool()) {
                s = args[0].AsI64() ? "1" : "0";
            } else if (args[0].IsFloat64()) {
                s = std::to_string(args[0].AsF64());
            } else if (args[0].IsNil()) {
                s = "0";
            } else if (args[0].IsObjRef()) {
                s = ResolveObjRefStringOnly(args[0]);
            }
            if (!s.empty()) {
                return InternString(s);
            }
        }
        return Value::Nil();
    }
    case Efun::ToInt: {
        if (!args.empty()) {
            if (args[0].IsBool()) return MakeI64(args[0].AsI64());
            if (args[0].Tag() == ValueTag::Int64) return args[0];
            if (args[0].IsFloat64())
                return MakeI64(static_cast<std::int64_t>(args[0].AsF64()));
            if (args[0].IsNil()) return MakeI64(0);
        }
        return MakeI64(0);
    }
    case Efun::ThisObject: {
        if (current_object_id_ > 0 && current_object_id_ <= objects_.size()) {
            return MakeObjectHandle(current_object_id_);
        }
        return Value::Nil();
    }
    case Efun::CloneObject: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string path = ResolveObjRefStringOnly(args[0]);
            Vm::LpcObject obj;
            obj.module_name = path;
            const ModuleRuntimeState *tms = GetModuleState(path);
            if (tms && tms->active_version_id != 0) {
                auto vit = tms->version_runtime_data.find(tms->active_version_id);
                if (vit != tms->version_runtime_data.end()) {
                    obj.module_version_id = tms->active_version_id;
                    obj.blueprint = &vit->second.chunk;
                    obj.globals = vit->second.chunk.globals;
                    obj.destroyed = false;
                    return AllocateObjectHandle(std::move(obj));
                }
            }
            obj.module_version_id = current_module_version_id_;
            obj.blueprint = &BoundChunk();
            obj.globals = BoundChunk().globals;
            obj.destroyed = false;
            return AllocateObjectHandle(std::move(obj));
        }
        return Value::Nil();
    }
    case Efun::Destruct: {
        if (!args.empty()) {
            std::size_t oid = DecodeObjectId(args[0]);
            if (oid > 0 && oid <= objects_.size()) {
                objects_[oid - 1].destroyed = true;
            }
        }
        return Value::Nil();
    }
    case Efun::Sprintf: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string fmt = ResolveObjRefStringOnly(args[0]);
            std::string buf;
            int arg_idx = 1;
            for (std::size_t p = 0; p < fmt.size(); ++p) {
                if (fmt[p] != '%') { buf.push_back(fmt[p]); continue; }
                ++p;
                if (p >= fmt.size()) break;
                if (fmt[p] == '%') { buf.push_back('%'); continue; }
                if (arg_idx >= static_cast<int>(args.size())) { buf.push_back('%'); buf.push_back(fmt[p]); continue; }
                const Value &arg = args[arg_idx];
                ++arg_idx;
                switch (fmt[p]) {
                case 'd': case 'i':
                    if (arg.Tag() == ValueTag::Int64) buf.append(std::to_string(GetI64(arg)));
                    else buf.append("0");
                    break;
                case 'f':
                    if (arg.IsFloat64()) {
                        char tmp[64]; std::snprintf(tmp, sizeof(tmp), "%g", arg.AsF64()); buf.append(tmp);
                    } else if (arg.Tag() == ValueTag::Int64) {
                        char tmp[64]; std::snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(GetI64(arg))); buf.append(tmp);
                    } else buf.append("0");
                    break;
                case 's':
                    buf.append(FormatValue(arg, *this));
                    break;
                default:
                    buf.push_back('%'); buf.push_back(fmt[p]); --arg_idx; break;
                }
            }
            if (!buf.empty()) return InternString(buf);
        }
        return Value::Nil();
    }
    case Efun::Write: {
        if (!args.empty()) {
            EmitOutput(FormatValue(args[0], *this));
        }
        return Value::Nil();
    }
    case Efun::Time: {
        return MakeI64(static_cast<std::int64_t>(std::time(nullptr)));
    }
    case Efun::MemberArray: {
        if (args.size() >= 2) {
            auto *arr = GetArrayObjRef(args[1], arrays_);
            if (arr) {
                for (std::size_t i = 0; i < arr->Size(); ++i) {
                    if (ValuesEqual(args[0], arr->At(i))) return MakeI64(static_cast<std::int64_t>(i));
                }
                return MakeI64(-1);
            }
            if (args[1].IsObjRef() && args[0].IsObjRef()) {
                std::string haystack = ResolveObjRefStringOnly(args[1]);
                std::string needle = ResolveObjRefStringOnly(args[0]);
                std::size_t pos = haystack.find(needle);
                if (pos == std::string::npos) return MakeI64(-1);
                return MakeI64(static_cast<std::int64_t>(pos));
            }
        }
        return MakeI64(-1);
    }
    case Efun::Explode: {
        if (args.size() >= 2 && args[0].IsObjRef() && args[1].IsObjRef()) {
            std::string str = ResolveObjRefStringOnly(args[0]);
            std::string delim = ResolveObjRefStringOnly(args[1]);
            std::vector<Value> parts;
            int dlen = static_cast<int>(delim.size());
            if (dlen == 0) {
                for (char c : str) {
                    std::string ch(1, c);
                    parts.push_back(InternString(ch));
                }
            } else {
                std::size_t pos = 0;
                while (pos < str.size()) {
                    std::size_t found = str.find(delim, pos);
                    if (found == std::string::npos) {
                        parts.push_back(InternString(str.substr(pos)));
                        break;
                    }
                    parts.push_back(InternString(str.substr(pos, found - pos)));
                    pos = found + dlen;
                }
                if (pos == str.size()) {
                    parts.push_back(InternString(""));
                }
            }
            return AllocateArrayHandle(std::move(parts));
        }
        return Value::Nil();
    }
    case Efun::Implode: {
        if (args.size() >= 2) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::string d;
                if (args[1].IsObjRef()) d = ResolveObjRefStringOnly(args[1]);
                std::string buf;
                for (std::size_t i = 0; i < arr->Size(); ++i) {
                    const Value &elem = arr->At(i);
                    if (elem.IsObjRef()) {
                        buf.append(ResolveObjRefStringOnly(elem));
                    } else if (elem.Tag() == ValueTag::Int64) {
                        buf.append(std::to_string(GetI64(elem)));
                    } else if (elem.IsFloat64()) {
                        buf.append(std::to_string(elem.AsF64()));
                    }
                    if (i + 1 < arr->Size()) buf.append(d);
                }
                if (!buf.empty()) return InternString(buf);
            }
        }
        return Value::Nil();
    }
    case Efun::Stringp: {
        if (!args.empty()) return MakeBool(IsStringObjRefFull(args[0]));
        return MakeBool(false);
    }
    case Efun::Intp: {
        if (!args.empty()) return MakeBool(args[0].Tag() == ValueTag::Int64);
        return MakeBool(false);
    }
    case Efun::Floatp: {
        if (!args.empty()) return MakeBool(args[0].IsFloat64());
        return MakeBool(false);
    }
    case Efun::Arrayp: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) return MakeBool(false);
                return MakeBool(GetArrayObjRef(args[0], arrays_) != nullptr);
            }
        }
        return MakeBool(false);
    }
    case Efun::Mappingp: {
        if (!args.empty()) {
            if (args[0].IsObjRef()) {
                if (IsStringObjRefFull(args[0])) return MakeBool(false);
                if (GetArrayObjRef(args[0], arrays_)) return MakeBool(false);
                return MakeBool(GetMappingObjRef(args[0], mappings_) != nullptr);
            }
        }
        return MakeBool(false);
    }
    case Efun::Objectp: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::size_t oid = DecodeObjectId(args[0]);
            if (oid > 0 && oid <= objects_.size() && !objects_[oid - 1].destroyed) {
                return MakeBool(true);
            }
        }
        return MakeBool(false);
    }
    case Efun::Nullp: {
        if (!args.empty()) return MakeBool(args[0].IsNil());
        return MakeBool(true);
    }
    case Efun::Functionp: {
        if (!args.empty()) return MakeBool(args[0].IsClosure());
        return MakeBool(false);
    }
    case Efun::ToFloat: {
        if (!args.empty()) {
            if (args[0].IsFloat64()) return args[0];
            if (args[0].Tag() == ValueTag::Int64 || args[0].IsBool())
                return MakeF64(static_cast<double>(GetI64(args[0])));
            if (args[0].IsObjRef()) {
                std::string s = ResolveObjRefStringOnly(args[0]);
                return MakeF64(std::atof(s.c_str()));
            }
        }
        return MakeF64(0.0);
    }
    case Efun::Abs: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            return MakeI64(GetI64(args[0]) < 0 ? -GetI64(args[0]) : GetI64(args[0]));
        }
        return MakeI64(0);
    }
    case Efun::Strlen: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            return MakeI64(static_cast<std::int64_t>(s.size()));
        }
        return MakeI64(0);
    }
    case Efun::MapDelete: {
        if (args.size() >= 2) {
            auto *map = GetMappingObjRef(args[0], mappings_);
            if (map) {
                map->Erase(args[1]);
            }
        }
        return Value::Nil();
    }
    case Efun::Capitalize: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            if (!s.empty()) s[0] = static_cast<char>(::toupper(static_cast<unsigned char>(s[0])));
            return InternString(s);
        }
        return Value::Nil();
    }
    case Efun::LowerCase: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return InternString(s);
        }
        return Value::Nil();
    }
    case Efun::UpperCase: {
        if (!args.empty() && args[0].IsObjRef()) {
            std::string s = ResolveObjRefStringOnly(args[0]);
            std::string upper;
            upper.resize(s.size());
            std::transform(s.begin(), s.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return InternString(upper);
        }
        return Value::Nil();
    }
    case Efun::Allocate: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64 && GetI64(args[0]) > 0) {
            std::size_t n = static_cast<std::size_t>(GetI64(args[0]));
            std::vector<Value> arr(n, Value::Nil());
            return AllocateArrayHandle(std::move(arr));
        }
        return Value::Nil();
    }
    case Efun::Reverse: {
        if (!args.empty() && args[0].IsObjRef()) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::reverse(arr->begin(), arr->end());
                return args[0];
            }
        }
        return Value::Nil();
    }
    case Efun::Min: {
        if (args.size() >= 2 && args[0].Tag() == ValueTag::Int64 && args[1].Tag() == ValueTag::Int64) {
            return MakeI64(std::min(GetI64(args[0]), GetI64(args[1])));
        }
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) return args[0];
        return MakeI64(0);
    }
    case Efun::Max: {
        if (args.size() >= 2 && args[0].Tag() == ValueTag::Int64 && args[1].Tag() == ValueTag::Int64) {
            return MakeI64(std::max(GetI64(args[0]), GetI64(args[1])));
        }
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) return args[0];
        return MakeI64(0);
    }
    case Efun::Sqrt: {
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) {
            return MakeI64(static_cast<std::int64_t>(std::sqrt(static_cast<double>(GetI64(args[0])))));
        }
        if (!args.empty() && args[0].IsFloat64()) {
            return MakeI64(static_cast<std::int64_t>(std::sqrt(args[0].AsF64())));
        }
        return MakeI64(0);
    }
    case Efun::Ctime: {
        std::time_t t = 0;
        if (!args.empty() && args[0].Tag() == ValueTag::Int64) t = static_cast<std::time_t>(GetI64(args[0]));
        return InternString(FormatCtimeSafe(t));
    }
    case Efun::Strsrch: {
        if (args.size() >= 2 && args[0].IsObjRef() && args[1].IsObjRef()) {
            std::string haystack = ResolveObjRefStringOnly(args[0]);
            std::string needle = ResolveObjRefStringOnly(args[1]);
            std::size_t pos = haystack.find(needle);
            if (pos == std::string::npos) return MakeI64(-1);
            return MakeI64(static_cast<std::int64_t>(pos));
        }
        return MakeI64(-1);
    }
    case Efun::ReplaceString: {
        if (args.size() >= 3 && args[0].IsObjRef() && args[1].IsObjRef() && args[2].IsObjRef()) {
            std::string str = ResolveObjRefStringOnly(args[0]);
            std::string from = ResolveObjRefStringOnly(args[1]);
            std::string to = ResolveObjRefStringOnly(args[2]);
            std::size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.size(), to);
                pos += to.size();
            }
            return InternString(str);
        }
        return Value::Nil();
    }
    case Efun::SortArray: {
        if (!args.empty() && args[0].IsObjRef()) {
            auto *arr = GetArrayObjRef(args[0], arrays_);
            if (arr) {
                std::sort(arr->begin(), arr->end(), [this](const Value &a, const Value &b) {
                    const bool a_num = (a.Tag() == ValueTag::Int64) || a.IsFloat64();
                    const bool b_num = (b.Tag() == ValueTag::Int64) || b.IsFloat64();
                    if (!a_num || !b_num) return false;
                    const double da = (a.Tag() == ValueTag::Int64) ? static_cast<double>(GetI64(a)) : a.AsF64();
                    const double db = (b.Tag() == ValueTag::Int64) ? static_cast<double>(GetI64(b)) : b.AsF64();
                    return da < db;
                });
                return args[0];
            }
        }
        return Value::Nil();
    }
    case Efun::Instanceof: {
        if (args.size() < 2) return MakeI64(0);
        if (!IsClassObjRef(args[0])) return MakeI64(0);
        std::uint16_t cur = 0;
        if (!ResolveClassTemplateIndex(args[0], &cur)) return MakeI64(0);
        std::string target_name;
        if (args[1].IsObjRef() && IsStringObjRefFull(args[1])) {
            target_name = ResolveObjRefStringOnly(args[1]);
        } else if (args[1].Tag() == ValueTag::Int64) {
            std::uint16_t idx = static_cast<std::uint16_t>(GetI64(args[1]));
            if (idx < BoundChunk().classes.size()) target_name = BoundChunk().classes[idx].name;
        }
        if (target_name.empty()) return MakeI64(0);
        while (cur != 0xFFFF && cur < BoundChunk().classes.size()) {
            if (BoundChunk().classes[cur].name == target_name) return MakeI64(1);
            cur = BoundChunk().classes[cur].parent_class_idx;
        }
        return MakeI64(0);
    }
    }

    return Value::Nil();
}
