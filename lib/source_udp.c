#ifndef _STD_LIBC_
#error "This file requires a socket implementation!"
#endif

#include "secure_gateway.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <threads.h>
#include <stdatomic.h>

#ifdef __STDC_NO_THREADS__
#error "This file requires C11 thread!"
#endif

#ifdef __STDC_NO_ATOMIC__
#error "This file requires C11 atomic!"
#endif

struct udp_socket_t
{
    bool initialized;
    int  port;
    int  fd;
    _Atomic(bool) terminate;
    thrd_t        thread;
    mtx_t         lock;
    cnd_t         buffer_empty;
    uint8_t buffer[4096];
    ssize_t cur_read, buffer_size;
    uint8_t output_buffer[4096];
};

static int udp_server(void * arg)
{
    struct udp_socket_t* udp = (struct udp_socket_t*)arg;
    ssize_t              bytes_read;

    while (!atomic_load(&udp->terminate))
    {
        bytes_read = recv(udp->fd, udp->buffer, sizeof(udp->buffer), 0);
        if (bytes_read == -1)
        {
            WARN("Failed to read from UDP socket!\n");
            return SEC_GATEWAY_IO_FAULT;
        }
        else if (bytes_read == 0)
        {
            WARN("UDP socket closed!\n");
            continue ;
        }

        mtx_lock(&udp->lock);
        udp->cur_read = 0;
        udp->buffer_size = bytes_read;

        while (udp->cur_read < udp->buffer_size)
        {
            cnd_wait(&udp->buffer_empty, &udp->lock);
        }
        mtx_unlock(&udp->lock);
    }

    return SUCC;
}

static int
udp_init(struct udp_socket_t* udp)
{
    if (udp->initialized)
    {
        return SUCC;
    }

    mtx_init(&udp->lock, mtx_plain);
    cnd_init(&udp->buffer_empty);
    udp->thread = (thrd_t) -1;
    atomic_init(&udp->terminate, false);

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

    if (thrd_create(&udp->thread, udp_server, udp) != thrd_success)
    {
        perror("Failed to create thread!");
        return SEC_GATEWAY_THREAD_ERROR;
    }

    udp->initialized = true;
    return SUCC;
}

static void
udp_cleanup(struct udp_socket_t* udp)
{
    if (udp == NULL || !udp->initialized)
    {
        return ;
    }
    udp->initialized = false;

    atomic_store(&udp->terminate, true);
    mtx_destroy(&udp->lock);
    cnd_destroy(&udp->buffer_empty);
    if (udp->thread != (thrd_t) -1)
    {
        thrd_join(udp->thread, NULL);
    }
    if (udp->fd != -1)
    {
        close(udp->fd);
    }
    free(udp);
    udp = NULL;
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
    }

    if (udp->buffer_size > 0 && udp->cur_read < udp->buffer_size)
    {
        return 1;
    }

    mtx_lock(&udp->lock);
    cnd_signal(&udp->buffer_empty);
    mtx_unlock(&udp->lock);

    return 0;
}

static int udp_read_byte(struct source_t * source)
{
    ASSERT(source != NULL && "source is NULL!");
    ASSERT(source->opaque != NULL && "source->opaque is NULL!");
    struct udp_socket_t* udp = (struct udp_socket_t*)source->opaque;

    mtx_lock(&udp->lock);
    if (udp->cur_read >= udp->buffer_size)
    {
        cnd_signal(&udp->buffer_empty);
        mtx_unlock(&udp->lock);
        return 0;
    }

    int byte = udp->buffer[udp->cur_read];
    udp->cur_read++;
    mtx_unlock(&udp->lock);

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
    }

    size_t len = mavlink_msg_to_send_buffer(udp->output_buffer, &msg->msg);
    ssize_t rv = sendto(udp->fd, udp->output_buffer, len, MSG_DONTWAIT,
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
    source->init = (init_t) udp_init;
    source->cleanup = (cleanup_t) udp_cleanup;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, sink_type);
    if (sink == NULL)
    {
        WARN("Failed to allocate sink!\n");
        free(udp);
        return SEC_GATEWAY_IO_FAULT;
    }
    sink->opaque = udp;
    sink->route = udp_route_to;
    sink->init = (init_t) udp_init;
    sink->cleanup = (cleanup_t) udp_cleanup;

    return SUCC;
}
