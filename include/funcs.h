#ifndef _H_FUNCS_
#define _H_FUNCS_

#include "async.h"
#include "future.h"

typedef struct async_spawn_result {
    char *stdout;
    int status;
} async_spawn_result_t;

future_t *async_spawn(const char *command);
void async_spawn_free_result(async_spawn_result_t *);

#endif
