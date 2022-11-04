#include <gcc.h>
#include <string.h>
#include <thinros.h>
#include <types.h>

#include "secure_gateway.h"

USE_ROS;

typedef struct mavlink_msg_t
{
    size_t  len;
    uint8_t data[MAVLINK_MAX_PACKET_LEN];
} mavlink_msg_t;

struct thinros_socket_t
{
    bool          initialized;
    char*         sub_topic;
    char*         pub_topic;
    char*         node_name;
    handler_t     publisher;
    size_t        current_read;
    mavlink_msg_t msg;
    mavlink_msg_t output_msg;
};

struct thinros_socket_t thinros_socket = {
    .initialized = FALSE,
    .node_name   = "mavlink_gateway",
};

static void
on_thinros_mavlink_recv(struct thinros_socket_t* ros, void* message)
{
    mavlink_msg_t* msg = (mavlink_msg_t*)message;
    ros->msg.len       = msg->len;
    memcpy(&ros->msg.data, msg->data, ros->msg.len);
    ros->current_read = 0;
}

static void
on_thinros_recv(void* message)
{
    on_thinros_mavlink_recv(&thinros_socket, message);
}

static int
thinros_socket_init(struct thinros_socket_t* ros)
{
    if (ros->initialized)
    {
        return SUCC;
    }

    ros_node(ros->node_name);
    ros->publisher    = ros_advertise(ros->pub_topic);
    if (ros->publisher == INVALID_HANDLER)
    {
        return SEC_GATEWAY_IO_FAULT;
    }

    ros->current_read = 0;
    ros->msg.len      = 0;
    ros_subscribe(ros->sub_topic, on_thinros_recv);

    ros_start();
    ros->initialized = TRUE;
    return SUCC;
}

static void
thinros_socket_deinit(struct thinros_socket_t* ros)
{
}

static int
thinros_socket_route_to(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL && "sink is NULL");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL");
    struct thinros_socket_t* ros = (struct thinros_socket_t*)sink->opaque;

    if (ros->initialized == FALSE)
    {
        thinros_socket_init(ros);
    }

    ros->output_msg.len
        = mavlink_msg_to_send_buffer(ros->output_msg.data, &msg->msg);
    ASSERT(ros->output_msg.len <= MAVLINK_MAX_PACKET_LEN
        && "mavlink message too long");

    ros_publish(ros->publisher, &ros->output_msg, sizeof(mavlink_msg_t));
    return SEC_GATEWAY_SUCCESS;
}

static int
thinros_socket_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct thinros_socket_t* ros = (struct thinros_socket_t*)source->opaque;

    if (ros->initialized == FALSE)
    {
        thinros_socket_init(ros);
    }

    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return 1;
    }

    ros_spin_one_message();
    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return 1;
    }

    return 0;
}

static int
thinros_socket_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct thinros_socket_t* ros = (struct thinros_socket_t*)source->opaque;

    if (ros->initialized == FALSE)
    {
        thinros_socket_init(ros);
    }

    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return ros->msg.data[ros->current_read++];
    }

    return 0;
}

int
hook_thinros(
    struct pipeline_t* pipeline, const char* pub_topic, const char* sub_topic)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");

    struct thinros_socket_t* ros = &thinros_socket;
    ros->pub_topic               = (char*)pub_topic;
    ros->sub_topic               = (char*)sub_topic;
    ros->initialized             = FALSE;

    struct source_t* source
        = source_allocate(&pipeline->sources, SOURCE_ID_LEGACY);
    source->opaque    = ros;
    source->init      = (init_t)thinros_socket_init;
    source->cleanup   = (cleanup_t)thinros_socket_deinit;
    source->has_more  = thinros_socket_has_more;
    source->read_byte = thinros_socket_read_byte;

    struct sink_t* sink = sink_allocate(&pipeline->sinks, SINK_TYPE_LEGACY);
    sink->opaque        = ros;
    source->init        = (init_t)thinros_socket_init;
    source->cleanup     = (cleanup_t)thinros_socket_deinit;
    sink->route         = thinros_socket_route_to;

    source          = source_allocate(&pipeline->sources, SOURCE_ID_ENCLAVE(0));
    source->opaque  = ros;
    source->init    = (init_t)thinros_socket_init;
    source->cleanup = (cleanup_t)thinros_socket_deinit;
    source->has_more  = thinros_socket_has_more;
    source->read_byte = thinros_socket_read_byte;

    sink            = sink_allocate(&pipeline->sinks, SINK_TYPE_ENCLAVE);
    sink->opaque    = ros;
    source->init    = (init_t)thinros_socket_init;
    source->cleanup = (cleanup_t)thinros_socket_deinit;
    sink->route     = thinros_socket_route_to;

    return SUCC;
}
