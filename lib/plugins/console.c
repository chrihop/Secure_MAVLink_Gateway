#include "secure_gateway.h"

#ifdef _STD_LIBC_
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

char
read_char(void)
{
    static int initialized = 0;
    if (!initialized)
    {
        struct termios t;
        tcgetattr(STDIN_FILENO, &t);
        t.c_lflag &= ~ICANON;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        initialized = 1;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { 0, 0 };
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (FD_ISSET(STDIN_FILENO, &fds))
    {
        return getchar();
    }

    return 0;
}
#elif defined(_CERTIKOS_)

#include <syscall.h>

char
read_char(void)
{
    static int initialized = 0;
    size_t     rv;
    if (!initialized)
    {
        sys_device_control(4, DEV_CONSOLE_NON_BLOCKING, 0, &rv);
    }

    char buf[8];
    sys_gets(DEFAULT_CONSOLE, buf, 1, &rv);
    return rv == 0 ? 0 : buf[0];
}

#else

char
read_char(void)
{
    return 0;
}

#endif

typedef void (*on_key_t)(void);

static void
to_terminate(void)
{
    secure_gateway_pipeline.terminated = true;
}

static void
to_enable_security_policy(void)
{
    secure_gateway_pipeline.policy_enabled = true;
    INFO("===============================\n");
    INFO("!!! SECURITY POLICY ENABLED !!!\n");
    INFO("===============================\n");
}

static void
to_disable_security_policy(void)
{
    secure_gateway_pipeline.policy_enabled = false;
    INFO("================================\n");
    INFO("!!! SECURITY POLICY DISABLED !!!\n");
    INFO("================================\n");
}

static void
to_enable_transformer(void)
{
    secure_gateway_pipeline.transform_enabled = true;
    INFO("===========================\n");
    INFO("!!! TRANSFORMER ENABLED !!!\n");
    INFO("===========================\n");
}

static void
to_disable_transformer(void)
{
    secure_gateway_pipeline.transform_enabled = false;
    INFO("============================\n");
    INFO("!!! TRANSFORMER DISABLED !!!\n");
    INFO("============================\n");
}

static on_key_t console_handlers[128] = {
//    ['q'] = to_terminate,
    ['e'] = to_enable_security_policy,
    ['d'] = to_disable_security_policy,
    ['t'] = to_enable_transformer,
    ['f'] = to_disable_transformer,
};

void
console_spin(void)
{
    static uint64_t last_time = 0;
    uint64_t        cur_time  = time_us();
    if (cur_time - last_time < 100000)
    {
        return;
    }
    last_time = cur_time;

    char c = read_char();
    if (c == 0)
    {
        return;
    }

    ASSERT(0 <= c);
    on_key_t handler = console_handlers[(unsigned)c];
    if (handler)
    {
        handler();
    }
}
