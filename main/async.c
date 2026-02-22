#include <stdio.h>
#include "async.h"

void *y(void *arg) {
    char *str = (char*)arg;
    printf("Hello, %s!\n", str);
    return (void*) 99;
}

void *f(void *arg) {
    int x = *(int*)arg;
    printf("Coroutine says %d!\n", x);
    async_await(y, "Francisco");
    printf("Coroutine says %d!\n", x + 10);
    return (void*) 1337;
}

int main() {
    async_context_t *ctx = async_context_create();
    if (ctx == NULL) {
        fputs("Failed to create async context.", stderr);
        return 1;
    }

    if (async_context_run(ctx, f, &(int){42}) != 0) {
        fputs("Error in async context.", stderr);
        return 1;
    }

    async_context_destroy(ctx);
}
