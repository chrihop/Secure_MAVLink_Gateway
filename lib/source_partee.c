#include <gcc.h>
#include <string.h>
#include <types.h>
#include <stdio.h>
#include <certikos/partee.h>

#include "secure_gateway.h"


typedef struct mavlink_msg_t
{
    size_t  len;
    uint8_t data[MAVLINK_MAX_PACKET_LEN];
} mavlink_msg_t;

struct partee_socket_t
{
    bool          initialized;
    struct partee_publisher * publisher;
    struct partee_subscriber * subscriber;

    size_t        current_read;
    mavlink_msg_t msg;
};



static int
partee_socket_init(void* opaque)
{
    return SUCC;
}

static void
partee_socket_deinit(void* opaque)
{
}

static int
partee_socket_route_to(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL && "sink is NULL");
    ASSERT(sink->opaque != NULL && "sink->opaque is NULL");
    struct partee_socket_t* ros = (struct partee_socket_t*)sink->opaque;
    ASSERT(ros->initialized && "ros is not initialized");

    struct partee_msg partee_msg = {0};
    if(partee_msg_alloc(ros->publisher, &partee_msg) != 0)
    {
        printf("Failed to allocate message\n");
        return SEC_GATEWAY_IO_FAULT;
    }

    partee_msg.data_size_cached = mavlink_msg_to_send_buffer(
            partee_msg.slot->data,
            &msg->msg);
    partee_msg.slot->data_size = partee_msg.data_size_cached;

    if(partee_msg.data_size_cached > MAVLINK_MAX_PACKET_LEN)
    {
        printf("mavlink message too long\n");
        return SEC_GATEWAY_INVALID_PARAM;
    }

    partee_topic_publish(ros->publisher, &partee_msg);

    return SEC_GATEWAY_SUCCESS;
}

static int
partee_socket_has_more(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct partee_socket_t* ros = (struct partee_socket_t*)source->opaque;
    ASSERT(ros->initialized && "ros is not initialized");

    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return 1;
    }

    ros->msg.len = partee_topic_read(ros->subscriber, ros->msg.data);
    ros->current_read = 0;

    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return 1;
    }

    return 0;
}

static int
partee_socket_read_byte(struct source_t* source)
{
    ASSERT(source != NULL && "source is NULL");
    ASSERT(source->opaque != NULL && "source->opaque is NULL");
    struct partee_socket_t* ros = (struct partee_socket_t*)source->opaque;
    ASSERT(ros->initialized && "ros is not initialized");

    if (ros->msg.len > 0 && ros->current_read < ros->msg.len)
    {
        return ros->msg.data[ros->current_read++];
    }

    return 0;
}

int
hook_partee(
    struct pipeline_t* pipeline, const char* pub_topic, const char* sub_topic)
{
    ASSERT(pipeline != NULL && "pipeline is NULL");

    static struct partee_socket_t partee_socket = {
        .initialized = TRUE,
        .msg.len = 0,
    };

    partee_socket.publisher = partee_create_publisher(
            pub_topic,
            0, /* automatic queue depth */
            MAVLINK_MAX_PACKET_LEN);
    if(partee_socket.publisher == NULL)
    {
        printf("Failed to create publisher\n");
        return SEC_GATEWAY_IO_FAULT;
    }

    partee_socket.subscriber = partee_create_subscription(
            sub_topic,
            NULL,
            0,
            0, /* automatic queue depth */
            MAVLINK_MAX_PACKET_LEN);
    if(partee_socket.subscriber == NULL)
    {
        printf("Failed to create subscriber\n");
        return SEC_GATEWAY_IO_FAULT;
    }

    struct source_t* source = source_allocate(
            &pipeline->sources, SOURCE_TYPE_LEGACY);
    struct sink_t* sink = sink_allocate(&pipeline->sinks, SINK_TYPE_LEGACY);

    source->opaque      = &partee_socket;
    source->init        = partee_socket_init;
    source->cleanup     = partee_socket_deinit;
    source->has_more    = partee_socket_has_more;
    source->read_byte   = partee_socket_read_byte;

    sink->opaque        = &partee_socket;
    sink->route         = partee_socket_route_to;
    sink->init          = partee_socket_init;
    sink->cleanup       = partee_socket_deinit;

    return SUCC;
}
