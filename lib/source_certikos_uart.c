#include "secure_gateway.h"
#include <gcc.h>
#include <string.h>
#include <types.h>

struct certikos_uart_socket_t
{
    bool         initialized;
    size_t       dev;
    console_id_t stream;
    size_t       current_read;
    size_t       len;
    uint8_t      data[MAVLINK_MAX_PACKET_LEN];
    uint8_t      output_data[MAVLINK_MAX_PACKET_LEN];
};

struct certikos_uart_socket_t certikos_uart_socket = {
    .initialized = FALSE,
};

static int
certikos_uart_socket_init(struct certikos_uart_socket_t* uart)
{
    if (uart->initialized)
    {
        return SUCC;
    }

    uart->current_read = 0;
    uart->len          = 0;

    sys_device_control(uart->dev, DEV_OPEN_CONSOLE, &uart->stream);

    uart->initialized = TRUE;
    return SUCC;
}

static void
certikos_uart_socket_deinit(struct certikos_uart_socket_t* uart)
{
}

static int
certikos_uart_route_to(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL && "sink is NULL");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL");
    struct certikos_uart_socket_t* uart
        = (struct certikos_uart_socket_t*)sink->opaque;

    if (uart->initialized == FALSE)
    {
        certikos_uart_socket_init(uart);
    }

    size_t len = mavlink_msg_to_send_buffer(uart->output_data, &msg->msg);
    ASSERT(len <= MAVLINK_MAX_PACKET_LEN && "mavlink message too long");
    writes(uart->stream, uart->output_data, len);

    return SUCC;
}

static int
certikos_uart_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct certikos_uart_socket_t* uart
        = (struct certikos_uart_socket_t*)source->opaque;

    if (uart->initialized == FALSE)
    {
        certikos_uart_socket_init(uart);
    }

    if (uart->len > 0 && uart->current_read < uart->len)
    {
        return 1;
    }

    uart->len = reads(uart->stream, uart->data, MAVLINK_MAX_PACKET_LEN);
    uart->current_read = 0;

    if (uart->len > 0 && uart->current_read < uart->len)
    {
        return 1;
    }

    return 0;
}

static int
certikos_uart_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct certikos_uart_socket_t* uart
        = (struct certikos_uart_socket_t*)source->opaque;

    if (uart->initialized == FALSE)
    {
        certikos_uart_socket_init(uart);
    }

    if (uart->len > 0 && uart->current_read < uart->len)
    {
        return uart->data[uart->current_read++];
    }

    return 0;
}

int
hook_certikos_uart(struct pipeline_t* pipeline, size_t dev, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");
    struct certikos_uart_socket_t* uart = &certikos_uart_socket;
    uart->dev                           = dev;

    struct source_t* source = source_allocate(&pipeline->sources, source_id);
    source->opaque          = uart;
    source->init            = (init_t)certikos_uart_socket_init;
    source->cleanup         = (cleanup_t)certikos_uart_socket_deinit;
    source->has_more        = certikos_uart_has_more;
    source->read_byte       = certikos_uart_read_byte;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, SINK_TYPE_LEGACY);
    sink->opaque        = uart;
    sink->init          = (init_t)certikos_uart_socket_init;
    sink->cleanup       = (cleanup_t)certikos_uart_socket_deinit;
    sink->route         = certikos_uart_route_to;

    return SUCC;
}
