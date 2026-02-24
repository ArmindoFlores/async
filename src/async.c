#include "async.h"
#include "future.h"
#include "logging.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <threads.h>

static _Thread_local async_context_t *_async_ctx_current = NULL;

typedef struct pollfd_array {
    struct pollfd *elements;
    size_t size;
    size_t capacity;
} pollfd_array_t;

struct wakeup_fds {
    int read;
    int write;
};

struct async_context {
    coroutine_queue_t scheduled_coroutines;
    coroutine_t *current;

    pollfd_array_t watched_file_descriptors;
    struct wakeup_fds wakeup_fds;

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

int _pollfd_array_ensure_capacity(pollfd_array_t *array, size_t capacity) {
    if (capacity <= array->capacity) {
        return 0;
    }
    array->capacity *= 2;
    array->elements = realloc(array->elements, sizeof(struct pollfd) * array->capacity);
    return array->elements == NULL ? -1 : 0;
}

int _pollfd_array_push(pollfd_array_t *array, int fd, short int events) {
    if (_pollfd_array_ensure_capacity(array, array->size + 1) != 0) {
        return -1;
    }
    array->elements[array->size++] = (struct pollfd){
        .fd = fd,
        .events = events,
        .revents = 0
    };
    return 0;
}

void _pollfd_array_free(pollfd_array_t *array) {
    array->capacity = 0;
    array->size = 0;
    free(array->elements);
    array->elements = NULL;
}

int _wakeup_fds_init(struct wakeup_fds *w) {
    int fds[2];

    if (pipe(fds) < 0)
        return -1;

    w->read  = fds[0];
    w->write = fds[1];

    if (fcntl(w->read, F_SETFL, O_NONBLOCK) < 0 || fcntl(w->write, F_SETFL, O_NONBLOCK) < 0) {
        close(w->read);
        close(w->write);
        return -1;
    }

    return 0;
}

int _wakeup_fds_signal(struct wakeup_fds *w) {
    ssize_t n = write(w->write, &(uint8_t){1}, 1);
    if (n < 0) {
        if (errno == EAGAIN)
            return 0;  // pipe full; scheduler will wake up anyway
        return -1;
    }
    return 0;
}

void _wakeup_fds_drain(struct wakeup_fds *w) {
    uint8_t buffer[128];

    while (1) {
        ssize_t n = read(w->read, buffer, sizeof(buffer));
        if (n <= 0)
            break;
    }
}

void _wakeup_fds_free(struct wakeup_fds *w) {
    if (w->read >= 0) close(w->read);
    if (w->write >= 0) close(w->write);

    w->read  = -1;
    w->write = -1;
}

async_context_t* async_context_create() {
    async_context_t *ctx = malloc(sizeof(async_context_t));
    if (ctx == NULL) {
        return NULL;
    }
    if (_wakeup_fds_init(&ctx->wakeup_fds)) {
        free(ctx);
        return NULL;
    }
    if (_pollfd_array_init(&ctx->watched_file_descriptors) != 0) {
        free(ctx);
        return NULL;
    }
    _pollfd_array_push(&ctx->watched_file_descriptors, ctx->wakeup_fds.read, POLLIN);
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
        if (ctx->watched_file_descriptors.elements[0].revents & POLLIN) {
            // ctx->watched_file_descriptors.elements[0] is guaranteed to be wakeup_fd
            debugf("poll woken up through signal\n");
            _wakeup_fds_drain(&ctx->wakeup_fds);
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

void async_signal_scheduler(async_context_t *ctx) {
    if (ctx == NULL) {
        ctx = async_context_get_current();
    }
    if (ctx == NULL) {
        errorf("no async context to signal\n");
        abort();
    }
    while (_wakeup_fds_signal(&ctx->wakeup_fds) != 0) {
        errorf("failed to signal scheduler, trying again\n");
        thrd_yield();
    }
}

struct dispatch_thread_wrapper_arg {
    dispatch_function_t func;
    void *original_arg;
    future_t *future;
};

static int _dispatch_thread_wrapper(void *_arg) {
    struct dispatch_thread_wrapper_arg *arg = (struct dispatch_thread_wrapper_arg*) _arg;
    future_set_state(arg->future, FUTURE_PENDING);
    arg->func(arg->future, arg->original_arg);
    free(arg);
    return 0;
}

future_t *async_dispatch(dispatch_function_t f, void *arg) {
    struct dispatch_thread_wrapper_arg *dispatch_arg = malloc(sizeof(struct dispatch_thread_wrapper_arg));
    if (dispatch_arg == NULL) {
        errorf("failed to allocate memory for dispatched thread arguments\n");
        return NULL;
    }

    future_t *result = future_create(FUT_OPT_THREADED);
    if (result == NULL) {
        errorf("failed to allocate memory for dispatched thread future\n");
        free(dispatch_arg);
        return NULL;
    }

    *dispatch_arg = (struct dispatch_thread_wrapper_arg){
        .func = f,
        .future = result,
        .original_arg = arg
    };

    thrd_t thread;
    if (thrd_create(&thread, _dispatch_thread_wrapper, dispatch_arg) != thrd_success) {
        errorf("failed to spawn thread\n");
        free(dispatch_arg);
        future_destroy(result);
        return NULL;
    }
    if (thrd_detach(thread) != thrd_success) {
        warnf("failed to detach thread, memory will be leaked\n");
    }

    return result;
}

void* async_await_future(future_t *f) {
    // FIXME: race condition, need to acquire f->lock
    future_state_e state = future_get_state(f);
    if (state == FUTURE_RESOLVED) {
        return future_borrow_return_value(f);
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

    // If the future is not executing yet, schedule it
    if (state == FUTURE_NEW) {
        if (future_start(f) != 0) {
            errorf("failed to schedule future at %p\n", f);
            return NULL;
        }
    }

    if (future_add_waiting(f, co) != 0) {
        errorf("failed to add coroutine at %p to waiting list of future at %p\n", co, f);
        return NULL;
    }

    _async_yield(current_async_ctx, co);
    if (future_get_state(f) != FUTURE_RESOLVED) {
        return NULL;
    }
    void *result = future_borrow_return_value(f);
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
    _wakeup_fds_free(&ctx->wakeup_fds);
    free(ctx);
}
