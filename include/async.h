#ifndef _H_ASYNC_
#define _H_ASYNC_

#include <stdint.h>
#include "coroutine.h"
#include "future.h"

typedef struct async_context async_context_t;

async_context_t* async_context_create();
context_t* async_context_get_stack_context(async_context_t *);
async_context_t* async_context_get_current();
coroutine_t* async_context_get_current_coroutine(async_context_t *);
int async_context_run(async_context_t *, coroutine_function_t entrypoint, void *arg);
int async_schedule_coroutine(async_context_t *, coroutine_t *);
void async_yield();
void* async_await_future(future_t *f);
void* async_await_function(coroutine_function_t, void *arg);
void async_context_destroy(async_context_t *);

#endif
