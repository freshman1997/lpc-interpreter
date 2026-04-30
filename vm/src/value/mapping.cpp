#include "vm/value/mapping.h"

#include <cstdlib>

namespace lpc {
namespace vm {

Mapping::~Mapping() {
    std::free(slots_);
    slots_ = nullptr;
}

Mapping::Mapping(Mapping &&other) noexcept
    : slots_(other.slots_), capacity_(other.capacity_),
      size_(other.size_) {
    other.slots_ = nullptr;
    other.capacity_ = 0;
    other.size_ = 0;
}

Mapping &Mapping::operator=(Mapping &&other) noexcept {
    if (this != &other) {
        std::free(slots_);
        slots_ = other.slots_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        other.slots_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }
    return *this;
}

std::uint32_t Mapping::HomeIndex(const Value &key) const {
    std::uint64_t h = HashKey(key);
    std::uint32_t mask = capacity_ - 1;
    return static_cast<std::uint32_t>(h) & mask;
}

Value *Mapping::Find(const Value &key) {
    if (capacity_ == 0) return nullptr;
    std::uint32_t idx = HomeIndex(key);
    std::uint32_t mask = capacity_ - 1;
    std::uint8_t dist = 1;
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (slots_[idx].dist == 0) return nullptr;
        if (slots_[idx].dist >= dist && KeyEqual(slots_[idx].key, key)) {
            return &slots_[idx].value;
        }
        if (slots_[idx].dist < dist) return nullptr;
        ++dist;
        idx = (idx + 1) & mask;
    }
    return nullptr;
}

const Value *Mapping::Find(const Value &key) const {
    if (capacity_ == 0) return nullptr;
    std::uint32_t idx = HomeIndex(key);
    std::uint32_t mask = capacity_ - 1;
    std::uint8_t dist = 1;
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (slots_[idx].dist == 0) return nullptr;
        if (slots_[idx].dist >= dist && KeyEqual(slots_[idx].key, key)) {
            return &slots_[idx].value;
        }
        if (slots_[idx].dist < dist) return nullptr;
        ++dist;
        idx = (idx + 1) & mask;
    }
    return nullptr;
}

void Mapping::Insert(const Value &key, const Value &value) {
    if (capacity_ == 0 || size_ + 1 > static_cast<std::uint32_t>(capacity_ * kLoadFactor)) {
        Grow();
    }

    std::uint32_t idx = HomeIndex(key);
    std::uint32_t mask = capacity_ - 1;
    std::uint8_t dist = 1;

    Value cur_key = key;
    Value cur_val = value;

    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (slots_[idx].dist == 0) {
            slots_[idx].key = cur_key;
            slots_[idx].value = cur_val;
            slots_[idx].dist = dist;
            ++size_;
            return;
        }

        if (KeyEqual(slots_[idx].key, cur_key)) {
            slots_[idx].value = cur_val;
            return;
        }

        if (slots_[idx].dist < dist) {
            Value swap_key = slots_[idx].key;
            Value swap_val = slots_[idx].value;
            std::uint8_t swap_dist = slots_[idx].dist;

            slots_[idx].key = cur_key;
            slots_[idx].value = cur_val;
            slots_[idx].dist = dist;

            cur_key = swap_key;
            cur_val = swap_val;
            dist = swap_dist;
        }

        ++dist;
        idx = (idx + 1) & mask;
    }
}

bool Mapping::Erase(const Value &key) {
    if (capacity_ == 0) return false;
    std::uint32_t idx = HomeIndex(key);
    std::uint32_t mask = capacity_ - 1;
    std::uint8_t dist = 1;

    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (slots_[idx].dist == 0) return false;
        if (slots_[idx].dist < dist) return false;
        if (slots_[idx].dist >= dist && KeyEqual(slots_[idx].key, key)) {
            --size_;
            std::uint32_t cur = idx;
            std::uint32_t next = (cur + 1) & mask;
            while (slots_[next].dist > 1) {
                slots_[cur].key = slots_[next].key;
                slots_[cur].value = slots_[next].value;
                slots_[cur].dist = slots_[next].dist - 1;
                cur = next;
                next = (cur + 1) & mask;
            }
            slots_[cur].key = Value::Nil();
            slots_[cur].value = Value::Nil();
            slots_[cur].dist = 0;
            return true;
        }
        ++dist;
        idx = (idx + 1) & mask;
    }
    return false;
}

void Mapping::Clear() {
    if (slots_) {
        std::free(slots_);
        slots_ = nullptr;
    }
    capacity_ = 0;
    size_ = 0;
}

void Mapping::Grow() {
    std::uint32_t new_cap = (capacity_ == 0) ? kMinCapacity : capacity_ * 2;
    Slot *new_slots = static_cast<Slot *>(std::calloc(new_cap, sizeof(Slot)));
    if (!new_slots) return;

    Slot *old_slots = slots_;
    std::uint32_t old_cap = capacity_;

    slots_ = new_slots;
    capacity_ = new_cap;
    size_ = 0;

    if (old_slots) {
        for (std::uint32_t i = 0; i < old_cap; ++i) {
            if (old_slots[i].dist != 0) {
                Insert(old_slots[i].key, old_slots[i].value);
            }
        }
        std::free(old_slots);
    }
}

std::vector<std::pair<Value, Value>> Mapping::ToPairVector() const {
    std::vector<std::pair<Value, Value>> result;
    result.reserve(size_);
    ForEachEntry([&](const Value &k, const Value &v) {
        result.push_back({k, v});
    });
    return result;
}

} // namespace vm
} // namespace lpc
