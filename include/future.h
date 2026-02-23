#ifndef _H_FUTURE_
#define _H_FUTURE_

#include "coroutine.h"
#include <stddef.h>

typedef struct future future_t;

typedef enum future_state {
    FUTURE_NEW,
    FUTURE_PENDING,
    FUTURE_RESOLVED,
    FUTURE_REJECTED
} future_state_e;

typedef enum future_option {
    FUT_OPT_EAGER = 1
} future_option_e;

future_t *future_create_from_function(coroutine_function_t func, void *arg, int options);
int future_start(future_t *);
int future_add_waiting(future_t *, coroutine_t *waiting);
void *future_get_return_value(future_t *);
future_state_e future_get_state(future_t *f);
future_t *future_all(future_t **future_array, size_t n_members);
void future_destroy(future_t *);

#endif
