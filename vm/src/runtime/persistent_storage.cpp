#include "vm/runtime/persistent_storage.h"
#include "vm/runtime/vm.h"
#include "vm/value/value.h"
#include "vm/value/objref.h"
#include "vm/bytecode/binary_format.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace lpc {
namespace vm {

void PersistenceCodec::WriteU8(std::vector<std::uint8_t> &buf, std::uint8_t v) {
    buf.push_back(v);
}

void PersistenceCodec::WriteU64(std::vector<std::uint8_t> &buf, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void PersistenceCodec::WriteF64(std::vector<std::uint8_t> &buf, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, 8);
    WriteU64(buf, bits);
}

void PersistenceCodec::WriteStr(std::vector<std::uint8_t> &buf, const std::string &s) {
    std::uint32_t slen = static_cast<std::uint32_t>(s.size());
    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<std::uint8_t>((slen >> (i * 8)) & 0xFF));
    buf.insert(buf.end(), s.begin(), s.end());
}

bool PersistenceCodec::ReadU8(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint8_t &out) {
    if (pos >= len) return false;
    out = data[pos++];
    return true;
}

bool PersistenceCodec::ReadU64(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint64_t &out) {
    if (pos + 8 > len) return false;
    out = 0;
    for (int i = 0; i < 8; ++i)
        out |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
    pos += 8;
    return true;
}

bool PersistenceCodec::ReadF64(const std::uint8_t *data, std::size_t len, std::size_t &pos, double &out) {
    std::uint64_t bits = 0;
    if (!ReadU64(data, len, pos, bits)) return false;
    std::memcpy(&out, &bits, 8);
    return true;
}

bool PersistenceCodec::ReadStr(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::string &out) {
    if (pos + 4 > len) return false;
    std::uint32_t slen = 0;
    for (int i = 0; i < 4; ++i)
        slen |= static_cast<std::uint32_t>(data[pos + i]) << (i * 8);
    pos += 4;
    if (pos + slen > len) return false;
    out.assign(reinterpret_cast<const char *>(data + pos), slen);
    pos += slen;
    return true;
}

void PersistenceCodec::SerializeValueFlat(const StoredValue &val, std::vector<std::uint8_t> &buf) {
    WriteU8(buf, static_cast<std::uint8_t>(val.type));
    switch (val.type) {
        case StoredValue::Null: break;
        case StoredValue::Bool: WriteU8(buf, val.bool_val ? 1 : 0); break;
        case StoredValue::Int64: WriteU64(buf, static_cast<std::uint64_t>(val.int_val)); break;
        case StoredValue::Float64: WriteF64(buf, val.float_val); break;
        case StoredValue::String: WriteStr(buf, val.str_val); break;
        case StoredValue::ObjRef: WriteU64(buf, static_cast<std::uint64_t>(val.int_val)); break;
        case StoredValue::Array: {
            WriteU64(buf, val.elements.size());
            for (const auto &e : val.elements) {
                SerializeValueFlat(e, buf);
            }
            break;
        }
        case StoredValue::Mapping: {
            WriteU64(buf, val.pairs.size());
            for (const auto &[k, v] : val.pairs) {
                SerializeValueFlat(k, buf);
                SerializeValueFlat(v, buf);
            }
            break;
        }
        case StoredValue::Class: {
            WriteStr(buf, val.class_name);
            WriteStr(buf, val.module_name);
            WriteU64(buf, val.fields.size());
            for (const auto &[name, fv] : val.fields) {
                WriteStr(buf, name);
                SerializeValueFlat(fv, buf);
            }
            break;
        }
        case StoredValue::Object: {
            WriteStr(buf, val.module_name);
            WriteU64(buf, val.module_version_id);
            WriteU64(buf, val.fields.size());
            for (const auto &[name, fv] : val.fields) {
                WriteStr(buf, name);
                SerializeValueFlat(fv, buf);
            }
            break;
        }
        case StoredValue::Closure: {
            WriteU64(buf, val.int_val);
            WriteStr(buf, val.str_val);
            break;
        }
    }
}

std::vector<std::uint8_t> PersistenceCodec::SerializeValue(const StoredValue &val) {
    std::vector<std::uint8_t> buf;
    buf.reserve(64);
    SerializeValueFlat(val, buf);
    return buf;
}

bool PersistenceCodec::DeserializeValue(const std::uint8_t *data, std::size_t len, std::size_t &pos, StoredValue &out) {
    std::uint8_t type_byte = 0;
    if (!ReadU8(data, len, pos, type_byte)) return false;
    out.type = static_cast<StoredValue::Type>(type_byte);

    switch (out.type) {
        case StoredValue::Null: break;
        case StoredValue::Bool: {
            std::uint8_t b = 0;
            if (!ReadU8(data, len, pos, b)) return false;
            out.bool_val = (b != 0);
            break;
        }
        case StoredValue::Int64: {
            std::uint64_t v = 0;
            if (!ReadU64(data, len, pos, v)) return false;
            out.int_val = static_cast<std::int64_t>(v);
            break;
        }
        case StoredValue::Float64: {
            if (!ReadF64(data, len, pos, out.float_val)) return false;
            break;
        }
        case StoredValue::String: {
            if (!ReadStr(data, len, pos, out.str_val)) return false;
            break;
        }
        case StoredValue::ObjRef: {
            std::uint64_t v = 0;
            if (!ReadU64(data, len, pos, v)) return false;
            out.int_val = static_cast<std::int64_t>(v);
            break;
        }
        case StoredValue::Array: {
            std::uint64_t count = 0;
            if (!ReadU64(data, len, pos, count)) return false;
            out.elements.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (!DeserializeValue(data, len, pos, out.elements[i])) return false;
            }
            break;
        }
        case StoredValue::Mapping: {
            std::uint64_t count = 0;
            if (!ReadU64(data, len, pos, count)) return false;
            out.pairs.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (!DeserializeValue(data, len, pos, out.pairs[i].first)) return false;
                if (!DeserializeValue(data, len, pos, out.pairs[i].second)) return false;
            }
            break;
        }
        case StoredValue::Class: {
            if (!ReadStr(data, len, pos, out.class_name)) return false;
            if (!ReadStr(data, len, pos, out.module_name)) return false;
            std::uint64_t fcount = 0;
            if (!ReadU64(data, len, pos, fcount)) return false;
            out.fields.resize(fcount);
            for (std::size_t i = 0; i < fcount; ++i) {
                if (!ReadStr(data, len, pos, out.fields[i].first)) return false;
                if (!DeserializeValue(data, len, pos, out.fields[i].second)) return false;
            }
            break;
        }
        case StoredValue::Object: {
            if (!ReadStr(data, len, pos, out.module_name)) return false;
            if (!ReadU64(data, len, pos, out.module_version_id)) return false;
            std::uint64_t fcount = 0;
            if (!ReadU64(data, len, pos, fcount)) return false;
            out.fields.resize(fcount);
            for (std::size_t i = 0; i < fcount; ++i) {
                if (!ReadStr(data, len, pos, out.fields[i].first)) return false;
                if (!DeserializeValue(data, len, pos, out.fields[i].second)) return false;
            }
            break;
        }
        case StoredValue::Closure: {
            std::uint64_t v = 0;
            if (!ReadU64(data, len, pos, v)) return false;
            out.int_val = static_cast<std::int64_t>(v);
            if (!ReadStr(data, len, pos, out.str_val)) return false;
            break;
        }
    }
    return true;
}

std::vector<std::uint8_t> PersistenceCodec::SerializeObject(const StoredObject &obj) {
    std::vector<std::uint8_t> buf;
    buf.reserve(64);
    WriteStr(buf, obj.module_name);
    WriteU64(buf, obj.module_version_id);
    WriteStr(buf, obj.object_type);
    WriteU64(buf, obj.globals.size());
    for (const auto &[name, val] : obj.globals) {
        WriteStr(buf, name);
        SerializeValueFlat(val, buf);
    }
    return buf;
}

bool PersistenceCodec::DeserializeObject(const std::uint8_t *data, std::size_t len, std::size_t &pos, StoredObject &out) {
    if (!ReadStr(data, len, pos, out.module_name)) return false;
    if (!ReadU64(data, len, pos, out.module_version_id)) return false;
    if (!ReadStr(data, len, pos, out.object_type)) return false;
    std::uint64_t gcount = 0;
    if (!ReadU64(data, len, pos, gcount)) return false;
    out.globals.resize(gcount);
    for (std::size_t i = 0; i < gcount; ++i) {
        if (!ReadStr(data, len, pos, out.globals[i].first)) return false;
        if (!DeserializeValue(data, len, pos, out.globals[i].second)) return false;
    }
    return true;
}

std::vector<std::uint8_t> PersistenceCodec::SerializeModuleState(const PersistedModuleState &state) {
    std::vector<std::uint8_t> buf;
    buf.reserve(128);
    buf.push_back(0x4C); buf.push_back(0x50);
    buf.push_back(0x43); buf.push_back(0x50);
    WriteStr(buf, state.module_name);
    WriteU64(buf, state.active_version_id);
    WriteStr(buf, state.chunk_serialized);
    WriteU64(buf, state.objects.size());
    for (const auto &obj : state.objects) {
        WriteStr(buf, obj.module_name);
        WriteU64(buf, obj.module_version_id);
        WriteStr(buf, obj.object_type);
        WriteU64(buf, obj.globals.size());
        for (const auto &[name, val] : obj.globals) {
            WriteStr(buf, name);
            SerializeValueFlat(val, buf);
        }
    }
    return buf;
}

bool PersistenceCodec::DeserializeModuleState(const std::uint8_t *data, std::size_t len, PersistedModuleState &out) {
    if (len < 4) return false;
    if (data[0] != 0x4C || data[1] != 0x50 || data[2] != 0x43 || data[3] != 0x50) return false;
    std::size_t pos = 4;
    if (!ReadStr(data, len, pos, out.module_name)) return false;
    if (!ReadU64(data, len, pos, out.active_version_id)) return false;
    if (!ReadStr(data, len, pos, out.chunk_serialized)) return false;
    std::uint64_t ocount = 0;
    if (!ReadU64(data, len, pos, ocount)) return false;
    out.objects.resize(ocount);
    for (std::size_t i = 0; i < ocount; ++i) {
        if (!DeserializeObject(data, len, pos, out.objects[i])) return false;
    }
    return true;
}

PersistentStorage::PersistentStorage(const std::string &base_dir) : base_dir_(base_dir) {
    std::filesystem::create_directories(base_dir_);
}

StoredValue PersistentStorage::ValueToStored(const Vm &vm, const Value &val) const {
    StoredValue stored;
    if (val.IsNil()) {
        stored.type = StoredValue::Null;
    } else if (val.Tag() == ValueTag::Bool) {
        stored.type = StoredValue::Bool;
        stored.bool_val = val.AsBool();
    } else if (val.Tag() == ValueTag::Int64) {
        stored.type = StoredValue::Int64;
        stored.int_val = val.AsI64();
    } else if (val.Tag() == ValueTag::BoxedInt) {
        stored.type = StoredValue::Int64;
        stored.int_val = vm.GetI64(val);
    } else if (val.IsFloat64()) {
        stored.type = StoredValue::Float64;
        stored.float_val = val.AsF64();
    } else if (val.IsObjRef()) {
        std::string sv = vm.ResolveObjRefStringOnly(val);
        if (!sv.empty()) {
            stored.type = StoredValue::String;
            stored.str_val = sv;
        } else if (DecodeClassId(val) > 0) {
            stored.type = StoredValue::Class;
            stored.class_name = "class";
        } else if (DecodeObjectId(val) > 0) {
            stored.type = StoredValue::Object;
            std::size_t obj_id = DecodeObjectId(val);
            std::size_t obj_idx = obj_id - 1;
            if (obj_idx < vm.objects().size() && !vm.objects()[obj_idx].destroyed) {
                const auto &obj = vm.objects()[obj_idx];
                stored.module_name = obj.module_name;
                stored.module_version_id = obj.module_version_id;
                const Chunk *chunk = vm.GetChunkForVersion(obj.module_version_id);
                if (chunk) {
                    for (std::size_t gi = 0; gi < obj.globals.size() && gi < chunk->global_names.size(); ++gi) {
                        stored.fields.push_back({chunk->global_names[gi], ValueToStored(vm, obj.globals[gi])});
                    }
                }
            }
        } else {
            stored.type = StoredValue::ObjRef;
            stored.int_val = static_cast<std::int64_t>(val.AsObj());
        }
    }
    return stored;
}

Value PersistentStorage::StoredToValue(Vm &vm, const StoredValue &stored) const {
    switch (stored.type) {
        case StoredValue::Null: return Value::Nil();
        case StoredValue::Bool: return Value::FromBool(stored.bool_val);
        case StoredValue::Int64: return vm.MakeI64(stored.int_val);
        case StoredValue::Float64: return Value::FromF64(stored.float_val);
        case StoredValue::String: return vm.InternString(stored.str_val);
        case StoredValue::ObjRef: return Value::FromObj(static_cast<std::uint64_t>(stored.int_val));
        case StoredValue::Array: {
            std::vector<Value> elems;
            for (const auto &e : stored.elements) {
                elems.push_back(StoredToValue(vm, e));
            }
            return vm.AllocateArrayHandle(std::move(elems));
        }
        case StoredValue::Mapping: {
            Mapping m;
            for (const auto &[k, v] : stored.pairs) {
                m.Insert(StoredToValue(vm, k), StoredToValue(vm, v));
            }
            return vm.AllocateMappingHandle(std::move(m));
        }
        case StoredValue::Class:
        case StoredValue::Object:
        case StoredValue::Closure:
            return Value::Nil();
    }
    return Value::Nil();
}

StoredObject PersistentStorage::ObjectToStored(const Vm &vm, std::size_t obj_id) const {
    StoredObject stored;
    if (obj_id >= vm.objects().size()) return stored;
    const auto &obj = vm.objects()[obj_id];
    if (obj.destroyed) return stored;

    stored.module_name = obj.module_name;
    stored.module_version_id = obj.module_version_id;
    stored.object_type = "object";

    const Chunk *chunk = vm.GetChunkForVersion(obj.module_version_id);
    if (chunk) {
        for (std::size_t gi = 0; gi < obj.globals.size() && gi < chunk->global_names.size(); ++gi) {
            stored.globals.push_back({chunk->global_names[gi], ValueToStored(vm, obj.globals[gi])});
        }
    }
    return stored;
}

bool PersistentStorage::StoredToObject(Vm &vm, const StoredObject &stored) const {
    for (auto &obj : vm.objects()) {
        if (obj.destroyed) continue;
        if (obj.module_name == stored.module_name) {
            const Chunk *chunk = vm.GetChunkForVersion(obj.module_version_id);
            if (!chunk) continue;
            for (const auto &[name, val] : stored.globals) {
                for (std::size_t gi = 0; gi < chunk->global_names.size(); ++gi) {
                    if (chunk->global_names[gi] == name) {
                        if (gi < obj.globals.size()) {
                            obj.globals[gi] = StoredToValue(vm, val);
                        }
                        break;
                    }
                }
            }
        }
    }
    return true;
}

std::string PersistentStorage::ModuleFilePath(const std::string &module_name) const {
    std::string safe_name = module_name;
    std::replace(safe_name.begin(), safe_name.end(), '/', '_');
    std::replace(safe_name.begin(), safe_name.end(), '\\', '_');
    std::replace(safe_name.begin(), safe_name.end(), ':', '_');
    return base_dir_ + "/" + safe_name + ".lpcp";
}

bool PersistentStorage::WriteFile(const std::string &path, const std::vector<std::uint8_t> &data) const {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

bool PersistentStorage::ReadFile(const std::string &path, std::vector<std::uint8_t> &data) const {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;
    auto size = ifs.tellg();
    ifs.seekg(0);
    data.resize(static_cast<std::size_t>(size));
    ifs.read(reinterpret_cast<char *>(data.data()), size);
    return ifs.good();
}

bool PersistentStorage::SaveModuleState(const Vm &vm, const std::string &module_name) {
    PersistedModuleState state;
    state.module_name = module_name;

    auto status = vm.GetHotReloadStatus(module_name);
    state.active_version_id = status.active_version;

    const Chunk *chunk = vm.GetChunkForVersion(status.active_version);
    if (chunk) {
        state.chunk_serialized = SerializeChunk(*chunk);
    }

    for (std::size_t i = 0; i < vm.objects().size(); ++i) {
        const auto &obj = vm.objects()[i];
        if (obj.destroyed) continue;
        if (obj.module_name == module_name) {
            state.objects.push_back(ObjectToStored(vm, i));
        }
    }

    auto data = PersistenceCodec::SerializeModuleState(state);
    return WriteFile(ModuleFilePath(module_name), data);
}

bool PersistentStorage::LoadModuleState(Vm &vm, const std::string &module_name) {
    std::vector<std::uint8_t> data;
    if (!ReadFile(ModuleFilePath(module_name), data)) return false;

    PersistedModuleState state;
    if (!PersistenceCodec::DeserializeModuleState(data.data(), data.size(), state)) return false;

    if (!state.chunk_serialized.empty()) {
        Chunk chunk;
        if (DeserializeChunk(state.chunk_serialized, &chunk)) {
            auto status = vm.GetHotReloadStatus(module_name);
            if (status.active_version == 0) {
                vm.LoadModule(module_name, chunk);
            }
        }
    }

    for (const auto &stored_obj : state.objects) {
        StoredToObject(vm, stored_obj);
    }

    return true;
}

bool PersistentStorage::SaveAllModules(const Vm &vm) {
    bool ok = true;
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < vm.objects().size(); ++i) {
        const auto &obj = vm.objects()[i];
        if (obj.destroyed || obj.module_name.empty()) continue;
        if (seen.count(obj.module_name)) continue;
        seen.insert(obj.module_name);
        if (!SaveModuleState(vm, obj.module_name)) ok = false;
    }
    return ok;
}

bool PersistentStorage::LoadAllModules(Vm &vm) {
    bool ok = true;
    for (const auto &name : ListPersistedModules()) {
        if (!LoadModuleState(vm, name)) ok = false;
    }
    return ok;
}

bool PersistentStorage::DeleteModuleState(const std::string &module_name) {
    return std::filesystem::remove(ModuleFilePath(module_name));
}

std::vector<std::string> PersistentStorage::ListPersistedModules() const {
    std::vector<std::string> result;
    if (!std::filesystem::exists(base_dir_)) return result;
    for (const auto &entry : std::filesystem::directory_iterator(base_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lpcp") {
            result.push_back(entry.path().stem().string());
        }
    }
    return result;
}

bool PersistentStorage::RehashAfterHotReload(Vm &vm, const std::string &module_name, std::uint64_t new_version_id) {
    const Chunk *new_chunk = vm.GetChunkForVersion(new_version_id);
    if (!new_chunk) return false;

    for (std::size_t i = 0; i < vm.objects().size(); ++i) {
        auto &obj = vm.objects()[i];
        if (obj.destroyed || obj.module_name != module_name) continue;

        std::vector<Value> new_globals(new_chunk->global_names.size(), Value::Nil());

        const Chunk *old_chunk = vm.GetChunkForVersion(obj.module_version_id);
        if (old_chunk) {
            for (std::size_t gi = 0; gi < obj.globals.size() && gi < old_chunk->global_names.size(); ++gi) {
                const std::string &name = old_chunk->global_names[gi];
                for (std::size_t ni = 0; ni < new_chunk->global_names.size(); ++ni) {
                    if (new_chunk->global_names[ni] == name) {
                        new_globals[ni] = obj.globals[gi];
                        break;
                    }
                }
            }
        }

        obj.globals = std::move(new_globals);
        obj.module_version_id = new_version_id;
        obj.blueprint = new_chunk;
    }

    if (auto_save_) {
        SaveModuleState(vm, module_name);
    }

    return true;
}

} // namespace vm
} // namespace lpc
