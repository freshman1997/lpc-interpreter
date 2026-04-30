#include "vm/value/lpc_array.h"

#include <cstdlib>
#include <cstring>

namespace lpc {
namespace vm {

LpcArray::LpcArray(std::size_t n, Value fill) : size_(n), capacity_(n) {
    if (n > 0) {
        data_ = static_cast<Value *>(std::calloc(n, sizeof(Value)));
        if (data_) {
            for (std::size_t i = 0; i < n; ++i) {
                data_[i] = fill;
            }
        }
    }
}

LpcArray::~LpcArray() {
    std::free(data_);
    data_ = nullptr;
}

LpcArray::LpcArray(LpcArray &&other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

LpcArray &LpcArray::operator=(LpcArray &&other) noexcept {
    if (this != &other) {
        std::free(data_);
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

void LpcArray::PushBack(const Value &v) {
    if (size_ >= capacity_) {
        Grow(size_ + 1);
    }
    data_[size_] = v;
    ++size_;
}

void LpcArray::Clear() {
    std::free(data_);
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

void LpcArray::InitFromVector(std::vector<Value> &&vec) {
    std::free(data_);
    size_ = vec.size();
    if (size_ > 0) {
        capacity_ = size_;
        data_ = static_cast<Value *>(std::calloc(capacity_, sizeof(Value)));
        if (data_) {
            std::memcpy(data_, vec.data(), size_ * sizeof(Value));
        }
    } else {
        data_ = nullptr;
        capacity_ = 0;
    }
    vec.clear();
}

void LpcArray::Grow(std::size_t min_cap) {
    std::size_t new_cap = (capacity_ == 0) ? 8 : capacity_;
    while (new_cap < min_cap) {
        new_cap *= 2;
    }
    Value *new_data = static_cast<Value *>(std::calloc(new_cap, sizeof(Value)));
    if (data_ && new_data) {
        std::memcpy(new_data, data_, size_ * sizeof(Value));
    }
    std::free(data_);
    data_ = new_data;
    capacity_ = new_cap;
}

} // namespace vm
} // namespace lpc
