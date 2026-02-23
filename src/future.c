#include "future.h"
#include "async.h"
#include "logging.h"
#include <stdarg.h>
#include <stdlib.h>

struct future {
    coroutine_t *coroutine;
    void *value;
    future_state_e state;
    coroutine_queue_t waited_on_by;
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

    for (size_t i = 0; i < arg->size; i++) {
        debugf("awaiting future at %p (state=%d)\n", arg->arr[i], arg->arr[i]->state);
        async_await_future(arg->arr[i]);
    }

    free(arg);
    return NULL;
}

future_t *future_create_from_function(coroutine_function_t func, void *arg) {
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

    // Create a new coroutine for the given function and add it to the
    // scheduled queue
    coroutine_t *new_co = coro_create(_coroutine_future_wrapper, wrapper_arg, 0);
    if (new_co == NULL) {
        errorf("failed create coroutine new coroutine to await\n");
        free(result);
        return NULL;
    }
    if (async_schedule_coroutine(current_async_ctx, new_co)) {
        errorf("failed to add coroutine at %p to scheduled queue\n");
        free(result);
        coro_destroy(new_co);
        return NULL;
    }

    *result = (future_t){
        .coroutine = new_co,
        .waited_on_by = {0},
        .state = FUTURE_PENDING,
        .value = NULL
    };

    return result;
}

int future_add_waiting(future_t *waited, coroutine_t *waiting) {
    if (coro_queue_push(&waited->waited_on_by, waiting) != 0) {
        return -1;
    }
    coro_add_waiting(waiting);
    return 0;
}

void *future_get_return_value(future_t *f) {
    return f->value;
}

future_state_e future_get_state(future_t *f) {
    return f->state;
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

    future_t *result = future_create_from_function(_future_all_wrapper, arg);
    if (result == NULL) {
        free(arg);
        return NULL;
    }

    return result;
}

void future_destroy(future_t *f) {
    if (f == NULL) return;
    if (f->state == FUTURE_PENDING) {
        errorf("attempting to free a pending future at %p\n", f);
        return;
    }
    coro_queue_destroy(&f->waited_on_by);
    free(f);
}
