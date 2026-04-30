#ifndef LPC_VM_VALUE_LPC_CLOSURE_H
#define LPC_VM_VALUE_LPC_CLOSURE_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

#include "vm/value/value.h"

namespace lpc {
namespace vm {

class LpcClosure {
public:
    LpcClosure() = default;
    LpcClosure(std::uint16_t func_id, std::size_t nupvalues, Value fill = Value::Nil());
    ~LpcClosure();

    LpcClosure(const LpcClosure &) = delete;
    LpcClosure &operator=(const LpcClosure &) = delete;

    LpcClosure(LpcClosure &&other) noexcept;
    LpcClosure &operator=(LpcClosure &&other) noexcept;

    std::uint16_t FuncId() const { return func_id_; }
    void SetFuncId(std::uint16_t fid) { func_id_ = fid; }
    std::uint64_t ModuleVersionId() const { return module_version_id_; }
    void SetModuleVersionId(std::uint64_t version_id) { module_version_id_ = version_id; }

    std::size_t UpvalueCount() const { return nupvalues_; }
    bool Empty() const { return nupvalues_ == 0 && func_id_ == 0; }

    Value &GetUpvalue(std::size_t idx) { return data_[idx]; }
    const Value &GetUpvalue(std::size_t idx) const { return data_[idx]; }
    void SetUpvalue(std::size_t idx, const Value &v) { data_[idx] = v; }

    void InitUpvalues(std::size_t n, Value fill = Value::Nil());
    void Clear();

    Value *Data() { return data_; }
    const Value *Data() const { return data_; }

    Value *begin() { return data_; }
    Value *end() { return data_ + nupvalues_; }
    const Value *begin() const { return data_; }
    const Value *end() const { return data_ + nupvalues_; }

private:
    Value *data_ = nullptr;
    std::size_t nupvalues_ = 0;
    std::uint16_t func_id_ = 0;
    std::uint64_t module_version_id_ = 0;
};

} // namespace vm
} // namespace lpc

#endif
