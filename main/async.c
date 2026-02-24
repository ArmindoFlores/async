#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <threads.h>
#include "async.h"
#include "logging.h"

struct run_process_arg {
    const char *command;
    char *output;
    int status;
};

void run_process(future_t *f, void *_arg) {
    struct run_process_arg *arg = (struct run_process_arg *) _arg;
    const char *cmd = arg->command;

    debugf("running threaded, cmd=\"%s\"\n", cmd);
    thrd_sleep(&(struct timespec){.tv_nsec=100000000}, NULL);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        perror("popen failed");
        arg->output = NULL;
        arg->status = -1;
        future_reject(f);
        return;
    }

    char *buffer = NULL;
    size_t capacity = 0;
    size_t used = 0;

    const size_t chunk = 4096;
    while (1) {
        if (used + chunk + 1 > capacity) {
            size_t new_capacity = capacity == 0 ? (chunk + 1) : (2 * capacity);
            char *new_buf = realloc(buffer, new_capacity);
            if (!new_buf) {
                perror("realloc failed");
                free(buffer);
                pclose(pipe);
                arg->output = NULL;
                arg->status = -1;
                future_reject(f);
                return;
            }
            buffer = new_buf;
            capacity = new_capacity;
        }

        size_t n = fread(buffer + used, 1, chunk, pipe);
        used += n;

        if (n < chunk) {
            if (ferror(pipe)) {
                perror("fread failed");
                free(buffer);
                pclose(pipe);
                arg->output = NULL;
                arg->status = -1;
                future_reject(f);
                return;
            }
            // n < chunk and !ferror -> EOF
            break;
        }
    }

    // NUL-terminate
    if (buffer == NULL) {
        // no output at all: still return a valid empty string
        buffer = malloc(1);
        if (!buffer) {
            perror("malloc failed");
            pclose(pipe);
            arg->output = NULL;
            arg->status = -1;
            future_reject(f);
            return;
        }
        buffer[0] = '\0';
    } else {
        buffer[used] = '\0';
    }

    arg->status = pclose(pipe);

    if (arg->status != 0) {
        free(buffer);
        future_reject(f);
        return;
    }

    arg->output = buffer;
    future_resolve(f, buffer);
    return;
}

void *y(void *arg) {
    char *str = (char*)arg;
    infof("Hello, %s!\n", str);
    return (void*) 99;
}

void *f(void *arg) {
    int x = *(int*)arg;
    infof("Coroutine says %d!\n", x);

    struct run_process_arg *args = malloc(sizeof(struct run_process_arg));
    if (args == NULL) {
        infof("failed to allocate args for run_process\n");
        return NULL;
    }
    *args = (struct run_process_arg){
        .command = "lss",
        .output = NULL,
        .status = 0
    };

    future_t *future1 = future_create_from_function(y, "Francisco", FUT_OPT_EAGER);
    future_t *future2 = future_create_from_function(y, "Armindo", FUT_OPT_EAGER);
    future_t *future3 = async_dispatch(run_process, args);
    future_t *all = future_all((future_t*[]){future1, future2, future3}, 3);

    future_all_result_t *results = async_await_future(all);
    if (results != NULL) {
        for (size_t i = 0; i < results->n; i++) {
            infof("async_await_future[%lu] = %p\n", i, results->future_arr[i]);
        }
        char *output = results->future_arr[2];
        infof("run_process result: %s\n", output);
        free(results->future_arr);
        free(results);
    }

    future_destroy(all);
    future_destroy(future1);
    future_destroy(future2);
    future_destroy(future3);
    free(args->output);
    free(args);

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
