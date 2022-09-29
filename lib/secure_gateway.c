#include "secure_gateway.h"

struct pipeline_t secure_gateway_pipeline;

int source_allocate(struct source_mgmt_t * src_mgmt, size_t src_id, uint8_t chan)
{
    ASSERT(src_mgmt->count < MAX_SOURCES && "not enough slots for sources");
    struct source_t * src = &src_mgmt->sources[src_mgmt->count];
    src->src_id = src_id;
    src->chan = chan;
    src->has_more = NULL;
    src->read_byte = NULL;
    src_mgmt->count++;
    return 0;
}

int sink_allocate(struct sink_mgmt_t * sink_mgmt, size_t sink_id, enum sink_type_t type)
{
    struct sink_t * sink = &sink_mgmt->sinks[type];
    sink->sink_id = sink_id;
    sink->route = NULL;
    return 0;
}

struct sink_t * sink_get(struct sink_mgmt_t * sink_mgmt, enum sink_type_t type)
{
    return &sink_mgmt->sinks[type];
}

int policy_register(struct security_policy_mgmt_t * policy_mgmt,
    size_t policy_id, match_t match, check_t check)
{
    ASSERT(policy_mgmt->count < MAX_POLICIES && "not enough slots for policies");
    struct security_policy_t * p = &policy_mgmt->policies[policy_mgmt->count];
    p->policy_id = policy_id;
    p->match = match;
    p->check = check;
    return 0;
}

void pipeline_init(struct pipeline_t * pipeline)
{
    pipeline->sources.count = 0;
    pipeline->policies.count = 0;
    pipeline->push = pipeline_push;
    pipeline->get_sink = pipeline_get_sink;
}

int pipeline_spin(struct pipeline_t * pipeline)
{
    int rv = 0;
    size_t i;

    for (i = 0; i < pipeline->sources.count; i++)
    {
        struct source_t * src = &pipeline->sources.sources[i];
        ASSERT(src != NULL && "source is NULL");
        ASSERT(src->has_more != NULL && "has_more() is not implemented");
        ASSERT(src->read_byte != NULL && "read_byte() is not implemented");

        struct message_t * msg = &src->cur;
        while (src->has_more(src))
        {
            uint8_t byte = src->read_byte(src);
            rv = mavlink_parse_char(src->chan, byte, &msg->msg, &msg->status);
            if (rv == 0)
            {
                continue;
            }
            else if (rv == 1)
            {
                msg->source = src->src_id;
                pipeline->push(pipeline, msg);
            }
            else
            {
                /* error */
                WARN("source %u: error parsing mavlink message, error code: "
                     "%d\n", src->chan, rv);
            }
        }
    }
    return rv;
}

int pipeline_push(struct pipeline_t * pipeline, struct message_t * msg)
{
    size_t i;
    for (i = 0; i < pipeline->policies.count; i++)
    {
        struct security_policy_t * policy = &pipeline->policies.policies[i];
        if (!policy->match(policy, msg))
        {
            continue;
        }
        size_t sink = EMPTY_SINK_ID;
        size_t attribute = msg->attribute;

        int is_secure = policy->check(policy, msg, &sink, &attribute);
        if (!is_secure)
        {
            WARN("policy %lu: message rejected\n", i);
            msg->sink = SINK_TYPE_DISCARD;
            break;
        }

        /* write only attribute */
        msg->attribute |= attribute;
        if (sink == SINK_TYPE_DISCARD)
        {
            break ;
        }
    }

    struct sink_t* s = pipeline->get_sink(pipeline, msg->sink);
    ASSERT(s != NULL);
    s->route(s, msg);

    return 0;
}

struct sink_t * pipeline_get_sink(struct pipeline_t * pipeline, enum sink_type_t type)
{
    return sink_get(&pipeline->sinks, type);
}
