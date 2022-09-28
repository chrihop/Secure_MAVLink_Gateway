#include "secure_gateway.h"

void pipline_init(struct pipeline_t * pipline)
{
    pipline->sources.count = 0;
    pipline->sinks.count = 0;
    pipline->policies.count = 0;
}

int pipline_spin(struct pipeline_t * pipeline)
{
    int rv = 0;
    size_t i;

    for (i = 0; i < pipeline->sources.count; i++)
    {
        struct source_t * src = &pipeline->sources.sources[i];

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

int pipline_push(struct pipeline_t * pipeline, struct message_t * msg)
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
        if (sink == EMPTY_SINK_ID)
        {
            /* next policy */
            continue ;
        }

        struct sink_t * s = pipeline->get_sink(pipeline, sink);
        ASSERT(s != NULL);
        s->route(s, msg);
    }
    return 0;
}
