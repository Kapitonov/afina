#ifndef AFINA_ALLOCATOR_SIMPLE_H
#define AFINA_ALLOCATOR_SIMPLE_H

#include "Block.h"
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>

namespace Afina {
namespace Allocator {

// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Pointer;

/**
 * Wraps given memory area and provides defagmentation allocator interface on
 * the top of it.
 *
 * Allocator instance doesn't take ownership of wrapped memmory and do not delete it
 * on destruction. So caller must take care of resource cleaup after allocator stop
 * being needs
 */
// TODO: Implements interface to allow usage as C++ allocators
class Simple {
public:
    Simple(void *base, const size_t size);

    /**
     * TODO: semantics
     * @param N size_t
     */
    Pointer alloc(size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     * @param N size_t
     */
    void realloc(Pointer &p, size_t N);

    /**
     * TODO: semantics
     * @param p Pointer
     */
    void free(Pointer &p);

    /**
     * TODO: semantics
     */
    void defrag();

    /**
     * TODO: semantics
     */
    std::string dump() const;

private:
    void *_base;
    const size_t _base_len;
    Block *_first_empty_block;
    void *_descriptor;
    void *_first_empty_descriptor;
    size_t _number_descriptor;

    void _find_empty_descriptor();
    void _merge_blocks(Block*);
};

} // namespace Allocator
} // namespace Afina
#endif // AFINA_ALLOCATOR_SIMPLE_H
