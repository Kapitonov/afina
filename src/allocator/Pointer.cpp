#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer() {}
Pointer::Pointer(void **other) : _pointer(other) {}
Pointer::Pointer(const Pointer &other) : _pointer(other._pointer) {}
Pointer::Pointer(Pointer &&other) {
    _pointer = other._pointer;
    other._pointer = nullptr;
}

Pointer &Pointer::operator=(const Pointer &other) {
    _pointer = other._pointer;
    return *this;
}
Pointer &Pointer::operator=(Pointer &&other) {
    std::swap(this->_pointer, other._pointer);
    return *this;
}

void Pointer::set(void **other) { _pointer = other; }

void *Pointer::get() const {
    if (_pointer) {
        return *_pointer;
    } else {
        return nullptr;
    }
}

} // namespace Allocator
} // namespace Afina
