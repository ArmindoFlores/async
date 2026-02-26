#ifndef _H_COROUTINE_
#define _H_COROUTINE_

#include <stddef.h>
#include "async_types.h"
#include "awaitable.h"

typedef enum coroutine_state {
    CO_NEW,
    CO_RUNNING,
    CO_SUSPENDED,
    CO_FINISHED,
    CO_FAILED
} coroutine_state_e;

typedef struct context {
    void *rsp;
    void *rbx;
    void *rbp;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
} context_t;

typedef enum coroutine_option {
    CORO_OPT_OWNED = 1
} coroutine_option_e;

extern void _context_switch(context_t *from, context_t *to);
coroutine_t* coro_create(coroutine_function_t f, void *arg, int options);
void coro_run(coroutine_t *, context_t *from);
int coro_add_waiting(coroutine_t *, awaitable_t);
void coro_remove_waiting(coroutine_t *, awaitable_t);
int coro_is_ready(coroutine_t *);
coroutine_state_e coro_get_state(coroutine_t *);
void coro_set_state(coroutine_t *, coroutine_state_e);
context_t *coro_get_stack_context(coroutine_t *);
void *coro_get_return_value(coroutine_t *);
int coro_is_owned(coroutine_t *);
void coro_destroy(coroutine_t*);

#endif
