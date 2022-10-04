#ifndef _STD_LIBC_
#error "This file requires a socket implementation!"
#endif

#include "secure_gateway.h"
#include <netinet/in.h>
#include <sys/socket.h>

struct tcp_socket_t
{
    bool               initialized;
    int                port;
    int                fd;
    int                connection;
    struct sockaddr_in client;
    uint8_t            buffer[4096];
    ssize_t            cur_read, buffer_size;
};

static int
tcp_init(struct tcp_socket_t* tcp)
{
    tcp->cur_read    = 0;
    tcp->buffer_size = 0;

    tcp->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp->fd == -1)
    {
        perror("Failed to create socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(tcp->port),
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY),
        },
    };

    if (bind(tcp->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        perror("Failed to bind socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (listen(tcp->fd, 1) == -1)
    {
        perror("Failed to listen on socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    socklen_t len   = sizeof(tcp->client);
    tcp->connection = accept(tcp->fd, (struct sockaddr*)&tcp->client, &len);
    if (tcp->connection == -1)
    {
        perror("Failed to accept connection! error %d");
        return tcp->connection;
    }

    tcp->initialized = true;
    return SUCC;
}

static int
tcp_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)source->opaque;

    if (!tcp->initialized)
    {
        int rv = tcp_init(tcp);
        if (rv != SUCC)
        {
            return 0;
        }
    }

    if (tcp->buffer_size > 0 && tcp->cur_read < tcp->buffer_size)
    {
        return 1;
    }

    tcp->cur_read = 0;
    tcp->buffer_size
        = recv(tcp->connection, tcp->buffer, sizeof(tcp->buffer), 0);
    if (tcp->buffer_size < 0)
    {
        perror("Failed to read from socket!");
        return 0;
    }
    return 1;
}

static int
tcp_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)source->opaque;
    ASSERT(tcp->initialized && "tcp socket is not initialized");

    if (tcp->cur_read >= tcp->buffer_size)
    {
        tcp->cur_read    = 0;
        tcp->buffer_size = 0;
        return 0;
    }
    int byte = tcp->buffer[tcp->cur_read];
    tcp->cur_read++;
    return byte;
}

static int tcp_route_to(struct sink_t * sink, struct message_t * msg)
{
    ASSERT(sink != NULL && "sink is NULL");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)sink->opaque;

    if (!tcp->initialized)
    {
        int rv = tcp_init(tcp);
        if (rv != 0)
        {
            return SEC_GATEWAY_IO_FAULT;
        }
    }

    ssize_t rv = send(tcp->connection, &msg->msg, msg->msg.len, 0);
    if (rv < 0)
    {
        perror("Failed to send message!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (rv < msg->msg.len)
    {
        WARN("Failed to send entire message! sent (%ld / %d)", rv, msg->msg.len);
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

int hook_tcp(struct pipeline_t * pipeline, int port, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");

    struct tcp_socket_t* tcp = malloc(sizeof(struct tcp_socket_t));
    if (tcp == NULL)
    {
        perror("Failed to allocate tcp socket!");
        return SEC_GATEWAY_NO_MEMORY;
    }

    tcp->port = port;
    tcp->initialized = false;

    struct source_t * source = source_allocate(&pipeline->sources, source_id);
    if (source == NULL)
    {
        WARN("Failed to allocate source!\n");
        free(tcp);
        return SEC_GATEWAY_NO_RESOURCE;
    }
    source->opaque = tcp;
    source->has_more = tcp_has_more;
    source->read_byte = tcp_read_byte;

    struct sink_t * sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate sink!\n");
        free(tcp);
        return SEC_GATEWAY_NO_RESOURCE;
    }
    sink->opaque = tcp;
    sink->route = tcp_route_to;


    return SUCC;
}
