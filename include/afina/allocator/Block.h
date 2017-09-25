//
// Created by alexander on 21.09.17.
//

#ifndef AFINA_BLOCK_H
#define AFINA_BLOCK_H
namespace Afina {
namespace Allocator {
class Block {
public:
    size_t size;
    Block *next;
};
}
}
#endif // AFINA_BLOCK_H
