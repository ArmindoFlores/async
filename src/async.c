#include "async.h"
#include "future.h"
#include "logging.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

static _Thread_local async_context_t *_async_ctx_current = NULL;

typedef struct pollfd_array {
    struct pollfd *elements;
    size_t size;
    size_t capacity;
} pollfd_array_t;

struct async_context {
    coroutine_queue_t scheduled_coroutines;
    coroutine_t *current;

    pollfd_array_t watched_file_descriptors;

    context_t scheduler_ctx;
};

struct next_coroutine_result {
    coroutine_t *co;
};

int _pollfd_array_init(pollfd_array_t *array) {
    array->capacity = 16;
    array->size = 0;
    array->elements = malloc(sizeof(struct pollfd) * array->capacity);
    if (array->elements == NULL) {
        return -1;
    }
    return 0;
}

void _pollfd_array_free(pollfd_array_t *array) {
    array->capacity = 0;
    array->size = 0;
    free(array->elements);
    array->elements = NULL;
}

async_context_t* async_context_create() {
    async_context_t *ctx = malloc(sizeof(async_context_t));
    if (ctx == NULL) {
        return NULL;
    }
    if (_pollfd_array_init(&ctx->watched_file_descriptors) != 0) {
        free(ctx);
        return NULL;
    }
    ctx->scheduled_coroutines.head = NULL;
    ctx->scheduled_coroutines.tail = NULL;
    return ctx;
}

async_context_t* async_context_get_current() {
    return _async_ctx_current;
}

coroutine_t* async_context_get_current_coroutine(async_context_t *ctx) {
    return ctx->current;
}

context_t* async_context_get_stack_context(async_context_t *ctx) {
    return &ctx->scheduler_ctx;
}

coroutine_t* _async_next_coroutine(async_context_t *ctx) {
    for (struct coroutine_queue_node* cur = ctx->scheduled_coroutines.head; cur != NULL; cur = cur->next) {
        coroutine_t *co = cur->element;
        if ((coro_get_state(co) == CO_SUSPENDED && coro_is_ready(co)) || coro_get_state(co) == CO_NEW) {
            if (cur->previous) {
                cur->previous->next = cur->next;
            }
            if (cur->next) {
                cur->next->previous = cur->previous;
            }
            if (ctx->scheduled_coroutines.head == cur) {
                ctx->scheduled_coroutines.head = cur->next;
            }
            if (ctx->scheduled_coroutines.tail == cur) {
                ctx->scheduled_coroutines.tail = cur->previous;
            }
            free(cur);
            return co;
        }
    }
    return NULL;
}

int _async_main_loop(async_context_t *ctx) {
    debugf("started async context main loop (%p)\n", ctx);
    _async_ctx_current = ctx;
    while (1) {
        coroutine_t *co = NULL;
        while ((co = _async_next_coroutine(ctx)) != NULL) {
            // _async_next_coroutine() has removed `co` from the queue
            ctx->current = co;
            debugf("switching context to coroutine at %p\n", co);
            coro_run(co, &ctx->scheduler_ctx);
            debugf("switched context back to scheduler from coroutine at %p\n", co);
    
            // When the coroutine yields or finishes, it will
            // do _context_switch(&co->ctx, &ctx->scheduler_ctx),
            // and execution will resume here.
    
            if (coro_get_state(co) == CO_FINISHED) {
                // Coroutine has finished and can be removed from scheduled queue
                coro_notify_waiting(co);
                if (!coro_is_owned(co)) {
                    // This coroutine has no owner and must be destroyed by the async
                    // context
                    coro_destroy(co);
                }
                debugf("coroutine at %p has finished\n", co);
            } else if (coro_get_state(co) == CO_SUSPENDED) {
                // Coroutine has yielded control but is still scheduled; place it
                // back at the end of the queue
                debugf("coroutine at %p has not finished, adding it to queue\n", co);
                coro_queue_push(&ctx->scheduled_coroutines, co);
            } else {
                errorf("coroutine at %p was left in an invalid state\n", co);
                abort();
            }
        }

        ctx->current = NULL;

        if (ctx->scheduled_coroutines.head == NULL) {
            // No more scheduled coroutines, stop the main loop
            debugf("no more scheduled coroutines, stopping main loop (%p)\n", ctx);
            break;
        }
        
        // TODO: handle a possible timeout through minheap of timers
        int poll_result = poll(
            ctx->watched_file_descriptors.elements,
            ctx->watched_file_descriptors.size,
            1000 // TODO: figure out the timeout
        );
        if (poll_result == -1) {
            debugf("poll() returned an error: '%s'\n", strerror(errno));
        }
    }

    debugf("finished async context main loop (%p)\n", ctx);
    _async_ctx_current = NULL;
    return 0;
}

int async_context_run(async_context_t *ctx, coroutine_function_t entrypoint, void *arg) {
    coroutine_t *co = coro_create(entrypoint, arg, CORO_OPT_OWNED);
    if (coro_queue_push(&ctx->scheduled_coroutines, co)) {
        return -1;
    }
    
    int result = _async_main_loop(ctx);
    coro_destroy(co);
    return result;
}

int async_schedule_coroutine(async_context_t *ctx, coroutine_t *co) {
    return coro_queue_push(&ctx->scheduled_coroutines, co);
}

void _async_yield(async_context_t *ctx, coroutine_t *co) {
    coro_set_state(co, CO_SUSPENDED);

    debugf("yielding from coroutine %p\n", co);
    _context_switch(
        coro_get_stack_context(co),
        async_context_get_stack_context(ctx)
    );
}

void async_yield() {
    async_context_t *current_async_ctx = async_context_get_current();
    if (current_async_ctx == NULL) {
        errorf("running coroutine outside async context\n");
        abort();
    }
    coroutine_t *co = async_context_get_current_coroutine(current_async_ctx);
    if (co == NULL) {
        errorf("running coroutine outside async context\n");
        abort();
    }

    return _async_yield(current_async_ctx, co);
}

void* async_await_future(future_t *f) {
    future_state_e state = future_get_state(f);
    if (state == FUTURE_RESOLVED) {
        return future_get_return_value(f);
    }
    else if (state == FUTURE_REJECTED) { 
        return NULL;
    }

    async_context_t *current_async_ctx = async_context_get_current();
    if (current_async_ctx == NULL) {
        errorf("running coroutine outside async context\n");
        abort();
    }
    coroutine_t *co = async_context_get_current_coroutine(current_async_ctx);
    if (co == NULL) {
        errorf("running coroutine outside async context\n");
        abort();
    }

    if (future_add_waiting(f, co) != 0) {
        errorf("failed to add coroutine at %p to waiting list of future at %p\n", co, f);
        return NULL;
    }

    _async_yield(current_async_ctx, co);
    void *result = future_get_return_value(f);
    debugf("future at %p resolved, back to coroutine at %p\n", f, co);

    return result;
}

void* async_await_function(coroutine_function_t func, void *arg) {
    async_yield();
    return func(arg);
}

void async_context_destroy(async_context_t *ctx) {
    if (ctx == NULL) return;
    coro_queue_destroy(&ctx->scheduled_coroutines);
    _pollfd_array_free(&ctx->watched_file_descriptors);
    free(ctx);
}
