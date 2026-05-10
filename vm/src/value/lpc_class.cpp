#include "vm/value/lpc_class.h"

#include <cstdlib>
#include <cstring>

namespace lpc {
namespace vm {

LpcClass::LpcClass(std::size_t nfields, Value fill) {
    Reset(nfields, fill);
}

LpcClass::~LpcClass() {
    if (data_ && !UsingInlineData()) {
        std::free(data_);
    }
    data_ = nullptr;
}

LpcClass::LpcClass(LpcClass &&other) noexcept
    : nfields_(other.nfields_) {
    if (other.UsingInlineData()) {
        data_ = inline_data_;
        for (std::size_t i = 0; i < other.nfields_; ++i) {
            inline_data_[i] = other.inline_data_[i];
        }
        other.ResetInlineData();
    } else {
        data_ = other.data_;
        other.data_ = nullptr;
    }
    other.nfields_ = 0;
}

LpcClass &LpcClass::operator=(LpcClass &&other) noexcept {
    if (this != &other) {
        if (data_ && !UsingInlineData()) {
            std::free(data_);
        }
        nfields_ = other.nfields_;
        if (other.UsingInlineData()) {
            data_ = inline_data_;
            for (std::size_t i = 0; i < other.nfields_; ++i) {
                inline_data_[i] = other.inline_data_[i];
            }
            other.ResetInlineData();
        } else {
            data_ = other.data_;
            other.data_ = nullptr;
        }
        other.nfields_ = 0;
    }
    return *this;
}

void LpcClass::ResetInlineData() {
    for (std::size_t i = 0; i < kInlineCapacity; ++i) {
        inline_data_[i] = Value::Nil();
    }
}

void LpcClass::Clear() {
    if (data_ && !UsingInlineData()) {
        std::free(data_);
    }
    data_ = nullptr;
    nfields_ = 0;
    ResetInlineData();
}

void LpcClass::Reset(std::size_t nfields, Value fill) {
    if (nfields == 0) {
        Clear();
        return;
    }

    if (nfields <= kInlineCapacity) {
        if (data_ && !UsingInlineData()) {
            std::free(data_);
        }
        data_ = inline_data_;
    } else if (UsingInlineData() || data_ == nullptr || nfields_ != nfields) {
        if (data_ && !UsingInlineData()) {
            std::free(data_);
        }
        data_ = static_cast<Value *>(std::calloc(nfields, sizeof(Value)));
    }

    nfields_ = data_ ? nfields : 0;
    for (std::size_t i = 0; i < nfields_; ++i) {
        data_[i] = fill;
    }
}

void LpcClass::InitFromVector(std::vector<Value> &&vec) {
    if (data_ && !UsingInlineData()) {
        std::free(data_);
    }
    nfields_ = vec.size();
    if (nfields_ > 0) {
        data_ = nfields_ <= kInlineCapacity
            ? inline_data_
            : static_cast<Value *>(std::calloc(nfields_, sizeof(Value)));
        if (data_) {
            std::memcpy(data_, vec.data(), nfields_ * sizeof(Value));
        }
    } else {
        data_ = nullptr;
    }
    vec.clear();
}

} // namespace vm
} // namespace lpc
