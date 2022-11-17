#ifndef _SECURE_GATEWAY_H_
#define _SECURE_GATEWAY_H_

#include "context.h"

#define MAVLINK_USE_MESSAGE_INFO
#include <mavlink.h>

#define PROFILING

#define BITMAP_MAX_LEN 64
struct bitmap_t
{
    uint64_t data[BITMAP_MAX_LEN / 64 + 1];
};

static inline void
bitmap_clear(struct bitmap_t* bitmap)
{
    memset(bitmap->data, 0, sizeof(bitmap->data));
}

static inline void
bitmap_set(struct bitmap_t* bitmap, size_t index)
{
    bitmap->data[index / 64] |= (1ULL << (index % 64));
}

static inline void
bitmap_unset(struct bitmap_t* bitmap, size_t index)
{
    bitmap->data[index / 64] &= ~(1ULL << (index % 64));
}

static inline void
bitmap_toggle(struct bitmap_t* bitmap, size_t index)
{
    bitmap->data[index / 64] ^= (1ULL << (index % 64));
}

static inline int
bitmap_test(struct bitmap_t* bitmap, size_t index)
{
    return (int)(bitmap->data[index / 64] & (1ULL << (index % 64)));
}

#define BITMAP_CONST(...)                                                      \
    {                                                                          \
        .data = { __VA_ARGS__ }                                                \
    }

#define BIT_OF(index) (1ULL << (index % 64))

static size_t
mavlink_msg_length(mavlink_message_t* msg)
{
    if (msg->magic == MAVLINK_STX_MAVLINK1)
    {
        return MAVLINK_CORE_HEADER_MAVLINK1_LEN + msg->len + 3;
    }
    else
    {
        return MAVLINK_CORE_HEADER_LEN
            + _mav_trim_payload(_MAV_PAYLOAD(msg), msg->len)
            + ((msg->incompat_flags & MAVLINK_IFLAG_SIGNED)
                    ? MAVLINK_SIGNATURE_BLOCK_LEN
                    : 0)
            + 3;
    }
}

enum sec_gateway_error_code_t
{
    SUCC                = 0,
    SEC_GATEWAY_SUCCESS = 0,
    SEC_GATEWAY_INVALID_PARAM,
    SEC_GATEWAY_INVALID_STATE,
    SEC_GATEWAY_INVALID_INDEX,
    SEC_GATEWAY_IO_FAULT,
    SEC_GATEWAY_NO_CLIENT,
    SEC_GATEWAY_NO_MEMORY,
    SEC_GATEWAY_NO_RESOURCE,
    SEC_GATEWAY_THREAD_ERROR,
};

enum port_type_t
{
    PORT_TYPE_SOURCE = 0,
    PORT_TYPE_SINK,

    MAX_PORT_TYPE
};

struct message_t
{
    mavlink_message_t msg;
    mavlink_status_t  status;
    struct bitmap_t   sinks;
    size_t            source;
    size_t            attribute;
};

struct source_t;

typedef int (*has_more_t)(struct source_t* src);
typedef int (*read_byte_t)(struct source_t* src);
typedef void (*transform_t)(struct message_t* msg);
typedef int (*init_t)(void* obj);
typedef void (*cleanup_t)(void* obj);

enum source_type_t
{
    SOURCE_TYPE_NULL = 0,
    SOURCE_TYPE_VMC,
    SOURCE_TYPE_LEGACY,
    SOURCE_TYPE_ENCLAVE,

    MAX_SOURCES
};

static inline const char*
source_name(enum source_type_t src)
{
    switch (src)
    {
    case SOURCE_TYPE_NULL: return "null";
    case SOURCE_TYPE_VMC: return "vmc";
    case SOURCE_TYPE_LEGACY: return "legacy";
    default: return "enclave";
    }
}

struct source_t
{
    bool             is_connected;
    size_t           source_id;
    struct message_t cur;
    void*            opaque;

    /* operations */
    has_more_t       has_more;
    read_byte_t      read_byte;
    transform_t      transform;
    init_t           init;
    cleanup_t        cleanup;
};

struct source_mgmt_t
{
    struct source_t sources[MAX_SOURCES];
};

struct source_t* source_allocate(
    struct source_mgmt_t* src_mgmt, size_t source_id);

struct sink_t;

typedef int (*route_t)(struct sink_t* sink, struct message_t* msg);

struct sink_t
{
    bool        is_connected;
    void*       opaque;

    /* operations */
    route_t     route;
    transform_t transform;
    init_t      init;
    cleanup_t   cleanup;
};

enum sink_type_t
{
    SINK_TYPE_DISCARD,
    SINK_TYPE_VMC,
    SINK_TYPE_ENCLAVE,
    SINK_TYPE_LEGACY,

    MAX_SINKS,
};

static_assert(MAX_SINKS <= BITMAP_MAX_LEN, "MAX_SINKS > BITMAP_MAX_LEN");

#define INVALID_SINK_ID (MAX_SINKS)
#define EMPTY_SINK_ID   (MAX_SINKS)

static inline const char*
sink_name(size_t sink_id)
{
    switch (sink_id)
    {
    case SINK_TYPE_DISCARD: return "discard";
    case SINK_TYPE_VMC: return "vmc";
    case SINK_TYPE_ENCLAVE: return "enclave";
    case SINK_TYPE_LEGACY: return "legacy";
    default: return "invalid";
    }
}

struct sink_mgmt_t
{
    struct sink_t sinks[MAX_SINKS];
};

struct sink_t* sink_allocate(
    struct sink_mgmt_t* sink_mgmt, enum sink_type_t type);

struct security_policy_t;

typedef int (*match_t)(
    const struct security_policy_t* policy, const struct message_t* msg);
typedef int (*check_t)(const struct security_policy_t* policy,
    const struct message_t* msg, size_t* attribute);

struct security_policy_t
{
    size_t  policy_id;

    /* operations */
    match_t match;
    check_t check;
};

#define MAX_POLICIES 16
struct security_policy_mgmt_t
{
    struct security_policy_t policies[MAX_POLICIES];
    size_t                   count;
};

int policy_register(struct security_policy_mgmt_t* policy_mgmt,
    size_t policy_id, match_t match, check_t check);

struct route_table_t
{
    struct bitmap_t table[MAX_SOURCES];
};

struct pipeline_t;

typedef int (*push_t)(struct pipeline_t* pipeline, struct message_t* msg);
typedef struct sink_t* (*get_sink_t)(
    struct pipeline_t* pipeline, enum sink_type_t type);

struct pipeline_t
{
    struct source_mgmt_t          sources;
    struct sink_mgmt_t            sinks;
    struct route_table_t          route_table;
    struct security_policy_mgmt_t policies;

    /* operations */
    push_t                        push;
    get_sink_t                    get_sink;
};

extern struct pipeline_t secure_gateway_pipeline;

/* performance accounting */

enum perf_port_unit_type_t
{
    PERF_PORT_UNIT_TYPE_SOURCE = 0,
    PERF_PORT_UNIT_TYPE_SINK,

    MAX_PERF_PORT_UNIT_TYPES
};

#define MAX_PERF_PORT_UNITS 4
#define QUERY_FREQUENCY     1000

_Static_assert(
    MAX_PERF_PORT_UNITS <= MAX_SOURCES, "perf port units not long enough");
_Static_assert(
    MAX_PERF_PORT_UNITS <= MAX_SINKS, "perf port units not long enough");

struct perf_port_unit_result_t
{
    uint64_t duration;
    uint64_t succ_count;
    uint64_t drop_count;
    uint64_t succ_bytes;
};

struct perf_port_unit_t
{
    int packet_rx_drop_count;

    uint64_t succ_count;
    uint64_t drop_count;
    uint64_t succ_bytes;
    uint64_t last_succ_count;
    uint64_t last_drop_count;
    uint64_t last_succ_bytes;
    uint64_t last_query;
};

struct perf_exec_unit_result_t
{
    uint64_t duration;
    uint64_t count;
    uint64_t load_us;
};

struct perf_exec_unit_t
{
    uint64_t total;
    uint64_t load_us;
    uint64_t last_total;
    uint64_t last_load_us;
    uint64_t last_query;
};

struct perf_result_t
{
    bool select[MAX_PERF_PORT_UNIT_TYPES][MAX_PERF_PORT_UNITS];
    struct perf_port_unit_result_t port_units[MAX_PERF_PORT_UNIT_TYPES]
                                             [MAX_PERF_PORT_UNITS];
    struct perf_exec_unit_result_t exec_unit;
};

struct perf_t
{
    struct perf_port_unit_t port_units[MAX_PERF_PORT_UNIT_TYPES]
                                      [MAX_PERF_PORT_UNITS];
    struct perf_exec_unit_t exec_unit;
};

void perf_init(struct perf_t* perf);
void perf_port_unit_update(struct perf_t* perf, enum perf_port_unit_type_t unit,
    size_t id, struct message_t* msg);
void perf_port_unit_query(struct perf_t* perf, enum perf_port_unit_type_t unit,
    size_t id, uint64_t now, struct perf_port_unit_result_t* result);
void perf_exec_unit_update(struct perf_t* perf, uint64_t duration, bool empty);
void perf_exec_unit_query(
    struct perf_t* perf, uint64_t now, struct perf_exec_unit_result_t* result);
void perf_show(struct perf_t* perf);

/* secure gateway */
void security_policy_init(struct pipeline_t* pipeline);

void pipeline_init(struct pipeline_t* pipline);
void pipeline_connect(struct pipeline_t* pipeline);
int  pipeline_spin(struct pipeline_t* pipeline);
int  pipeline_push(struct pipeline_t* pipeline, struct message_t* msg);
struct sink_t* pipeline_get_sink(
    struct pipeline_t* pipeline, enum sink_type_t type);
void pipeline_disconnect(struct pipeline_t* pipeline);

void add_transformer(struct pipeline_t* pipeline, enum port_type_t type,
    size_t id, transform_t transform);

#ifdef _STD_LIBC_
int hook_tcp(struct pipeline_t* pipeline, int port, size_t source_id,
    enum sink_type_t sink_type);
int hook_udp(struct pipeline_t* pipeline, int port, size_t source_id,
    enum sink_type_t sink_type);
int hook_tcpout(struct pipeline_t* pipeline, const char * ip, int port, size_t source_id,
    enum sink_type_t sink_type);

#define DEVICE_USB(n) "/dev/ttyUSB" #n
#define DEVICE_S(n)   "/dev/ttyS" #n
#define DEVICE_AMA(n) "/dev/ttyAMA" #n
int hook_uart(struct pipeline_t* pipeline, char* device, size_t source_id,
    enum sink_type_t sink_type);

#endif

#ifdef _CERTIKOS_
int hook_thinros(
    struct pipeline_t* pipeline, const char* pub_topic, const char* sub_topic);
int hook_certikos_uart(struct pipeline_t* pipeline, size_t dev,
    size_t source_id, enum sink_type_t sink_type);
#endif

void hook_stdio_sink(struct pipeline_t* pipeline, enum sink_type_t sink_type);

#ifdef USE_XOR
void xor_encode(struct message_t* msg);
void xor_decode(struct message_t* msg);
#endif

#endif /* !_SECURE_GATEWAY_H_ */
