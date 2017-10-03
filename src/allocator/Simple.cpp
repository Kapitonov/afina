#include <afina/allocator/Error.h>
#include <afina/allocator/Simple.h>

#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Simple::Simple(void *base, size_t size)
    : _base(base), _base_len(size), _first_empty_block((Block *)_base),
      _descriptor((void *)((char *)_base + _base_len)), _first_empty_descriptor(nullptr), _number_descriptor(0) {
    _first_empty_block->size = size;
    _first_empty_block->next = nullptr;
}

/**
 * TODO: semantics
 * @param N size_t
 */
Pointer Simple::alloc(size_t N) {

    if (N < sizeof(void *))
        N = sizeof(void *); // Не выделяем памяти меньше чем возможно освободить.
    // Проверка на наличие свободной памяти

    if (!_first_empty_block) {
        throw AllocError(AllocErrorType::NoMemory, "No Memory");
    }
    // Ищем дескриптор для выделяемой памяти
    _find_empty_descriptor();
    // Поиск подходящего блока свободной памяти
    Block *tmp = nullptr;
    Block *temp = _first_empty_block;
    while (temp && (temp->size < N)) {
        tmp = temp;
        temp = temp->next;
    }
    if (!temp) {
        defrag();
        if (_first_empty_block->size < (N + sizeof(size_t))) {
            throw AllocError(AllocErrorType::NoMemory, "No Memory");
        }
        tmp = nullptr;
        temp = _first_empty_block;
    }
    if (*(size_t *)temp < (N + sizeof(size_t) + sizeof(Block))) { // Убираем блок выделенной памяти из списка.
        if (tmp) {
            tmp->next = temp->next;
        } else {
            _first_empty_block = temp->next;
        }
    } else {
        if (tmp) {
            tmp->next = ((Block *)((char *)temp + temp->size))->next;
            ((Block *)((char *)temp + temp->size))->size = temp->size - sizeof(size_t) - N;
            ((Block *)((char *)temp + temp->size))->next = temp->next;
            temp->size = N + sizeof(size_t);
        } else {
            _first_empty_block = (Block *)((char *)temp + sizeof(size_t) + N);
            _first_empty_block->size = temp->size - sizeof(size_t) - N;
            _first_empty_block->next = temp->next;
            temp->size = N + sizeof(size_t);
        }
    }
    Pointer result((void **)_first_empty_descriptor);
    _first_empty_descriptor = *((void **)_first_empty_descriptor);
    *result._pointer = (void *)((size_t *)temp + 1);
    ++_number_descriptor;
    return result;
}

/**
 * TODO: semantics
 * @param p Pointer
 * @param N size_t
 */
void Simple::realloc(Pointer &p, size_t N) {
    if (p.get()) {
        size_t n;
        n = *((size_t *)p.get() - 1);
        if (n >= N) {
            if ((n - N) < (sizeof(Block))) {
                return;
            }
            Block *new_block = (Block *)((char *)p.get() + N);
            new_block->size = n - N - sizeof(size_t);
            Block *first = _first_empty_block, *second = nullptr;
            while (first && (first < p.get())) {
                second = first;
                first = first->next;
            }

            if (second) {
                second->next = new_block;

            } else {
                _first_empty_block = new_block;
            }
            new_block->next = first;
            _merge_blocks(new_block);
        } else {
            Block *first = _first_empty_block, *second = nullptr;
            while (first && (first < p.get())) { // Поиск блоков между которыми находится выделенный.
                second = first;
                first = first->next;
            }
            if ((void *)((char *)p.get() + *((size_t *)p.get() - 1) - sizeof(size_t)) == (void *)first) {
                if (first->size >= (N - n + sizeof(Block))) {
                    Block *temp = (Block *)((char *)first + (N - n));
                    if (second) {
                        second->next = temp;
                    } else {
                        _first_empty_block = temp;
                    }
                    temp->next = first->next;
                    temp->size = first->size + n - N;
                    *((size_t *)p.get() - 1) += N - n;
                } else {
                    if (second) {
                        second->next = first->next;
                    } else {
                        _first_empty_block = first->next;
                    }
                    *((size_t *)p.get() - 1) += first->size;
                }
            } else {
                Pointer result = alloc(N);
                memcpy(result.get(), p.get(), N);
                free(p);
                p = result;
            }
        }

    } else {
        p = alloc(N);
    }
}

/**
 * TODO: semantics
 * @param p Pointer
 */
void Simple::free(Pointer &p) {
    if (!p.get()) {
        return;
    }
    bool flag = false;
    void *temp = _first_empty_descriptor;
    for (size_t i = 0; i < _number_descriptor; ++i) {
        if (*((void **)_descriptor + i) == p.get()) {
            flag = true;
            while (temp) {
                if (temp == p.get()) {
                    flag = false;
                }
                temp = *((void **)temp);
            }
        }
    }
    if (!flag)
        return;
    if (p.get()) {
        Block *first, *second = nullptr;
        first = _first_empty_block;
        while (first && (first < p.get())) {
            second = first;
            first = first->next;
        }
        if (second) {
            second->next = (Block *)((size_t *)p.get() - 1);

        } else {
            _first_empty_block = (Block *)((size_t *)p.get() - 1);
        }
        *(void **)p.get() = first;
        _merge_blocks((Block *)((size_t *)p.get() - 1));
        _merge_blocks(second);

        *p._pointer = _first_empty_descriptor;
        _first_empty_descriptor = (void *)p._pointer;
    }
}

/**
 * TODO: semantics
 */
void Simple::defrag() {

    if (!_first_empty_block)
        return;
    size_t size_block;
    Block *pointer_block;
    Block *next_block;
    while ((void *)((char *)_first_empty_block + _first_empty_block->size) < _descriptor) {
        size_block = _first_empty_block->size;
        pointer_block = _first_empty_block->next;
        next_block = (Block *)((char *)_first_empty_block + _first_empty_block->size);
        memcpy((void *)_first_empty_block, next_block, *((size_t *)next_block) + sizeof(size_t));
        for (size_t i = 0; i < _number_descriptor; ++i) {
            if (*((void **)_descriptor + i) == (void *)((size_t *)next_block + 1)) {
                *((void **)_descriptor + i) = (void *)((size_t *)_first_empty_block + 1);
                break;
            }
        }
        _first_empty_block = (Block *)((char *)_first_empty_block + *(size_t *)_first_empty_block);
        _first_empty_block->size = size_block;
        _first_empty_block->next = pointer_block;
        _merge_blocks(_first_empty_block);
    }
}

/**
 * TODO: semantics
 */
std::string Simple::dump() const { return ""; }

void Simple::_find_empty_descriptor() {
    if (!_first_empty_descriptor) {
        Block *temp = _first_empty_block;
        while (temp->next) {
            temp = temp->next;
        }

        if ((temp->size >= 2 * (sizeof(size_t) + sizeof(void *))) &&
            ((void *)((char *)temp + temp->size) == _descriptor)) {
            temp->size -= sizeof(void *);
            _first_empty_descriptor = (void *)((char *)temp + temp->size);
            *((void **)_first_empty_descriptor) = nullptr;
        } else {
            defrag();
            if (_first_empty_block->size >= 2 * (sizeof(size_t) + sizeof(void *))) {
                _first_empty_block->size -= sizeof(void *);
                _first_empty_descriptor = (void *)((char *)_first_empty_block + _first_empty_block->size);
                (*((void **)_first_empty_descriptor)) = nullptr;
            } else {
                throw AllocError(AllocErrorType::NoMemory, "No Memory");
            }
        }
        _descriptor = (void *)((void **)_descriptor - 1);
    }
}

void Simple::_merge_blocks(Block *block) {
    if (block && block->next == (Block *)((char *)block + block->size)) {
        block->size += block->next->size;
        block->next = block->next->next;
    }
}

} // namespace Allocator
} // namespace Afina
