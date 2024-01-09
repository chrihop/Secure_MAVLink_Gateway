#include "secure_gateway.h"
#include <ringleader.h>
#include <stdio.h>

struct mavlink_rl
{

    struct
    {
        int arena : 1;
        int socket : 1;
        int bind_listen : 1;
    } flags;

    struct sockaddr_in       addr;
    struct ringleader_arena* arena;
    int                      sockfd;

    struct sockaddr_in*      arena_client_sockaddr;
    socklen_t*               arena_client_socklen;
    char*                    arena_data_pages[4];

    struct mavlink_rl*       user_data_tbl[4];

    char*                    ready;
    size_t                   ready_head;
    size_t                   ready_tail;
};

static struct ringleader*
get_rl()
{
    static struct ringleader* rl = NULL;

    if (rl == NULL)
    {
        rl = ringleader_factory(16);
    }

    return rl;
}

static err_t
ringleader_tcp_arena_cb(struct ringleader* rl, struct io_uring_cqe* cqe)
{
    struct mavlink_rl* mavrl = (struct mavlink_rl*)cqe->user_data;
    err_t              err   = ringleader_init_arena(rl, cqe, &mavrl->arena);

    for (size_t j = 0; j < SIZEOF_ARRAY(mavrl->arena_data_pages); j++)
    {
        mavrl->arena_data_pages[j] = ringleader_arena_push(mavrl->arena, 4096);
    }
    return err;
}

static err_t
ringleader_tcp_socket_cb(struct ringleader* rl, struct io_uring_cqe* cqe)
{
    struct mavlink_rl* mavrl = (struct mavlink_rl*)cqe->user_data;
    mavrl->sockfd            = cqe->res;
    return ERR_OK;
}

static err_t
ringleader_tcp_recv_cb(struct ringleader* rl, struct io_uring_cqe* cqe)
{
    struct mavlink_rl* mavrl = *(struct mavlink_rl**)cqe->user_data;
    size_t             index
        = (struct mavlink_rl**)cqe->user_data - &mavrl->user_data_tbl[0];

    printf("ringleader recv(%zu) %i!\n", index, cqe->res);
    if (cqe->res > 0)
    {
        size_t length = cqe->res;
        while (length > 0)
        {
            size_t to_copy  = MIN(length, (uintptr_t)4096 - mavrl->ready_head);
            size_t new_head = (mavrl->ready_head + to_copy) & (4096 - 1);
            printf("to_copy=%llu, head: %llu -> %llu\n", to_copy,
                mavrl->ready_head, new_head);

            memcpy(mavrl->ready + mavrl->ready_head,
                mavrl->arena_data_pages[index], to_copy);

            if ((mavrl->ready_head < mavrl->ready_tail
                    && new_head >= mavrl->ready_tail)
                || (new_head >= mavrl->ready_tail
                    && new_head < mavrl->ready_head))
            {
                printf("overwritting tail\n");
                mavrl->ready_tail = (new_head + 1) & (4096 - 1);
            }
            mavrl->ready_head = new_head;
            length -= to_copy;
        }
    }

    return ERR_OK;
}

static err_t
ringleader_tcp_accept_cb(struct ringleader* rl, struct io_uring_cqe* cqe)
{
    struct mavlink_rl* mavrl = (struct mavlink_rl*)cqe->user_data;
    printf("ringleader accept fd=%i!\n", cqe->res);

    int32_t i;

    for (size_t j = 0; j < SIZEOF_ARRAY(mavrl->arena_data_pages); j++)
    {
        i = ringleader_prep_recv(
            rl, cqe->res, mavrl->arena_data_pages[j], 4096, 0);
        ringleader_set_callback(
            rl, i, ringleader_tcp_recv_cb, &mavrl->user_data_tbl[j]);
    }

    ringleader_submit(rl);

    return ERR_OK;
}

static struct ringleader*
ringleader_tcp_tick(struct mavlink_rl* mavrl)
{
    int32_t            i;
    struct ringleader* rl = get_rl();

    if (ringleader_init_finished(rl) == ERR_OK)
    {
        /* trigger callbacks */
        struct io_uring_cqe* cqe = ringleader_peek_cqe(rl);
        if (cqe)
        {
            ringleader_consume_cqe(rl, cqe);
        }

        /* initialize arena and socket fd */
        if (mavrl->arena == NULL)
        {

            if (!mavrl->flags.arena)
            {
                mavrl->flags.arena = 1;
                ringleader_request_arena_callback(
                    rl, 32 * 4096, ringleader_tcp_arena_cb, mavrl);
            }

            if (!mavrl->flags.socket)
            {
                mavrl->flags.socket = 1;
                i = ringleader_prep_socket(rl, AF_INET, SOCK_STREAM, 0);
                ringleader_set_callback(rl, i, ringleader_tcp_socket_cb, mavrl);
                ringleader_submit(rl);
            }
        }
        else if (mavrl->sockfd != -1 && !mavrl->flags.bind_listen)
        {
            mavrl->flags.bind_listen = 1;

            void* arena_sockaddr = ringleader_arena_apush(
                mavrl->arena, &mavrl->addr, sizeof(mavrl->addr));

            i = ringleader_prep_bind(
                rl, mavrl->sockfd, arena_sockaddr, sizeof(mavrl->addr));
            ringleader_sqe_set_flags(rl, i, IOSQE_IO_LINK);

            i = ringleader_prep_listen(rl, mavrl->sockfd, 16);
            ringleader_sqe_set_flags(rl, i, IOSQE_IO_LINK);

            mavrl->arena_client_sockaddr = ringleader_arena_push(
                mavrl->arena, sizeof(struct sockaddr_in));
            mavrl->arena_client_socklen
                = ringleader_arena_push(mavrl->arena, sizeof(socklen_t));

            i = ringleader_prep_accept(rl, mavrl->sockfd,
                (struct sockaddr*)mavrl->arena_client_sockaddr,
                mavrl->arena_client_socklen, 0);
            ringleader_set_callback(rl, i, ringleader_tcp_accept_cb, mavrl);
            ringleader_submit(rl);
        }

        return rl;
    }

    return NULL;
}

static int
ringleader_tcp_init(struct mavlink_rl* mavrl)
{
    struct ringleader* rl = ringleader_tcp_tick(mavrl);
    (void)rl;

    return SUCC;
}

static void
ringleader_tcp_cleanup(struct mavlink_rl* mavrl)
{
}

static int
ringleader_tcp_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct mavlink_rl* mavrl = (struct mavlink_rl*)source->opaque;
    struct ringleader* rl    = ringleader_tcp_tick(mavrl);
    (void)rl;

    return mavrl->ready_head != mavrl->ready_tail;
}

static int
ringleader_tcp_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct mavlink_rl* mavrl = (struct mavlink_rl*)source->opaque;

    int                ret = mavrl->ready[mavrl->ready_tail];
    mavrl->ready_tail      = (mavrl->ready_tail + 1) & (4096 - 1);
    return ret;
}

int
hook_ringleader_tcp(struct pipeline_t* pipeline, int port, size_t source_id,
    enum sink_type_t sink_type)
{
    struct mavlink_rl* mavrl = calloc(sizeof(struct mavlink_rl), 1);
    if (!mavrl)
    {
        return SEC_GATEWAY_NO_MEMORY;
    }

    mavrl->ready = (char*)malloc(4096);
    if (!mavrl->ready)
    {
        free(mavrl);
        return SEC_GATEWAY_NO_MEMORY;
    }
    mavrl->ready_head           = 0;
    mavrl->ready_tail           = 0;
    mavrl->sockfd               = -1;
    mavrl->addr.sin_family      = AF_INET;
    mavrl->addr.sin_port        = htons(port);
    mavrl->addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* little trick to let us identify what recv request is coming back */
    for (size_t j = 0; j < SIZEOF_ARRAY(mavrl->user_data_tbl); j++)
    {
        mavrl->user_data_tbl[j] = mavrl;
    }

    struct source_t* source = source_allocate(&pipeline->sources, source_id);
    source->opaque          = mavrl;
    source->init            = (init_t)ringleader_tcp_init;
    source->cleanup         = (cleanup_t)ringleader_tcp_cleanup;
    source->has_more        = ringleader_tcp_has_more;
    source->read_byte       = ringleader_tcp_read_byte;

    return SUCC;
}
