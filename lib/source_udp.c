#ifndef _STD_LIBC_
#error "This file requires a socket implementation!"
#endif

#include "secure_gateway.h"
#include <sys/socket.h>
#include <netinet/in.h>

struct udp_socket_t
{
    bool               initialized;
    int                port;
    int                fd;
    uint8_t            buffer[4096];
    ssize_t            cur_read, buffer_size;
};

static int
udp_init(struct udp_socket_t* udp)
{
    udp->cur_read    = 0;
    udp->buffer_size = 0;

    udp->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp->fd == -1)
    {
        perror("Failed to create socket!\n");
        return SEC_GATEWAY_IO_FAULT;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(udp->port),
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY),
        },
    };

    if (bind(udp->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        perror("Failed to bind socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

static int udp_has_more(struct source_t * source)
{
    ASSERT(source != NULL && "source is NULL!");
    ASSERT(source->opaque != NULL && "source->opaque is NULL!");
    struct udp_socket_t* udp = (struct udp_socket_t*)source->opaque;

    if (!udp->initialized)
    {
        int rv = udp_init(udp);
        if (rv != SUCC)
        {
            return 0;
        }
        udp->initialized = true;
    }

    if (udp->buffer_size > 0 && udp->cur_read < udp->buffer_size)
    {
        return 1;
    }

    udp->cur_read = 0;
    udp->buffer_size = recvfrom(udp->fd, udp->buffer, sizeof(udp->buffer),
        MSG_WAITALL, NULL, NULL);
    if (udp->buffer_size == -1)
    {
        perror("Failed to read from socket!");
        return 0;
    }
    return 1;
}

static int udp_read_byte(struct source_t * source)
{
    ASSERT(source != NULL && "source is NULL!");
    ASSERT(source->opaque != NULL && "source->opaque is NULL!");
    struct udp_socket_t* udp = (struct udp_socket_t*)source->opaque;

    if (udp->cur_read >= udp->buffer_size)
    {
        udp->cur_read = 0;
        udp->buffer_size = 0;
        return 0;
    }

    int byte = udp->buffer[udp->cur_read];
    udp->cur_read++;

    return byte;
}

static int udp_route_to(struct sink_t * sink, struct message_t * msg)
{
    ASSERT(sink != NULL && "sink is NULL!");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL!");
    ASSERT(msg != NULL && "msg is NULL!");
    struct udp_socket_t* udp = (struct udp_socket_t*)sink->opaque;

    if (!udp->initialized)
    {
        int rv = udp_init(udp);
        if (rv != SUCC)
        {
            return rv;
        }
        udp->initialized = true;
    }

    ssize_t rv = sendto(udp->fd, &msg->msg, msg->msg.len, MSG_WAITALL,
        NULL, 0);
    if (rv < msg->msg.len)
    {
        perror("Failed to send message!");
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

int hook_udp(struct pipeline_t * pipeline, int port, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL!");

    struct udp_socket_t* udp = malloc(sizeof(struct udp_socket_t));
    if (udp == NULL)
    {
        perror("Failed to allocate memory for udp socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    udp->port = port;
    udp->initialized = false;

    struct source_t* source = source_allocate(&pipeline->sources, source_id);
    if (source == NULL)
    {
        WARN("Failed to allocate source!\n");
        free(udp);
        return SEC_GATEWAY_IO_FAULT;
    }
    source->opaque = udp;
    source->has_more = udp_has_more;
    source->read_byte = udp_read_byte;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate sink!\n");
        free(udp);
        return SEC_GATEWAY_IO_FAULT;
    }
    sink->opaque = udp;
    sink->route = udp_route_to;

    return SUCC;
}