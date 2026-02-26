#ifndef _H_AWAITABLE_
#define _H_AWAITABLE_

#include "async_types.h"

#define AWAITABLE_FUTURE(f) ((awaitable_t){.type=AWAITABLE_TYPE_FUTURE,.future=f})
#define AWAITABLE_FD(fd) ((awaitable_t){.type=AWAITABLE_TYPE_FD,.fd=fd})

typedef struct async_context async_context_t;
typedef void (*dispatch_function_t)(future_t*, void *arg);

typedef enum awaitable_type {
    AWAITABLE_TYPE_FUTURE,
    AWAITABLE_TYPE_FD
} awaitable_type_e;

typedef struct awaitable {
    awaitable_type_e type;
    union {
        future_t *future;
        int fd;
    };
} awaitable_t;

#endif
