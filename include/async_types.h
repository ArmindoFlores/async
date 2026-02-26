#ifndef _H_ASYNC_TYPES_
#define _H_ASYNC_TYPES_

typedef struct coroutine coroutine_t;
typedef struct future future_t;
typedef struct async_context async_context_t;

typedef void (*dispatch_function_t)(future_t*, void *arg);
typedef void*(*coroutine_function_t)(void*);
typedef struct coroutine coroutine_t;

#endif
