#include <stdio.h>
#include "async.h"
#include "logging.h"

void *entry(void *_) {
    printf("hello world\n");
    return NULL;
}

int main() {
    async_context_t *ctx = async_context_create();
    if (ctx == NULL) {
        errorf("failed to create async context\n");
        return 1;
    }
    
    if (async_context_run(ctx, entry, NULL) != 0) {
        errorf("error in async context\n");
        return 1;
    }
    
    async_context_destroy(ctx);

    return 0;
}

/* TEST RESULT
{
    "stdout": "hello world"
}
*/
