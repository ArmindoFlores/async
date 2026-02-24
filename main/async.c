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

    future_t *all = future_all(
        (future_t*[]){
            future_create_from_function(y, "Francisco", FUT_OPT_EAGER),
            future_create_from_function(y, "Armindo", FUT_OPT_EAGER),
            async_spawn("curl www.example.com")
        },
        3,
        1
    );

    future_all_result_t *results = async_await_future(all);
    if (results != NULL) {
        for (size_t i = 0; i < results->n; i++) {
            infof("async_await_future[%lu] = %p\n", i, results->future_arr[i].value);
        }
        async_spawn_result_t *result = results->future_arr[2].value;
        infof("process exited with code %d, stdout:\n%s\n", result->status, result->stdout);
    }

    future_destroy(all);

    infof("Coroutine says %d!\n", x + 10);
    return (void*) 1337;
}

int main() {
    async_context_t *ctx = async_context_create();
    if (ctx == NULL) {
        errorf("failed to create async context\n");
        return 1;
    }

    if (async_context_run(ctx, f, &(int){42}) != 0) {
        errorf("error in async context\n");
        return 1;
    }

    async_context_destroy(ctx);
}
