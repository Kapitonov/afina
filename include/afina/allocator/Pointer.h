#ifndef AFINA_ALLOCATOR_POINTER_H
#define AFINA_ALLOCATOR_POINTER_H

#include <utility>

namespace Afina {
namespace Allocator {
// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Simple;

class Pointer {

    friend class Simple;

public:
    Pointer();

    Pointer(void **);
    Pointer(const Pointer &);
    Pointer(Pointer &&);

    Pointer &operator=(const Pointer &);
    Pointer &operator=(Pointer &&);

    void *get() const;
    void set(void **);

private:
    void **_pointer;
};

} // namespace Allocator
} // namespace Afina

#endif // AFINA_ALLOCATOR_POINTER_H
