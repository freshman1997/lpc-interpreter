#ifndef LPC_VM_VALUE_LPC_ARRAY_H
#define LPC_VM_VALUE_LPC_ARRAY_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

#include "vm/value/value.h"

namespace lpc {
namespace vm {

class LpcArray {
public:
    LpcArray() = default;
    explicit LpcArray(std::size_t n, Value fill = Value::Nil());
    ~LpcArray();

    LpcArray(const LpcArray &) = delete;
    LpcArray &operator=(const LpcArray &) = delete;

    LpcArray(LpcArray &&other) noexcept;
    LpcArray &operator=(LpcArray &&other) noexcept;

    std::size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }

    Value &At(std::size_t idx) { return data_[idx]; }
    const Value &At(std::size_t idx) const { return data_[idx]; }
    void Set(std::size_t idx, const Value &v) { data_[idx] = v; }

    void PushBack(const Value &v);
    void Clear();

    Value *Data() { return data_; }
    const Value *Data() const { return data_; }

    Value *begin() { return data_; }
    Value *end() { return data_ + size_; }
    const Value *begin() const { return data_; }
    const Value *end() const { return data_ + size_; }

    void InitFromVector(std::vector<Value> &&vec);

private:
    static constexpr std::size_t kInlineCapacity = 8;
    Value *data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    Value inline_data_[kInlineCapacity]{};

    bool UsingInlineData() const { return data_ == inline_data_; }
    void ResetInlineData();
    void Grow(std::size_t min_cap);
};

} // namespace vm
} // namespace lpc

#endif
