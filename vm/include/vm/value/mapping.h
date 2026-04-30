#ifndef LPC_VM_VALUE_MAPPING_H
#define LPC_VM_VALUE_MAPPING_H

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

#include "vm/value/value.h"
#include "vm/value/hash.h"

namespace lpc {
namespace vm {

class Mapping {
public:
    Mapping() = default;
    ~Mapping();

    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;

    Mapping(Mapping &&other) noexcept;
    Mapping &operator=(Mapping &&other) noexcept;

    Value *Find(const Value &key);
    const Value *Find(const Value &key) const;

    void Insert(const Value &key, const Value &value);
    bool Erase(const Value &key);

    std::uint32_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    void Clear();

    template <typename Fn>
    void ForEachEntry(Fn &&fn) const {
        if (!slots_) return;
        for (std::uint32_t i = 0; i < capacity_; ++i) {
            if (slots_[i].dist != 0) {
                fn(slots_[i].key, slots_[i].value);
            }
        }
    }

    std::vector<std::pair<Value, Value>> ToPairVector() const;

private:
    static constexpr double kLoadFactor = 0.7;
    static constexpr std::uint32_t kMinCapacity = 8;

    struct Slot {
        Value key;
        Value value;
        std::uint8_t dist = 0;
    };

    Slot *slots_ = nullptr;
    std::uint32_t capacity_ = 0;
    std::uint32_t size_ = 0;

    void Grow();
    std::uint32_t HomeIndex(const Value &key) const;

    static std::uint64_t HashKey(const Value &key) { return lpc::vm::HashValue(key); }
    static bool KeyEqual(const Value &a, const Value &b) { return lpc::vm::KeyEqual(a, b); }
};

} // namespace vm
} // namespace lpc

#endif
