#ifndef _STD_LIBC_
#error "This file requires a socket implementation!"
#endif

#include "secure_gateway.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

struct tcp_socket_t
{
    bool               initialized;
    int                port;
    int                fd;
    int                connection;
    struct sockaddr_in client;
#ifndef __STDC_NO_THREADS__
    _Atomic(bool) terminate;
    thrd_t        thread;
    mtx_t         lock;
    cnd_t         buffer_empty;
#endif
    uint8_t buffer[4096];
    ssize_t cur_read, buffer_size;
    uint8_t output_buffer[4096];
};

static void
tcp_state_init(struct tcp_socket_t* tcp)
{
#ifndef __STDC_NO_THREADS__
    mtx_init(&tcp->lock, mtx_plain);
    cnd_init(&tcp->buffer_empty);
    tcp->thread = (thrd_t)-1;
    atomic_init(&tcp->terminate, false);
#endif

    tcp->cur_read    = 0;
    tcp->buffer_size = 0;
    tcp->connection  = -1;
}

static void
tcp_cleanup(struct tcp_socket_t* tcp)
{
    if (tcp == NULL)
        return;

    tcp->initialized = false;
#ifndef __STDC_NO_THREADS__
    atomic_store(&tcp->terminate, true);
    mtx_destroy(&tcp->lock);
    cnd_destroy(&tcp->buffer_empty);
    if (tcp->thread != (thrd_t)-1)
        thrd_join(tcp->thread, NULL);
#endif

    if (tcp->fd != -1)
        close(tcp->fd);

    if (tcp->connection != -1)
        close(tcp->connection);

    free(tcp);
}

static int
tcp_listen_to(struct tcp_socket_t* tcp)
{
    tcp->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp->fd == -1)
    {
        perror("Failed to create socket!");
        return SEC_GATEWAY_IO_FAULT;
    }

    int opt = 1;
    if (setsockopt(tcp->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Failed to reset socket!");
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

    return SUCC;
}

static int
tcp_accept_client(struct tcp_socket_t* tcp)
{
    socklen_t len   = sizeof(tcp->client);
    tcp->connection = accept(tcp->fd, (struct sockaddr*)&tcp->client, &len);
    if (tcp->connection == -1)
    {
        perror("Failed to accept connection! error %d");
        return SEC_GATEWAY_IO_FAULT;
    }
    INFO("TCP client connected %s: %d\n",
        inet_ntoa(tcp->client.sin_addr), ntohs(tcp->client.sin_port));

    return SUCC;
}

static int
tcp_init(struct tcp_socket_t* tcp)
{
    int rv;
    if (tcp->initialized)
    {
        return SUCC;
    }

    tcp_state_init(tcp);

    if ((rv = tcp_listen_to(tcp)) != SUCC)
    {
        return rv;
    }

    if ((rv = tcp_accept_client(tcp)) != SUCC)
    {
        return rv;
    }

    tcp->initialized = true;
    return SUCC;
}

static int
tcp_server(void* arg)
{
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)arg;

    if (tcp == NULL)
    {
        return SEC_GATEWAY_INVALID_PARAM;
    }

    while (atomic_load(&tcp->terminate) == false)
    {
        if (tcp->connection == -1)
        {
            if (tcp_accept_client(tcp) != SUCC)
            {
                continue;
            }
        }

        ssize_t read
            = recv(tcp->connection, tcp->buffer, sizeof(tcp->buffer), 0);
        if (read == -1)
        {
            perror("Failed to read from socket!");
            close(tcp->connection);
            tcp->connection = -1;
            continue;
        }
        else if (read == 0)
        {
            /* client close the socket */
            close(tcp->connection);
            tcp->connection = -1;
            continue;
        }

        mtx_lock(&tcp->lock);

        tcp->cur_read    = 0;
        tcp->buffer_size = read;

        while (tcp->cur_read < tcp->buffer_size)
        {
            cnd_wait(&tcp->buffer_empty, &tcp->lock);
        }
        mtx_unlock(&tcp->lock);
    }
    return SUCC;
}

static int
tcp_init_mt(struct tcp_socket_t* tcp)
{
    ASSERT(tcp != NULL && "tcp is NULL!");
    if (tcp->initialized)
    {
        return SUCC;
    }

    tcp_state_init(tcp);
    int rv = SUCC;
    if ((rv = tcp_listen_to(tcp)) != SUCC)
    {
        return rv;
    }

    rv = thrd_create(&tcp->thread, tcp_server, tcp);
    if (rv != thrd_success)
    {
        perror("Failed to create worker thread!");
        return SEC_GATEWAY_THREAD_ERROR;
    }
    tcp->initialized = true;
    return rv;
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

    tcp->buffer_size
        = recv(tcp->connection, tcp->buffer, sizeof(tcp->buffer), 0);
    if (tcp->buffer_size < 0)
    {
        perror("Failed to read from socket!");
        return 0;
    }
    tcp->cur_read = 0;
    return 1;
}

static int
tcp_has_more_mt(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)source->opaque;

    if (!tcp->initialized)
    {
        int rv = tcp_init_mt(tcp);
        if (rv != SUCC)
        {
            return 0;
        }
    }

    if (tcp->buffer_size > 0 && tcp->cur_read < tcp->buffer_size)
    {
        return 1;
    }

    mtx_lock(&tcp->lock);
    cnd_signal(&tcp->buffer_empty);
    mtx_unlock(&tcp->lock);

    return 0;
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

static int
tcp_read_byte_mt(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)source->opaque;
    ASSERT(tcp->initialized && "tcp socket is not initialized");

    mtx_lock(&tcp->lock);
    if (tcp->cur_read >= tcp->buffer_size)
    {
        cnd_signal(&tcp->buffer_empty);
        mtx_unlock(&tcp->lock);
        return 0;
    }
    int byte = tcp->buffer[tcp->cur_read];
    tcp->cur_read++;
    mtx_unlock(&tcp->lock);
    return byte;
}

static int
tcp_route_to(struct sink_t* sink, struct message_t* msg)
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

    size_t  len = mavlink_msg_length(&msg->msg);
    ssize_t rv  = send(tcp->connection, &msg->msg, len, 0);
    if (rv < 0)
    {
        perror("Failed to send message!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (rv < msg->msg.len)
    {
        WARN(
            "Failed to send entire message! sent (%ld / %d)", rv, msg->msg.len);
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

static int
tcp_route_to_mt(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL && "sink is NULL");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL");
    struct tcp_socket_t* tcp = (struct tcp_socket_t*)sink->opaque;

    if (!tcp->initialized)
    {
        int rv = tcp_init_mt(tcp);
        if (rv != 0)
        {
            return SEC_GATEWAY_IO_FAULT;
        }
    }

    if (tcp->connection == -1)
    {
        WARN("Message %d dropped. No client to send to!\n", msg->msg.msgid);
        return SUCC;
    }

    int len = mavlink_msg_to_send_buffer(tcp->output_buffer, &msg->msg);
    int rv  = send(tcp->connection, tcp->output_buffer, len, 0);
    if (rv < 0)
    {
        perror("Failed to send message!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (rv < len)
    {
        WARN("Failed to send entire message! sent (%d / %d)", rv, msg->msg.len);
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

int
hook_tcp(struct pipeline_t* pipeline, int port, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");

    struct tcp_socket_t* tcp = malloc(sizeof(struct tcp_socket_t));
    if (tcp == NULL)
    {
        perror("Failed to allocate tcp socket!");
        return SEC_GATEWAY_NO_MEMORY;
    }

    tcp->port        = port;
    tcp->initialized = false;

    struct source_t* source = source_allocate(&pipeline->sources, source_id);
    if (source == NULL)
    {
        WARN("Failed to allocate source!\n");
        free(tcp);
        return SEC_GATEWAY_NO_RESOURCE;
    }
    source->opaque = tcp;

#ifdef __STDC_NO_THREADS__
    source->has_more  = tcp_has_more;
    source->read_byte = tcp_read_byte;
    source->init      = (init_t)tcp_init;
#else
    source->has_more  = tcp_has_more_mt;
    source->read_byte = tcp_read_byte_mt;
    source->init      = (init_t)tcp_init_mt;
#endif
    source->cleanup = (cleanup_t)tcp_cleanup;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate sink!\n");
        free(tcp);
        return SEC_GATEWAY_NO_RESOURCE;
    }
    sink->opaque = tcp;

#ifdef __STDC_NO_THREADS__
    sink->route = tcp_route_to;
    sink->init  = (init_t)tcp_init;
#else
    sink->route       = tcp_route_to_mt;
    sink->init        = (init_t)tcp_init_mt;
#endif
    sink->cleanup = (cleanup_t)tcp_cleanup;

    return SUCC;
}
