#ifndef LPC_VM_VALUE_LPC_CLASS_H
#define LPC_VM_VALUE_LPC_CLASS_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

#include "vm/value/value.h"

namespace lpc {
namespace vm {

class LpcClass {
public:
    LpcClass() = default;
    explicit LpcClass(std::size_t nfields, Value fill = Value::Nil());
    ~LpcClass();

    LpcClass(const LpcClass &) = delete;
    LpcClass &operator=(const LpcClass &) = delete;

    LpcClass(LpcClass &&other) noexcept;
    LpcClass &operator=(LpcClass &&other) noexcept;

    std::size_t Size() const { return nfields_; }
    bool Empty() const { return nfields_ == 0; }

    Value &At(std::size_t idx) { return data_[idx]; }
    const Value &At(std::size_t idx) const { return data_[idx]; }
    void Set(std::size_t idx, const Value &v) { data_[idx] = v; }

    void Clear();
    void Reset(std::size_t nfields, Value fill = Value::Nil());

    Value *Data() { return data_; }
    const Value *Data() const { return data_; }

    Value *begin() { return data_; }
    Value *end() { return data_ + nfields_; }
    const Value *begin() const { return data_; }
    const Value *end() const { return data_ + nfields_; }

    void InitFromVector(std::vector<Value> &&vec);

private:
    static constexpr std::size_t kInlineCapacity = 4;
    Value *data_ = nullptr;
    std::size_t nfields_ = 0;
    Value inline_data_[kInlineCapacity]{};

    bool UsingInlineData() const { return data_ == inline_data_; }
    void ResetInlineData();
};

} // namespace vm
} // namespace lpc

#endif
