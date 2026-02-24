#ifndef _H_FUTURE_
#define _H_FUTURE_

#include "coroutine.h"
#include <stddef.h>

typedef struct future future_t;
typedef void (*free_function_t)(void*);

typedef enum future_state {
    FUTURE_NEW,
    FUTURE_PENDING,
    FUTURE_RESOLVED,
    FUTURE_REJECTED
} future_state_e;

typedef struct future_all_result_element {
    void *value;
    free_function_t free_value;
} future_all_result_element_t;

typedef struct future_all_result {
    size_t n;
    future_all_result_element_t *future_arr;
} future_all_result_t;

typedef enum future_option {
    FUT_OPT_EAGER = 1,
    FUT_OPT_THREADED = 2
} future_option_e;

future_t *future_create(int options);
future_t *future_create_from_function(coroutine_function_t func, void *arg, int options);
int future_start(future_t *);
int future_add_waiting(future_t *, coroutine_t *waiting);
void *future_borrow_return_value(future_t *);
void *future_take_return_value(future_t *);
free_function_t future_get_free_result_func(future_t *);
void future_resolve(future_t *, void *result, free_function_t free_result);
void future_reject(future_t *);
void future_set_state(future_t *, future_state_e);
future_state_e future_get_state(future_t *f);
future_t *future_all(future_t **future_array, size_t n_members, int take_futures);
void future_all_free_result(future_all_result_t *);
void future_destroy(future_t *);

#endif
