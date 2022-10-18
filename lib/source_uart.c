#ifndef _STD_LIBC_
#error "This file requires a /dev/tty* implementation!"
#endif

#include "ring_buffer.h"
#include "secure_gateway.h"
#include <stdatomic.h>
#include <threads.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

struct uart_connection_t
{
    bool                 initialized;
    int                  fd;
    char*                device;
    atomic_bool          terminate;
    thrd_t               thread;
    mtx_t                lock;
    cnd_t                buffer_not_full;
    struct termios       options;
    struct ring_buffer_t input_buffer;
    uint8_t              output_buffer[4096];
};

static uint8_t uart_input[256];

static int
uart_server(void* arg)
{
    struct uart_connection_t* uart = (struct uart_connection_t*)arg;
    ssize_t                   bytes_read;

    while (!atomic_load(&uart->terminate))
    {
        bytes_read = read(uart->fd, uart_input, sizeof(uart_input));
        if (bytes_read == -1)
        {
            WARN("Failed to read from UART device!\n");
            return SEC_GATEWAY_IO_FAULT;
        }

        mtx_lock(&uart->lock);
        ring_buffer_copy_from(&uart->input_buffer, uart_input, bytes_read);
        while (ring_buffer_is_full(&uart->input_buffer))
        {
            cnd_wait(&uart->buffer_not_full, &uart->lock);
        }
        mtx_unlock(&uart->lock);
    }

    return SUCC;
}

static int
uart_device_init(struct uart_connection_t* uart)
{
    if ((uart->fd = open(uart->device, O_RDWR | O_NOCTTY | O_NDELAY)) == -1)
    {
        WARN("Failed to open UART device! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }

    fcntl(uart->fd, F_SETFL, fcntl(uart->fd, F_GETFL) & ~O_NONBLOCK);

    if (tcgetattr(uart->fd, &uart->options) != 0)
    {
        WARN("Failed to get UART device options! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }

    uart->options.c_cflag &= ~CSIZE;   /* Mask the character size bits */
    uart->options.c_cflag |= CS8;      /* Select 8 data bits */
    uart->options.c_cflag &= ~PARENB;  /* Disable parity */
    uart->options.c_cflag &= ~CSTOPB;  /* Use one stop bit */
    uart->options.c_cflag &= ~CRTSCTS; /* Disable hardware flow control */
    uart->options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); /* raw input */
    uart->options.c_oflag &= ~OPOST;                          /* raw output */
    uart->options.c_iflag
        &= ~(IXON | IXOFF | IXANY); /* Disable software flow control */
    uart->options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR
        | ICRNL); /* Disable special handling of received bytes */
    /* read will block until at least one byte is available */
    uart->options.c_cc[VMIN]  = 1;
    uart->options.c_cc[VTIME] = 0;
    cfsetispeed(&uart->options, B115200);
    cfsetospeed(&uart->options, B115200);

    if (tcsetattr(uart->fd, TCSANOW, &uart->options) != 0)
    {
        WARN("Failed to set UART device options! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }
    return SUCC;
}

static int
uart_state_init(struct uart_connection_t* uart)
{
    mtx_init(&uart->lock, mtx_plain);
    cnd_init(&uart->buffer_not_full);
    uart->thread = (thrd_t)-1;
    atomic_init(&uart->terminate, false);
    ring_buffer_init(
        &uart->input_buffer, uart->output_buffer, sizeof(uart->output_buffer));
    return SUCC;
}

static int
uart_init(struct uart_connection_t* uart)
{
    ASSERT(uart != NULL && "uart is NULL!");
    if (uart->initialized)
    {
        return SUCC;
    }

    if (uart_device_init(uart) != SUCC)
    {
        return SEC_GATEWAY_IO_FAULT;
    }

    if (uart_state_init(uart) != SUCC)
    {
        return SEC_GATEWAY_INVALID_STATE;
    }

    int rv;
    if ((rv = thrd_create(&uart->thread, uart_server, uart)) != thrd_success)
    {
        WARN("Failed to create UART server thread! %s\n", strerror(errno));
        return SEC_GATEWAY_THREAD_ERROR;
    }

    uart->initialized = true;
    return rv;
}

static void
uart_cleanup(struct uart_connection_t* uart)
{
    if (uart == NULL)
    {
        return;
    }
    uart->initialized = false;
    atomic_store(&uart->terminate, true);
    mtx_destroy(&uart->lock);
    cnd_destroy(&uart->buffer_not_full);
    if (uart->thread != (thrd_t)-1)
    {
        thrd_join(uart->thread, NULL);
    }
    if (uart->fd != -1)
    {
        close(uart->fd);
    }
    free(uart);
}

static int
uart_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL!");
    ASSERT(source->opaque != NULL && "source->opaque is NULL!");
    struct uart_connection_t* uart = (struct uart_connection_t*)source->opaque;
    if (!uart->initialized)
    {
        if (uart_init(uart) != SUCC)
        {
            return 0;
        }
    }

    mtx_lock(&uart->lock);
    int has_more = !ring_buffer_is_empty(&uart->input_buffer);
    mtx_unlock(&uart->lock);

    return has_more;
}

static int
uart_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL!");
    ASSERT(source->opaque != NULL && "source->opaque is NULL!");
    struct uart_connection_t* uart = (struct uart_connection_t*)source->opaque;

    mtx_lock(&uart->lock);
    uint8_t byte = ring_buffer_pop(&uart->input_buffer);
    cnd_signal(&uart->buffer_not_full);
    mtx_unlock(&uart->lock);

    return byte;
}

static int
uart_route_to(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL && "sink is NULL!");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL!");

    struct uart_connection_t* uart = (struct uart_connection_t*)sink->opaque;
    if (!uart->initialized)
    {
        if (uart_init(uart) != SUCC)
        {
            return SEC_GATEWAY_IO_FAULT;
        }
    }

    int len = mavlink_msg_to_send_buffer(uart->output_buffer, &msg->msg);
    int bytes_written = write(uart->fd, uart->output_buffer, len);
    if (bytes_written == -1)
    {
        WARN("Failed to write to UART device! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }

    if (bytes_written < len)
    {
        WARN("Failed to write all bytes to UART device! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

int
hook_uart(struct pipeline_t* pipeline, char* device, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL!");
    ASSERT(device != NULL && "device is NULL!");

    struct uart_connection_t* uart = malloc(sizeof(struct uart_connection_t));
    if (uart == NULL)
    {
        WARN("Failed to allocate memory for UART connection! %s\n",
            strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }

    uart->device            = device;
    uart->initialized       = false;
    struct source_t* source = source_allocate(&pipeline->sources, source_id);
    if (source == NULL)
    {
        WARN(
            "Failed to allocate memory for UART source! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }
    source->opaque = uart;

    source->init      = (init_t)uart_init;
    source->has_more  = uart_has_more;
    source->read_byte = uart_read_byte;
    source->cleanup   = (cleanup_t)uart_cleanup;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate memory for UART sink! %s\n", strerror(errno));
        return SEC_GATEWAY_IO_FAULT;
    }
    sink->opaque = uart;

    sink->route   = uart_route_to;
    sink->init    = (init_t)uart_init;
    sink->cleanup = (cleanup_t)uart_cleanup;

    return SUCC;
}
