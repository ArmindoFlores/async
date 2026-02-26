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
    void (*free_value)(void *);

    future_state_e state;
    dllist_t *waited_on_by;

    int is_taken, is_locked;
    mtx_t lock;
};

iteration_result_e _future_notify_waiting_iterator_helper(dllist_element_t *, void *value, void *awaitable) {
    coro_remove_waiting((coroutine_t*) value, *(awaitable_t*) awaitable);
    return ITERATION_CONTINUE;
}

void _future_notify_waiting(future_t *f) {
    awaitable_t awaitable = AWAITABLE_FUTURE(f);
    dllist_iterate_with_args(f->waited_on_by, _future_notify_waiting_iterator_helper, &awaitable);
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
    int take_futures;
};

void *_future_all_wrapper(void *_arg) {
    struct future_all_wrapper_args *arg = (struct future_all_wrapper_args*) _arg;

    future_all_result_t *result = malloc(sizeof(future_all_result_t));
    if (result == NULL) {
        errorf("failed to allocate memory for future_all_result_t\n");
        return NULL;
    }
    result->future_arr = malloc(sizeof(future_all_result_element_t) * arg->size);
    if (result->future_arr == NULL) {
        free(result);
        errorf("failed to allocate memory for future_all_result_t\n");
        return NULL;
    }
    result->n = arg->size;

    for (size_t i = 0; i < arg->size; i++) {
        debugf("awaiting future at %p (state=%d)\n", arg->arr[i], arg->arr[i]->state);
        result->future_arr[i] = (future_all_result_element_t){
            .value = async_await_future(arg->arr[i]),
            .free_value = arg->take_futures ? future_get_free_result_func(arg->arr[i]) : NULL
        };
        if (arg->take_futures) {
            // Own the value of each future so we can destroy them now
            (void) future_take_return_value(arg->arr[i]);
            future_destroy(arg->arr[i]);
        }
    }

    free(arg);
    return result;
}

void _future_all_free_result(void *_result) {
    future_all_result_t *result = (future_all_result_t *) _result;
    if (result == NULL) return;
    for (size_t i = 0; i < result->n; i++) {
        if (result->future_arr[i].free_value == NULL) continue;
        if (result->future_arr[i].value == NULL) continue;
        result->future_arr[i].free_value(result->future_arr[i].value);
    }
    free(result->future_arr);
    free(result);
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
        .waited_on_by = dllist_create(NULL),
        .state = FUTURE_NEW,
        .value = NULL,
        .free_value = NULL,
        .is_locked = thread_protected ? 1 : 0,
        .is_taken = 0
    };

    if (result->waited_on_by == NULL) {
        errorf("failed to allocate memory for a future\n");
        free(result);        
        return NULL;
    }

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

    dllist_t *waited_on_by = dllist_create(NULL);
    if (waited_on_by == NULL) {
        errorf("failed to allocate memory for a future\n");
        free(result);
        return NULL;
    }

    struct coroutine_future_wrapper_args *wrapper_arg = malloc(sizeof(struct coroutine_future_wrapper_args));
    if (wrapper_arg == NULL) {
        errorf("failed to allocate memory for a future\n");
        dllist_destroy(waited_on_by);
        free(result);
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
        dllist_destroy(waited_on_by);
        free(result);
        return NULL;
    }

    // Add future coroutine to schedule if specified as eager
    int eager = options & FUT_OPT_EAGER;
    if (eager) {
        if (async_schedule_coroutine(current_async_ctx, new_co)) {
            errorf("failed to add coroutine at %p to scheduled queue\n");
            dllist_destroy(waited_on_by);
            free(result);
            coro_destroy(new_co);
            return NULL;
        }
    }

    *result = (future_t){
        .ctx = current_async_ctx,
        .coroutine = new_co,
        .waited_on_by = waited_on_by,
        .state = eager ? FUTURE_PENDING : FUTURE_NEW,
        .value = NULL,
        .free_value = NULL,
        .is_locked = 0,
        .is_taken = 0
    };

    return result;
}

int future_start(future_t *f) {
    if (f->coroutine == NULL) return 0;
    return async_schedule_coroutine(f->ctx, f->coroutine);
}

int future_add_waiting(future_t *waited, coroutine_t *waiting) {
    _future_lock_guard_begin(waited);
    if (dllist_push_back(waited->waited_on_by, waiting) != 0) {
        _future_lock_guard_end(waited);
        return -1;
    }
    _future_lock_guard_end(waited);
    coro_add_waiting(waiting, AWAITABLE_FUTURE(waited));
    return 0;
}

void *future_borrow_return_value(future_t *f) {
    _future_lock_guard_begin(f);
    void *value = f->value;
    _future_lock_guard_end(f);
    return value;
}

void *future_take_return_value(future_t *f) {
    _future_lock_guard_begin(f);
    if (f->is_taken) {
        errorf("tried to take the value of future %p when it was already taken\n", f);
        return NULL;
    }
    void *value = f->value;
    f->is_taken = 1;
    _future_lock_guard_end(f);
    return value;
}

free_function_t future_get_free_result_func(future_t *f) {
    _future_lock_guard_begin(f);
    free_function_t result = f->free_value;
    _future_lock_guard_end(f);
    return result;
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

void future_resolve(future_t *f, void *result, free_function_t free_result) {
    _future_lock_guard_begin(f);
    if (f->state != FUTURE_PENDING) {
        _future_lock_guard_end(f);
        return;
    }
    f->state = FUTURE_RESOLVED;
    f->value = result;
    f->free_value = free_result;
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

future_t *future_all(future_t **future_array, size_t n_members, int take_futures) {
    struct future_all_wrapper_args *arg = malloc(sizeof(struct future_all_wrapper_args));
    if (arg == NULL) {
        return NULL;
    }

    *arg = (struct future_all_wrapper_args){
        .arr = future_array,
        .size = n_members,
        .take_futures = take_futures
    };

    future_t *result = future_create_from_function(_future_all_wrapper, arg, 0);
    result->free_value = _future_all_free_result;
    if (result == NULL) {
        free(arg);
        return NULL;
    }

    return result;
}

void future_destroy(future_t *f) {
    if (f == NULL) return;
    _future_lock_guard_begin(f);
    if (f->state == FUTURE_PENDING) {
        _future_lock_guard_end(f);
        errorf("attempting to free a pending future at %p\n", f);
        return;
    }
    if (!f->is_taken && f->free_value != NULL && f->value != NULL) {
        f->free_value(f->value);
    }
    dllist_destroy(f->waited_on_by);
    if (f->is_locked) {
        mtx_destroy(&f->lock);
    }
    free(f);
}
