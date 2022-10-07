#include "secure_gateway.h"

struct pipeline_t secure_gateway_pipeline;

struct source_t*
source_allocate(struct source_mgmt_t* src_mgmt, size_t source_id)
{
    ASSERT(src_mgmt->count < MAX_SOURCES && "not enough slots for sources");
    struct source_t* src = &src_mgmt->sources[src_mgmt->count];
    src->source_id       = source_id;
    src->has_more        = NULL;
    src->read_byte       = NULL;
    src->is_connected    = true;
    src_mgmt->count++;
    return src;
}

struct sink_t*
sink_allocate(struct sink_mgmt_t* sink_mgmt, enum sink_type_t type)
{
    struct sink_t* sink = &sink_mgmt->sinks[type];
    sink->route         = NULL;
    sink->is_connected  = true;
    return sink;
}

struct sink_t*
sink_get(struct sink_mgmt_t* sink_mgmt, enum sink_type_t type)
{
    return &sink_mgmt->sinks[type];
}

int
policy_register(struct security_policy_mgmt_t* policy_mgmt, size_t policy_id,
    match_t match, check_t check)
{
    ASSERT(
        policy_mgmt->count < MAX_POLICIES && "not enough slots for policies");
    struct security_policy_t* p = &policy_mgmt->policies[policy_mgmt->count];
    p->policy_id                = policy_id;
    p->match                    = match;
    p->check                    = check;
    policy_mgmt->count++;
    return 0;
}

extern struct route_table_t default_route_table;

void
pipeline_init(struct pipeline_t* pipeline)
{
    pipeline->sources.count  = 0;
    pipeline->policies.count = 0;
    pipeline->push           = pipeline_push;
    pipeline->get_sink       = pipeline_get_sink;
    memcpy(&pipeline->route_table, &default_route_table,
        sizeof(struct route_table_t));
    security_policy_init(pipeline);
}

void
pipeline_connect(struct pipeline_t* pipeline)
{
    for (size_t i = 0; i < pipeline->sources.count; i++)
    {
        struct source_t* src = &pipeline->sources.sources[i];
        if (src->is_connected && src->init != NULL)
        {
            src->init(src);
        }
    }

    for (size_t i = 0; i < MAX_SINKS; i++)
    {
        struct sink_t* sink = &pipeline->sinks.sinks[i];
        if (sink->is_connected && sink->init != NULL)
        {
            sink->init(sink);
        }
    }
}

void
pipeline_disconnect(struct pipeline_t* pipeline)
{
    for (size_t i = 0; i < pipeline->sources.count; i++)
    {
        struct source_t* src = &pipeline->sources.sources[i];
        if (src->is_connected && src->cleanup != NULL)
        {
            src->cleanup(src);
        }
    }

    for (size_t i = 0; i < MAX_SINKS; i++)
    {
        struct sink_t* sink = &pipeline->sinks.sinks[i];
        if (sink->is_connected && sink->cleanup != NULL)
        {
            sink->cleanup(sink);
        }
    }
}

int
pipeline_spin(struct pipeline_t* pipeline)
{
    int     rv = 0;
    size_t  i;
    uint8_t byte;
    uint8_t drop;

    for (i = 0; i < pipeline->sources.count; i++)
    {
        struct source_t* src = &pipeline->sources.sources[i];
        ASSERT(src != NULL && "source is NULL");
        ASSERT(src->has_more != NULL && "has_more() is not implemented");
        ASSERT(src->read_byte != NULL && "read_byte() is not implemented");

        struct message_t* msg = &src->cur;
        while (src->has_more(src))
        {
            byte = src->read_byte(src);
#ifdef DEBUG
            drop = msg->status.packet_rx_drop_count;
#endif
            rv = mavlink_parse_char(i, byte, &msg->msg, &msg->status);
            if (rv == 0)
            {
#ifdef DEBUG
                if (drop != msg->status.packet_rx_drop_count)
                {
                    WARN("MAVLink parser error: message dropped!\n");
                }
#endif
                continue;
            }
            else if (rv == 1)
            {
                msg->source = src->source_id;
                pipeline->push(pipeline, msg);
            }
            else
            {
                /* error */
                WARN("source %lu: error parsing mavlink message, error code: "
                     "%d\n",
                    src->source_id, rv);
            }
        }
    }
    return SUCC;
}

static int
route_table_route(struct route_table_t* route_table, struct message_t* msg)
{
    ASSERT(msg != NULL && "message is NULL");
    ASSERT(route_table != NULL && "route table is NULL");
    ASSERT(msg->source < MAX_SOURCES && "source id is out of range");

    memcpy(&msg->sinks, &route_table->table[msg->source], sizeof(msg->sinks));
    return 0;
}

int
pipeline_push(struct pipeline_t* pipeline, struct message_t* msg)
{
    int    rv;
    size_t i;

    ASSERT(pipeline != NULL && "pipeline is NULL");
    ASSERT(msg != NULL && "message is NULL");

    rv = route_table_route(&pipeline->route_table, msg);
    //    static int msg_index = 0;
    //    INFO("msg %d id %d (sz=%d) [%ld -> %lx]\n", msg_index++,
    //    msg->msg.msgid,
    //        msg->msg.len, msg->source, msg->sinks.data[0]);

    for (i = 0; i < pipeline->policies.count; i++)
    {
        struct security_policy_t* policy = &pipeline->policies.policies[i];
        if (!policy->match(policy, msg))
        {
            continue;
        }
        size_t attribute = msg->attribute;

        int    is_secure = policy->check(policy, msg, &attribute);

        /* write only attribute */
        msg->attribute |= attribute;
        if (!is_secure)
        {
            WARN("policy %lu: message rejected\n", i);
            bitmap_set(&msg->sinks, SINK_TYPE_DISCARD);
            break;
        }
    }

    if (bitmap_test(&msg->sinks, SINK_TYPE_DISCARD))
    {
#ifdef DEBUG
        struct sink_t* sink = pipeline->get_sink(pipeline, SINK_TYPE_DISCARD);
        if (sink != NULL && sink->route != NULL)
        {
            sink->route(sink, msg);
        }
#endif
        return rv;
    }

    for (i = 0; i < MAX_SINKS; i++)
    {
        if (bitmap_test(&msg->sinks, i))
        {
            struct sink_t* sink = pipeline->get_sink(pipeline, i);
            if (sink->route != NULL)
            {
                sink->route(sink, msg);
            }
        }
    }

    return rv;
}

struct sink_t*
pipeline_get_sink(struct pipeline_t* pipeline, enum sink_type_t type)
{
    return sink_get(&pipeline->sinks, type);
}
