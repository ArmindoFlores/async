#include "coroutine.h"
#include "logging.h"
#include "async.h"
#include "dllist.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>

#if defined DEBUGGING || defined VALGRIND
    #include <valgrind/valgrind.h>
#endif

struct coroutine {
    int options;

    unsigned char *stack;
    size_t stack_size;

    coroutine_function_t func;
    void *arg;

    coroutine_state_e state;
    dllist_t *waiting_on;

    context_t ctx;

    void *return_value;

#if defined DEBUGGING || defined VALGRIND
    unsigned valgrind_stack_id;
#endif
};

static void _coro_run_trampoline();

coroutine_t* coro_create(coroutine_function_t f, void *arg, int options) {
    size_t stack_size = 64 * 1024;

    coroutine_t *co = malloc(sizeof *co);
    if (!co) return NULL;

    co->stack = malloc(stack_size);
    if (!co->stack) {
        free(co);
        return NULL;
    }

    co->stack_size = stack_size;
    co->func = f;
    co->arg = arg;
    co->state = CO_NEW;
    co->return_value = NULL;
    co->waiting_on = dllist_create(free);
    co->ctx = (context_t){};
    co->options = options;

    // Register this stack with valgrind when debugging
#if defined DEBUGGING || defined VALGRIND
    co->valgrind_stack_id = VALGRIND_STACK_REGISTER(
        co->stack,
        (char *)co->stack + co->stack_size
    );
#endif

    unsigned char *stack_top = co->stack + stack_size - sizeof(uintptr_t);
    uintptr_t *sp = (uintptr_t *)stack_top;

    // Align to 16 bytes
    sp = (uintptr_t *)((uintptr_t)sp & ~0xFUL);
    *sp = (uintptr_t) _coro_run_trampoline;

    // For the first time this coroutine runs, it should start at
    // `_coro_run_trampoline`
    co->ctx.rsp = sp;

    return co;
}

[[noreturn]] __attribute__((used)) static void _coro_run_trampoline() {
    //! IMPORTANT: this function always runs in a coroutine's stack

    // Get current context, fail if not found
    async_context_t *current_async_ctx = async_context_get_current();
    if (current_async_ctx == NULL) {
        abort();
    }
    coroutine_t *co = async_context_get_current_coroutine(current_async_ctx);
    if (co == NULL) {
        abort();
    }

    // Run coroutine function
    debugf("calling coroutine at %p\n", co);
    co->return_value = co->func(co->arg);
    debugf("coroutine at %p has finished with value %p\n", co, co->return_value);
    co->state = CO_FINISHED;
    
    // Switch back to scheduler context
    _context_switch(
        &co->ctx,
        async_context_get_stack_context(current_async_ctx)
    );
    __builtin_unreachable();
}

void coro_run(coroutine_t *co, context_t *from) {
    _context_switch(from, &co->ctx);
}

int coro_is_ready(coroutine_t *co) {
    if (co->state == CO_NEW) return 1;
    if (co->state == CO_SUSPENDED) {
        return dllist_is_empty(co->waiting_on);
    }
    return 0;
}

coroutine_state_e coro_get_state(coroutine_t *co) {
    return co->state;
}

void coro_set_state(coroutine_t *co, coroutine_state_e state) {
    co->state = state;
}

context_t *coro_get_stack_context(coroutine_t *co) {
    return &co->ctx;
}

void *coro_get_return_value(coroutine_t *co) {
    return co->return_value;
}

int coro_is_owned(coroutine_t *co) {
    return co->options & CORO_OPT_OWNED;
}

int coro_add_waiting(coroutine_t *co, awaitable_t awaitable) {
    awaitable_t *new_awaitable = malloc(sizeof(awaitable_t));
    if (new_awaitable == NULL) {
        errorf("failed to allocate memory for new awaitable\n");
        return -1;
    }
    memcpy(new_awaitable, &awaitable, sizeof(awaitable_t));
    return dllist_push_back(co->waiting_on, new_awaitable);
}

int _find_awaitable(void *_value, void *_arg) {
    awaitable_t *value = (awaitable_t*) _value, *arg = (awaitable_t*) _arg;
    if (value->type != arg->type) return 0;
    switch (arg->type) {
        case AWAITABLE_TYPE_FUTURE:
            return arg->future == value->future;
        case AWAITABLE_TYPE_FD:
            return arg->fd == value->fd;
    }
    return 0;
}

void coro_remove_waiting(coroutine_t *co, awaitable_t awaitable) {
    dllist_element_t *element = dllist_find_by_predicate(co->waiting_on, _find_awaitable, &awaitable);
    if (element == NULL) return;
    dllist_remove(co->waiting_on, element);
}

void coro_destroy(coroutine_t *co) {
    if (co == NULL) return; 
    // Unregister this stack with valgrind when debugging
#if defined DEBUGGING || defined VALGRIND
    VALGRIND_STACK_DEREGISTER(co->valgrind_stack_id);
#endif
    dllist_destroy(co->waiting_on);
    free(co->stack);
    free(co);
}
