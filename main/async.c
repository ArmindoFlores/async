#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <threads.h>
#include "async.h"
#include "funcs.h"
#include "logging.h"

void *y(void *arg) {
    char *str = (char*)arg;
    infof("Hello, %s!\n", str);
    return (void*) 99;
}

void *f(void *arg) {
    int x = *(int*)arg;
    infof("Coroutine says %d!\n", x);

    future_t *future1 = future_create_from_function(y, "Francisco", FUT_OPT_EAGER);
    future_t *future2 = future_create_from_function(y, "Armindo", FUT_OPT_EAGER);
    future_t *future3 = async_spawn("ping -c 5 www.google.com");
    future_t *all = future_all((future_t*[]){future1, future2, future3}, 3);

    future_all_result_t *results = async_await_future(all);
    if (results != NULL) {
        for (size_t i = 0; i < results->n; i++) {
            infof("async_await_future[%lu] = %p\n", i, results->future_arr[i]);
        }
        async_spawn_result_t *result = results->future_arr[2];
        infof("process exited with code %d, stdout:\n%s\n", result->status, result->stdout);
        async_spawn_free_result(result);
    }
    future_all_free_result(results);

    future_destroy(all);
    future_destroy(future1);
    future_destroy(future2);
    future_destroy(future3);

    infof("Coroutine says %d!\n", x + 10);
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
