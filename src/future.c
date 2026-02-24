#include "future.h"
#include "async.h"
#include "logging.h"
#include <stdarg.h>
#include <stdlib.h>
#include <threads.h>

struct future {
    async_context_t *ctx;
    coroutine_t *coroutine;
    void *value;
    future_state_e state;
    coroutine_queue_t waited_on_by;

    int is_locked;
    mtx_t lock;
};

void _future_notify_waiting(future_t *f) {
    for (struct coroutine_queue_node *cur = f->waited_on_by.head; cur != NULL; cur = cur->next) {
        coro_notify_waiting(cur->element);
    }
}

struct coroutine_future_wrapper_args {
    void *original_arg;
    future_t *future;
    coroutine_function_t original_func;
};

void *_coroutine_future_wrapper(void *_arg) {
    struct coroutine_future_wrapper_args *arg = (struct coroutine_future_wrapper_args*) _arg;
    void *result = arg->original_func(arg->original_arg);

    // Update the future after the coroutine has finished
    arg->future->value = result;
    arg->future->state = FUTURE_RESOLVED;
    arg->future->coroutine = NULL;

    // Notify all coroutines awaiting this future
    _future_notify_waiting(arg->future);

    free(arg);

    return result;
}

struct future_all_wrapper_args {
    future_t **arr;
    size_t size;
};

void *_future_all_wrapper(void *_arg) {
    struct future_all_wrapper_args *arg = (struct future_all_wrapper_args*) _arg;

    future_all_result_t *result = malloc(sizeof(future_all_result_t));
    if (result == NULL) {
        errorf("failed to allocate memory for future_all_result_t\n");
        return NULL;
    }
    result->future_arr = malloc(sizeof(void*) * arg->size);
    if (result->future_arr == NULL) {
        free(result);
        errorf("failed to allocate memory for future_all_result_t\n");
        return NULL;
    }
    result->n = arg->size;

    for (size_t i = 0; i < arg->size; i++) {
        debugf("awaiting future at %p (state=%d)\n", arg->arr[i], arg->arr[i]->state);
        result->future_arr[i] = async_await_future(arg->arr[i]);
    }

    free(arg);
    return result;
}

void _future_lock_guard_begin(future_t *f) {
    if (!f->is_locked) return;
    if (mtx_lock(&f->lock) != thrd_success) {
        errorf("failed to release lock for future at %p\n", f);
        abort();
    }
}

void _future_lock_guard_end(future_t *f) {
    if (!f->is_locked) return;
    if (mtx_unlock(&f->lock) != thrd_success) {
        errorf("failed to release lock for future at %p\n", f);
        abort();
    }
}

future_t *future_create(int options) {
    future_t *result = malloc(sizeof(future_t));
    if (result == NULL) {
        errorf("failed to allocate memory for a future\n");
        return NULL;
    }

    if (options & FUT_OPT_EAGER) {
        warnf("a future created with future_create() cannot be eager\n");
    }

    int thread_protected = options & FUT_OPT_THREADED;
    *result = (future_t){
        .ctx = async_context_get_current(),
        .coroutine = NULL,
        .waited_on_by = {0},
        .state = FUTURE_NEW,
        .value = NULL,
        .is_locked = thread_protected ? 1 : 0
    };

    if (thread_protected) {
        if (mtx_init(&result->lock, mtx_plain) != thrd_success) {
            errorf("error creating lock for future\n");
            result->is_locked = 0;
            future_destroy(result);
            return NULL;
        }
    }

    return result;
}

future_t *future_create_from_function(coroutine_function_t func, void *arg, int options) {
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

    future_t *result = malloc(sizeof(future_t));
    if (result == NULL) {
        errorf("failed to allocate memory for a future\n");
        return NULL;
    }

    struct coroutine_future_wrapper_args *wrapper_arg = malloc(sizeof(struct coroutine_future_wrapper_args));
    if (wrapper_arg == NULL) {
        errorf("failed to allocate memory for a future\n");
        return NULL;
    }

    *wrapper_arg = (struct coroutine_future_wrapper_args){
        .future = result,
        .original_arg = arg,
        .original_func = func
    };

    // Create a new coroutine for the given function
    coroutine_t *new_co = coro_create(_coroutine_future_wrapper, wrapper_arg, 0);
    if (new_co == NULL) {
        errorf("failed create coroutine new coroutine to await\n");
        free(result);
        return NULL;
    }

    // Add future coroutine to schedule if specified as eager
    int eager = options & FUT_OPT_EAGER;
    if (eager) {
        if (async_schedule_coroutine(current_async_ctx, new_co)) {
            errorf("failed to add coroutine at %p to scheduled queue\n");
            free(result);
            coro_destroy(new_co);
            return NULL;
        }
    }

    *result = (future_t){
        .ctx = current_async_ctx,
        .coroutine = new_co,
        .waited_on_by = {0},
        .state = eager ? FUTURE_PENDING : FUTURE_NEW,
        .value = NULL,
        .is_locked = 0
    };

    return result;
}

int future_start(future_t *f) {
    return async_schedule_coroutine(f->ctx, f->coroutine);
}

int future_add_waiting(future_t *waited, coroutine_t *waiting) {
    _future_lock_guard_begin(waited);
    if (coro_queue_push(&waited->waited_on_by, waiting) != 0) {
        _future_lock_guard_end(waited);
        return -1;
    }
    _future_lock_guard_end(waited);
    coro_add_waiting(waiting);
    return 0;
}

void *future_get_return_value(future_t *f) {
    _future_lock_guard_begin(f);
    void *value = f->value;
    _future_lock_guard_end(f);
    return value;
}

future_state_e future_get_state(future_t *f) {
    _future_lock_guard_begin(f);
    future_state_e state = f->state;
    _future_lock_guard_end(f);
    return state;
}

void future_set_state(future_t *f, future_state_e state) {
    _future_lock_guard_begin(f);
    f->state = state;
    _future_lock_guard_end(f);
}

void future_resolve(future_t *f, void *result) {
    _future_lock_guard_begin(f);
    if (f->state != FUTURE_PENDING) {
        _future_lock_guard_end(f);
        return;
    }
    f->state = FUTURE_RESOLVED;
    f->value = result;
    async_signal_scheduler(f->ctx);
    _future_lock_guard_end(f);
    _future_notify_waiting(f);
}

void future_reject(future_t *f) {
    _future_lock_guard_begin(f);
    if (f->state != FUTURE_PENDING) {
        _future_lock_guard_end(f);
        return;
    }
    f->state = FUTURE_REJECTED;
    async_signal_scheduler(f->ctx);
    _future_lock_guard_end(f);
    _future_notify_waiting(f);
}

future_t *future_all(future_t **future_array, size_t n_members) {
    struct future_all_wrapper_args *arg = malloc(sizeof(struct future_all_wrapper_args));
    if (arg == NULL) {
        return NULL;
    }

    *arg = (struct future_all_wrapper_args){
        .arr = future_array,
        .size = n_members
    };

    future_t *result = future_create_from_function(_future_all_wrapper, arg, 0);
    if (result == NULL) {
        free(arg);
        return NULL;
    }

    return result;
}

void future_all_free_result(future_all_result_t *result) {
    if (result == NULL) return;
    free(result->future_arr);
    free(result);
}

void future_destroy(future_t *f) {
    if (f == NULL) return;
    _future_lock_guard_begin(f);
    if (f->state == FUTURE_PENDING) {
        _future_lock_guard_end(f);
        errorf("attempting to free a pending future at %p\n", f);
        return;
    }
    coro_queue_destroy(&f->waited_on_by);
    if (f->is_locked) {
        mtx_destroy(&f->lock);
    }
    free(f);
}
