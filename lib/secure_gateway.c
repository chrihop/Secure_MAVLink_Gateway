#include "secure_gateway.h"

struct pipeline_t secure_gateway_pipeline;

#ifdef PROFILING
static struct perf_t perf_secure_gateway;
#endif

struct source_t*
source_allocate(struct source_mgmt_t* src_mgmt, size_t source_id)
{
    ASSERT(source_id < MAX_SOURCES && "not enough slots for sources");
    struct source_t* src = &src_mgmt->sources[source_id];
    src->source_id       = source_id;
    src->has_more        = NULL;
    src->read_byte       = NULL;
    src->transform       = NULL;
    src->is_connected    = true;
    return src;
}

struct sink_t*
sink_allocate(struct sink_mgmt_t* sink_mgmt, enum sink_type_t type)
{
    struct sink_t* sink = &sink_mgmt->sinks[type];
    sink->route         = NULL;
    sink->transform     = NULL;
    sink->is_connected  = true;
    return sink;
}

struct source_t *
    source_get(struct source_mgmt_t *src_mgmt, size_t source_id)
{
    ASSERT(source_id < MAX_SOURCES && "not enough slots for sources");
    return &src_mgmt->sources[source_id];
}

struct sink_t*
sink_get(struct sink_mgmt_t* sink_mgmt, enum sink_type_t type)
{
    ASSERT(type < MAX_SINKS && "not enough slots for sinks");
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
    memset(&pipeline->sources, 0, sizeof(pipeline->sources));
    memset(&pipeline->sinks, 0, sizeof(pipeline->sinks));
    pipeline->policies.count = 0;
    pipeline->push           = pipeline_push;
    pipeline->get_sink       = pipeline_get_sink;
    memcpy(&pipeline->route_table, &default_route_table,
        sizeof(struct route_table_t));
    security_policy_init(pipeline);
#ifdef PROFILING
    perf_init(&perf_secure_gateway);
#endif
}

void
pipeline_connect(struct pipeline_t* pipeline)
{
    for (size_t i = 0; i < MAX_SOURCES; i++)
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
    for (size_t i = 0; i < MAX_SOURCES; i++)
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

#ifdef PROFILING
    bool    has_load = false;
    uint64_t tstart, tend;
    tstart = time_us();
#endif
    for (i = 0; i < MAX_SOURCES; i++)
    {
        struct source_t* src = &pipeline->sources.sources[i];
        if (!src->is_connected)
        {
            continue;
        }
        ASSERT(src != NULL && "source is NULL");
        ASSERT(src->has_more != NULL && "has_more() is not implemented");
        ASSERT(src->read_byte != NULL && "read_byte() is not implemented");

        struct message_t* msg = &src->cur;
        while (src->has_more(src))
        {
            byte = src->read_byte(src);

#ifdef PROFILING
            has_load = true;
#endif

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
#ifdef PROFILING
                perf_port_unit_update(&perf_secure_gateway, PERF_PORT_UNIT_TYPE_SOURCE,
                    msg->source, msg);
#endif
                if (src->transform != NULL)
                {
                    src->transform(msg);
                }
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

#ifdef PROFILING
    tend = time_us();
    perf_exec_unit_update(&perf_secure_gateway, tend - tstart, has_load);
    perf_show(&perf_secure_gateway);
#endif

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
//    printf("msg %d: source %lu, msg id %d, seq %d, sys %d, comp %d, len %d\n",
//        msg_index++, msg->source, msg->msg.msgid, msg->msg.seq, msg->msg.sysid,
//        msg->msg.compid, msg->msg.len);

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
                if (sink->transform != NULL)
                {
                    sink->transform(msg);
                }

                sink->route(sink, msg);
#ifdef PROFILING
                perf_port_unit_update(&perf_secure_gateway, PERF_PORT_UNIT_TYPE_SINK,
                    i, msg);
#endif
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

void add_transformer(struct pipeline_t* pipeline, enum port_type_t type,
    size_t id, transform_t transform)
{
    if (type == PORT_TYPE_SOURCE)
    {
        ASSERT(id <= MAX_SOURCES && "source id is out of range");
        struct source_t* src = &pipeline->sources.sources[id];
        if (src != NULL)
        {
            src->transform = transform;
        }
    }
    else if (type == PORT_TYPE_SINK)
    {
        ASSERT(id <= MAX_SINKS && "sink id is out of range");
        struct sink_t* sink = sink_get(&pipeline->sinks, id);
        if (sink != NULL)
        {
            sink->transform = transform;
        }
    }
}


void perf_init(struct perf_t* perf)
{
    memset(perf, 0, sizeof(struct perf_t));
}

void perf_port_unit_update(struct perf_t * perf, enum perf_port_unit_type_t unit,
    size_t id, struct message_t * msg)
{
    if (unit == PERF_PORT_UNIT_TYPE_SOURCE)
    {
        ASSERT(id <= MAX_SOURCES && "source id is out of range");
        int drop_count = msg->msg.seq - perf->port_units[unit][id].prev_seq - 1;
        perf->port_units[unit][id].prev_seq = msg->msg.seq;
        drop_count = drop_count < 0 ? 256 + drop_count : drop_count;

        perf->port_units[unit][id].succ_count ++;
        perf->port_units[unit][id].drop_count += drop_count;
        perf->port_units[unit][id].succ_bytes += msg->msg.len + MAVLINK_NUM_NON_PAYLOAD_BYTES;
    }
    else if (unit == PERF_PORT_UNIT_TYPE_SINK)
    {
        ASSERT(id <= MAX_SINKS && "sink id is out of range");
        perf->port_units[unit][id].succ_count ++;
        perf->port_units[unit][id].succ_bytes += msg->msg.len + MAVLINK_NUM_NON_PAYLOAD_BYTES;
    }
}

void perf_port_unit_query(struct perf_t * perf, enum perf_port_unit_type_t unit,
    size_t id, uint64_t now, struct perf_port_unit_result_t * result)
{
    result->duration = now - perf->port_units[unit][id].last_query;
    result->succ_count = perf->port_units[unit][id].succ_count - perf->port_units[unit][id].last_succ_count;
    result->drop_count = perf->port_units[unit][id].drop_count - perf->port_units[unit][id].last_drop_count;
    result->succ_bytes = perf->port_units[unit][id].succ_bytes - perf->port_units[unit][id].last_succ_bytes;

    perf->port_units[unit][id].last_query = now;
    perf->port_units[unit][id].last_succ_count = perf->port_units[unit][id].succ_count;
    perf->port_units[unit][id].last_drop_count = perf->port_units[unit][id].drop_count;
    perf->port_units[unit][id].last_succ_bytes = perf->port_units[unit][id].succ_bytes;
}

void perf_exec_unit_update(struct perf_t * perf, uint64_t duration, bool has_load)
{
    perf->exec_unit.total ++;
    if (has_load)
    {
        perf->exec_unit.load_us += duration;
    }
}

void perf_exec_unit_query(struct perf_t * perf, uint64_t now, struct perf_exec_unit_result_t * result)
{
    result->duration = now - perf->exec_unit.last_query;
    result->count = perf->exec_unit.total - perf->exec_unit.last_total;
    result->load_us = perf->exec_unit.load_us - perf->exec_unit.last_load_us;

    perf->exec_unit.last_query = now;
    perf->exec_unit.last_total = perf->exec_unit.total;
    perf->exec_unit.last_load_us = perf->exec_unit.load_us;
}

static struct perf_result_t perf_results = {
    .select = {
        [PERF_PORT_UNIT_TYPE_SOURCE] = {
            [SOURCE_TYPE_VMC] = true,
        },
        [PERF_PORT_UNIT_TYPE_SINK] = {
            [SINK_TYPE_VMC] = true,
        },
    }
};

void perf_show(struct perf_t * perf)
{
    static uint64_t last = 0;
    uint64_t now = time_us();
    if (now - last < 2000000)
    {
        return;
    }

    for (size_t i = 0; i < MAX_PERF_PORT_UNITS; i++)
    {
        for (size_t j = 0; j < MAX_PERF_PORT_UNIT_TYPES; j++)
        {
            if (!perf_results.select[j][i])
            {
                continue;
            }
            perf_port_unit_query(perf, j, i, now, &perf_results.port_units[j][i]);
        }
    }
    perf_exec_unit_query(perf, now, &perf_results.exec_unit);
    for (size_t i = 0; i < MAX_PERF_PORT_UNITS; i++)
    {
        for (size_t j = 0; j < MAX_PERF_PORT_UNIT_TYPES; j++)
        {
            if (!perf_results.select[j][i])
            {
                continue;
            }
            uint64_t total_count = perf_results.port_units[j][i].succ_count + perf_results.port_units[j][i].drop_count;
            uint64_t duration = perf_results.port_units[j][i].duration;
            printf("%s %s %lu %lu/s %luB/s drop %lu/s (loss %lu.%02lu%%) | ",
                j == PERF_PORT_UNIT_TYPE_SOURCE
                    ? source_name(i) : perf_results.select[0][i] ? "" : sink_name(i),
                j == PERF_PORT_UNIT_TYPE_SOURCE ? "down" : "up",
                perf[0].port_units[j][i].succ_count + perf[0].port_units[j][i].drop_count,
                perf_results.port_units[j][i].succ_count * 1000000 / duration,
                perf_results.port_units[j][i].succ_bytes * 1000000 / duration,
                perf_results.port_units[j][i].drop_count * 1000000 / duration,
                total_count == 0 ? 0 : perf_results.port_units[j][i].drop_count * 100 / total_count,
                total_count == 0 ? 0 : perf_results.port_units[j][i].drop_count * 10000 / total_count % 100);
        }
    }

    printf("pipeline %lu %lu/s (load %lu.%03lu%%)\n",
        perf->exec_unit.total,
        perf_results.exec_unit.count * 1000000 / perf_results.exec_unit.duration,
        perf_results.exec_unit.load_us * 100 / perf_results.exec_unit.duration,
        perf_results.exec_unit.load_us * 100000 / perf_results.exec_unit.duration % 1000);

    last = now;
}
