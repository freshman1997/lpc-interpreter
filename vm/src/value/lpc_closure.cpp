#include "vm/value/lpc_closure.h"

#include <cstdlib>
#include <cstring>

namespace lpc {
namespace vm {

LpcClosure::LpcClosure(std::uint16_t func_id, std::size_t nupvalues, Value fill)
    : nupvalues_(nupvalues), func_id_(func_id) {
    if (nupvalues > 0) {
        data_ = static_cast<Value *>(std::calloc(nupvalues, sizeof(Value)));
        if (data_) {
            for (std::size_t i = 0; i < nupvalues; ++i) {
                data_[i] = fill;
            }
        }
    }
}

LpcClosure::~LpcClosure() {
    std::free(data_);
    data_ = nullptr;
}

LpcClosure::LpcClosure(LpcClosure &&other) noexcept
    : data_(other.data_), nupvalues_(other.nupvalues_), func_id_(other.func_id_), module_version_id_(other.module_version_id_) {
    other.data_ = nullptr;
    other.nupvalues_ = 0;
    other.func_id_ = 0;
    other.module_version_id_ = 0;
}

LpcClosure &LpcClosure::operator=(LpcClosure &&other) noexcept {
    if (this != &other) {
        std::free(data_);
        data_ = other.data_;
        nupvalues_ = other.nupvalues_;
        func_id_ = other.func_id_;
        module_version_id_ = other.module_version_id_;
        other.data_ = nullptr;
        other.nupvalues_ = 0;
        other.func_id_ = 0;
        other.module_version_id_ = 0;
    }
    return *this;
}

void LpcClosure::InitUpvalues(std::size_t n, Value fill) {
    std::free(data_);
    nupvalues_ = n;
    if (n > 0) {
        data_ = static_cast<Value *>(std::calloc(n, sizeof(Value)));
        if (data_) {
            for (std::size_t i = 0; i < n; ++i) {
                data_[i] = fill;
            }
        }
    } else {
        data_ = nullptr;
    }
}

void LpcClosure::Clear() {
    std::free(data_);
    data_ = nullptr;
    nupvalues_ = 0;
    func_id_ = 0;
    module_version_id_ = 0;
}

} // namespace vm
} // namespace lpc
