#ifndef _SECURE_GATEWAY_CONTEXT_H_
#define _SECURE_GATEWAY_CONTEXT_H_


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <certikos/debug.h>
#include <certikos/macros.h>

#ifdef _STD_LIBC_
static inline unsigned long long time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif /* _STD_LIBC_ */

#ifdef _CERTIKOS_
struct ringleader* get_rl();
#endif /* _CERTIKOS_ */


void pipeline_log_printf(const char *format, ...);


#endif /* !_SECURE_GATEWAY_CONTEXT_H_ */
