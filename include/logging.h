#ifndef _H_LOGGING_
#define _H_LOGGING_

#include <stdio.h>

#ifndef LOG_LEVEL
    #if defined(DEBUGGING)
        #define LOG_LEVEL 0
    #else
        #define LOG_LEVEL 2
    #endif
#endif

#define debugf(fmt, ...)\
    do {\
        _logf(\
            0, stderr, __FILE__, __LINE__,\
            __func__, fmt, ##__VA_ARGS__\
        );\
    } while (0)

#define infof(fmt, ...)\
    do {\
        _logf(\
            1, stderr, __FILE__, __LINE__,\
            __func__, fmt, ##__VA_ARGS__\
        );\
    } while (0)

#define warnf(fmt, ...)\
    do {\
        _logf(\
            2, stderr, __FILE__, __LINE__,\
            __func__, fmt, ##__VA_ARGS__\
        );\
    } while (0)

#define errorf(fmt, ...)\
    do {\
        _logf(\
            3, stderr, __FILE__, __LINE__,\
            __func__, fmt, ##__VA_ARGS__\
        );\
    } while (0)


void _logf(int level, FILE *stream, const char* filename, int line, const char* func, const char* fmt, ...);
#endif
