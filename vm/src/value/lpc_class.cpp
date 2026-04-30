#include "vm/value/lpc_class.h"

#include <cstdlib>
#include <cstring>

namespace lpc {
namespace vm {

LpcClass::LpcClass(std::size_t nfields, Value fill) : nfields_(nfields) {
    if (nfields > 0) {
        data_ = static_cast<Value *>(std::calloc(nfields, sizeof(Value)));
        if (data_) {
            for (std::size_t i = 0; i < nfields; ++i) {
                data_[i] = fill;
            }
        }
    }
}

LpcClass::~LpcClass() {
    std::free(data_);
    data_ = nullptr;
}

LpcClass::LpcClass(LpcClass &&other) noexcept
    : data_(other.data_), nfields_(other.nfields_) {
    other.data_ = nullptr;
    other.nfields_ = 0;
}

LpcClass &LpcClass::operator=(LpcClass &&other) noexcept {
    if (this != &other) {
        std::free(data_);
        data_ = other.data_;
        nfields_ = other.nfields_;
        other.data_ = nullptr;
        other.nfields_ = 0;
    }
    return *this;
}

void LpcClass::Clear() {
    std::free(data_);
    data_ = nullptr;
    nfields_ = 0;
}

void LpcClass::InitFromVector(std::vector<Value> &&vec) {
    std::free(data_);
    nfields_ = vec.size();
    if (nfields_ > 0) {
        data_ = static_cast<Value *>(std::calloc(nfields_, sizeof(Value)));
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
