#ifndef _STD_LIBC_
#error "This file requires a socket implementation!"
#endif

#include "secure_gateway.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

struct tcpout_socket_t
{
    bool        initialized;
    char        ip[16];
    int         port;
    int         fd;
    atomic_bool terminate;
    thrd_t      thread;
    mtx_t       lock;
    cnd_t       buffer_empty;
    uint8_t     buffer[4096];
    ssize_t     cur_read, buffer_size;
    uint8_t     output_buffer[4096];
};

static void
tcp_state_init(struct tcpout_socket_t* tcp)
{
    mtx_init(&tcp->lock, mtx_plain);
    cnd_init(&tcp->buffer_empty);
    tcp->thread = (thrd_t)-1;
    atomic_init(&tcp->terminate, false);

    tcp->cur_read    = 0;
    tcp->buffer_size = 0;
    tcp->fd          = -1;
}

static void
tcp_cleanup(struct tcpout_socket_t* tcp)
{
    if (tcp == NULL)
        return;

    tcp->initialized = false;
    atomic_store(&tcp->terminate, true);
    mtx_destroy(&tcp->lock);
    cnd_destroy(&tcp->buffer_empty);
    if (tcp->thread != (thrd_t)-1)
        thrd_join(tcp->thread, NULL);

    if (tcp->fd != -1)
        close(tcp->fd);

    free(tcp);
}

static int
tcp_connect_to(struct tcpout_socket_t* tcp)
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
    };

    if (inet_pton(AF_INET, tcp->ip, &addr.sin_addr) <= 0)
    {
        perror("Invalid address!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (connect(tcp->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        perror("Failed to connect to server!");
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

static int
tcp_server(void* arg)
{
    struct tcpout_socket_t* tcp = (struct tcpout_socket_t*)arg;

    if (tcp == NULL)
    {
        return SEC_GATEWAY_INVALID_PARAM;
    }

    while (!atomic_load(&tcp->terminate))
    {
        if (tcp->fd == -1)
        {
            if (tcp_connect_to(tcp) != SUCC)
            {
                sleep(1);
                printf("retrying connection to %s:%d ...\n", tcp->ip, tcp->port);
                continue;
            }
        }

        ssize_t read = recv(tcp->fd, tcp->buffer, sizeof(tcp->buffer), 0);
        if (read == -1)
        {
            perror("Failed to read from socket!");
            close(tcp->fd);
            tcp->fd = -1;
            continue;
        }
        else if (read == 0)
        {
            /* client close the socket */
            perror("Server disconnected!");
            close(tcp->fd);
            tcp->fd = -1;
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
tcp_init(struct tcpout_socket_t* tcp)
{
    ASSERT(tcp != NULL && "tcp is NULL!");
    if (tcp->initialized)
    {
        return SUCC;
    }

    tcp_state_init(tcp);
    int rv = SUCC;

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
    struct tcpout_socket_t* tcp = (struct tcpout_socket_t*)source->opaque;

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
    struct tcpout_socket_t* tcp = (struct tcpout_socket_t*)source->opaque;
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
    struct tcpout_socket_t* tcp = (struct tcpout_socket_t*)sink->opaque;

    if (!tcp->initialized)
    {
        int rv = tcp_init(tcp);
        if (rv != 0)
        {
            return SEC_GATEWAY_IO_FAULT;
        }
    }

    if (tcp->fd == -1)
    {
        WARN("Message %d dropped. No client to send to!\n", msg->msg.msgid);
        return SUCC;
    }

    int     len = mavlink_msg_to_send_buffer(tcp->output_buffer, &msg->msg);
    ssize_t rv  = send(tcp->fd, tcp->output_buffer, len, 0);
    if (rv < 0)
    {
        perror("Failed to send message!");
        return SEC_GATEWAY_IO_FAULT;
    }

    if (rv < len)
    {
        WARN("Failed to send entire message! sent (%zd / %d)", rv, msg->msg.len);
        return SEC_GATEWAY_IO_FAULT;
    }

    return SUCC;
}

int
hook_tcpout(struct pipeline_t* pipeline, const char * ip, int port, size_t source_id,
    enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");

    struct tcpout_socket_t* tcp = malloc(sizeof(struct tcpout_socket_t));
    if (tcp == NULL)
    {
        perror("Failed to allocate tcp socket!");
        return SEC_GATEWAY_NO_MEMORY;
    }

    strncpy(tcp->ip, ip, 16);
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

    source->has_more  = tcp_has_more;
    source->read_byte = tcp_read_byte;
    source->init      = (init_t)tcp_init;
    source->cleanup   = (cleanup_t)tcp_cleanup;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate sink!\n");
        free(tcp);
        return SEC_GATEWAY_NO_RESOURCE;
    }
    sink->opaque = tcp;

    sink->route   = tcp_route_to;
    sink->init    = (init_t)tcp_init;
    sink->cleanup = (cleanup_t)tcp_cleanup;

    return SUCC;
}
