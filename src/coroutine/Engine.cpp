#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char this_point;
    int sig = (&this_point > StackBottom)?1:-1;
    std::get<1>(ctx.Stack) = uint32_t(sig*(&this_point - StackBottom));
    if (std::get<0>(ctx.Stack)) {
        free(std::get<0>(ctx.Stack));
    }
    std::get<0>(ctx.Stack) = (char *)malloc(sizeof(char) * std::get<1>(ctx.Stack));
    memcpy(std::get<0>(ctx.Stack), &this_point, std::get<1>(ctx.Stack));
}

void Recursive_point_offset(const char *point) {
    char this_point;
    if (&this_point > point) {
        Recursive_point_offset(point);
    }
}

void Engine::Restore(context &ctx) {
    char this_point;
    int sig = (&this_point > StackBottom)?1:-1;
    if (sig*(&this_point - StackBottom) < std::get<1>(ctx.Stack)) {
        Restore(ctx);
    }
    memcpy(StackBottom - std::get<1>(ctx.Stack), std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (alive) {
        context *next_routine = alive;
        while (next_routine == cur_routine){
            next_routine = next_routine->next;
        }
        if(alive == next_routine){
            alive = alive->next;
        }
        sched(next_routine);
    }
}

void Engine::sched(void *routine_) {
    context *called = reinterpret_cast<context *>(routine_);
    if (called && called != cur_routine) {
        if (cur_routine) {
            Store(*cur_routine);
            if (setjmp(cur_routine->Environment)) {
                return;
            }
        }
        cur_routine = called;
        Restore(*called);
    } else {
//        std::cout << "Yield\n";
        yield();
    }
}

} // namespace Coroutine
} // namespace Afina
