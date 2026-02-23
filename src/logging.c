#include "logging.h"
#include <stdarg.h>

static const char *level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
static const char *level_colors[] = {"\033[36m", "", "\033[33m", "\033[31m"};
static const char *reset = "\033[0m";

void _logf(int level, FILE *stream, const char *filename, int line, const char *func, const char *fmt, ...) {
    if (level < LOG_LEVEL) return;

    const char *name  = level < 4 ? level_names[level]  : "UNKNOWN";
    const char *color = level < 4 ? level_colors[level] : "";

    fprintf(stream, "%s[%s] %s:%d:%s(): ", color, name, filename, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);

    fprintf(stream, "%s", reset);
}
