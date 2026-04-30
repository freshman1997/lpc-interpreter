#ifndef LPC_VM_PERSISTENT_STORAGE_H
#define LPC_VM_PERSISTENT_STORAGE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace lpc {
namespace vm {

class Vm;

struct StoredValue {
    enum Type : std::uint8_t {
        Null = 0,
        Int64 = 1,
        Float64 = 2,
        String = 3,
        ObjRef = 4,
        Array = 5,
        Mapping = 6,
        Class = 7,
        Object = 8,
        Closure = 9,
        Bool = 10,
    };
    Type type = Null;
    std::int64_t int_val = 0;
    double float_val = 0.0;
    std::string str_val;
    bool bool_val = false;
    std::vector<StoredValue> elements;
    std::vector<std::pair<StoredValue, StoredValue>> pairs;
    std::string class_name;
    std::string module_name;
    std::uint64_t module_version_id = 0;
    std::vector<std::pair<std::string, StoredValue>> fields;
};

struct StoredObject {
    std::string module_name;
    std::uint64_t module_version_id = 0;
    std::vector<std::pair<std::string, StoredValue>> globals;
    std::string object_type;
};

struct PersistedModuleState {
    std::string module_name;
    std::uint64_t active_version_id = 0;
    std::string chunk_serialized;
    std::vector<StoredObject> objects;
};

class PersistenceCodec {
public:
    static std::vector<std::uint8_t> SerializeValue(const StoredValue &val);
    static bool DeserializeValue(const std::uint8_t *data, std::size_t len, std::size_t &pos, StoredValue &out);

    static std::vector<std::uint8_t> SerializeObject(const StoredObject &obj);
    static bool DeserializeObject(const std::uint8_t *data, std::size_t len, std::size_t &pos, StoredObject &out);

    static std::vector<std::uint8_t> SerializeModuleState(const PersistedModuleState &state);
    static bool DeserializeModuleState(const std::uint8_t *data, std::size_t len, PersistedModuleState &out);

private:
    static void SerializeValueFlat(const StoredValue &val, std::vector<std::uint8_t> &buf);
    static void WriteU8(std::vector<std::uint8_t> &buf, std::uint8_t v);
    static void WriteU64(std::vector<std::uint8_t> &buf, std::uint64_t v);
    static void WriteF64(std::vector<std::uint8_t> &buf, double v);
    static void WriteStr(std::vector<std::uint8_t> &buf, const std::string &s);
    static bool ReadU8(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint8_t &out);
    static bool ReadU64(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint64_t &out);
    static bool ReadF64(const std::uint8_t *data, std::size_t len, std::size_t &pos, double &out);
    static bool ReadStr(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::string &out);
};

class PersistentStorage {
public:
    explicit PersistentStorage(const std::string &base_dir);
    ~PersistentStorage() = default;

    bool SaveModuleState(const Vm &vm, const std::string &module_name);
    bool LoadModuleState(Vm &vm, const std::string &module_name);
    bool SaveAllModules(const Vm &vm);
    bool LoadAllModules(Vm &vm);

    bool DeleteModuleState(const std::string &module_name);
    std::vector<std::string> ListPersistedModules() const;

    void SetAutoSave(bool enabled) { auto_save_ = enabled; }
    bool auto_save() const { return auto_save_; }

    bool RehashAfterHotReload(Vm &vm, const std::string &module_name, std::uint64_t new_version_id);

    std::string base_dir() const { return base_dir_; }

private:
    StoredValue ValueToStored(const Vm &vm, const class Value &val) const;
    class Value StoredToValue(Vm &vm, const StoredValue &stored) const;
    StoredObject ObjectToStored(const Vm &vm, std::size_t obj_id) const;
    bool StoredToObject(Vm &vm, const StoredObject &stored) const;

    std::string ModuleFilePath(const std::string &module_name) const;
    bool WriteFile(const std::string &path, const std::vector<std::uint8_t> &data) const;
    bool ReadFile(const std::string &path, std::vector<std::uint8_t> &data) const;

    std::string base_dir_;
    bool auto_save_ = false;
};

} // namespace vm
} // namespace lpc

#endif
