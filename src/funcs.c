#define _POSIX_C_SOURCE 200809L
#include "funcs.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct async_spawn_args {
    char *command;
};

void async_spawn_free_args(struct async_spawn_args *args) {
    if (args == NULL) return;
    free(args->command);
    free(args);
}

void async_spawn_free_result(async_spawn_result_t *result) {
    if (result == NULL) return;
    free(result->stdout);
    free(result);
}

void _spawn(future_t *f, void *_arg) {
    struct async_spawn_args *arg = (struct async_spawn_args *) _arg;
    async_spawn_result_t *result = malloc(sizeof(async_spawn_result_t));
    if (result == NULL) {
        errorf("failed to allocate memory for async_spawn() result\n");
        async_spawn_free_args(arg);
        future_reject(f);
        return;
    }

    FILE *pipe = popen(arg->command, "r");
    async_spawn_free_args(arg);
    if (!pipe) {
        errorf("failed to start process in async_spawn()\n");
        free(result);
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
                errorf("failed to allocate memory for async_spawn() result\n");
                free(buffer);
                pclose(pipe);
                free(result);
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
                errorf("failed to read from async_spawn() pipe\n");
                free(buffer);
                pclose(pipe);
                free(result);
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
            errorf("failed to allocate memory for async_spawn() result\n");
            pclose(pipe);
            free(result);
            future_reject(f);
            return;
        }
        buffer[0] = '\0';
    } else {
        buffer[used] = '\0';
    }

    result->status = pclose(pipe);

    if (result->status != 0) {
        free(buffer);
        free(result);
        future_reject(f);
        return;
    }

    result->stdout = buffer;
    future_resolve(f, result);
    return;
}

future_t *async_spawn(const char *command) {
    struct async_spawn_args *spawn_args = malloc(sizeof(struct async_spawn_args)); 
    if (spawn_args == NULL) {
        errorf("failed to allocate memory for async_spawn()\n");
        return NULL;
    }
    char *command_copy = strdup(command);
    *spawn_args = (struct async_spawn_args){
        .command = command_copy
    };
    return async_dispatch(_spawn, spawn_args);
}
