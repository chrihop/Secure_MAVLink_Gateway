#include <ringleader.h>
#include <ringleader/writer.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "context.h"


struct ringleader*
get_rl()
{
    static struct ringleader* _rl = NULL;
    static struct ringleader* rl = NULL;

    if(rl) return rl;

    if (_rl == NULL)
    {
        _rl = ringleader_factory(16);
    }

    if(ringleader_init_finished(_rl) == ERR_OK)
    {
        rl = _rl;
    }

    return rl;
}

static void
ringleader_log_vprintf(int fd, ssize_t len, const char *format, va_list *args)
{
    struct ringleader *rl = get_rl();

    static struct ringleader_writer *rw = NULL;
    if(rw == NULL)
    {
        rw = ringleader_writer_factory(rl, fd, 4096, 65536, 4096);
    }

    /* flush any pending cqes, TODO not a nice interface */
    struct io_uring_cqe cqe;
    (void)ringleader_peek_cqe(rl, &cqe);

    char buffer[len + 1];
    vsnprintf(buffer, len + 1, format, args);
    buffer[len] = '\0';

    ringleader_promise_t p = ringleader_writer_write(
            rl, rw, buffer, sizeof(buffer));

    if(p == RINGLEADER_PROMISE_INVALID)
    {
        return;
    }

    //TODO don't infinitely block here on the untrusted OS
    ringleader_promise_await(rl, p, NULL);
}


void
pipeline_log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

#ifdef _CERTIKOS_
    /* echo to CertiKOS stdout */
    ssize_t len = vfprintf(stdout, format, &args);

    va_end(args);
    va_start(args, format);

    /* echo to Linux stdout */
    ringleader_log_vprintf(STDOUT_FILENO, len, format, &args);
#else
    vfprintf(stdout, format, args);
#endif /* _CERTIKOS_ */

    va_end(args);
}
