#ifndef _SECURE_GATEWAY_H_
#define _SECURE_GATEWAY_H_

#include <mavlink.h>
#include "context.h"

struct message_t
{
    mavlink_message_t msg;
    mavlink_status_t status;
    size_t sink;
    size_t source;
    size_t attribute;
};

struct source_t;

typedef int (*has_more_t)(struct source_t * src);
typedef int (*read_byte_t)(struct source_t * src);


#define SOURCE_ID_NULL        (0)
#define SOURCE_ID_VMC         (1)
#define SOURCE_ID_LEGACY      (2)
#define SOURCE_ID_ENCLAVE(x)  (3 + x)

struct source_t
{
    size_t  src_id;
    uint8_t chan;
    struct message_t cur;

    /* operations */
    has_more_t has_more;
    read_byte_t read_byte;
};

#define MAX_SOURCES 4
struct source_mgmt_t
{
    struct source_t sources[MAX_SOURCES];
    size_t          count;
};

struct sink_t;

typedef int (*route_t)(struct sink_t * sink, struct message_t * msg);

struct sink_t
{
    size_t sink_id;

    /* operations */
    route_t route;
};

enum sink_type_t
{
    SINK_TYPE_DISCARD,
    SINK_TYPE_VMC,
    SINK_TYPE_ENCLAVE,
    SINK_TYPE_LEGACY,
    SINK_TYPE_MMC, /* both enclave and legacy */

    MAX_SINKS,
};

#define INVALID_SINK_ID (MAX_SINKS)
#define EMPTY_SINK_ID   (MAX_SINKS)

struct sink_mgmt_t
{
    struct sink_t sinks[MAX_SINKS];
};

struct security_policy_t;

typedef int (*match_t)(struct security_policy_t * policy, struct message_t * msg);
typedef int (*check_t)(struct security_policy_t * policy, struct message_t * msg, size_t * sink, size_t * attribute);

struct security_policy_t
{
    size_t policy_id;

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

struct pipeline_t;

typedef int (*push_t)(struct pipeline_t * pipeline, struct message_t * msg);
typedef struct sink_t * (*get_sink_t)(struct pipeline_t * pipeline, enum sink_type_t type);

struct pipeline_t
{
    struct source_mgmt_t       sources;
    struct sink_mgmt_t         sinks;
    struct security_policy_mgmt_t policies;

    /* operations */
    push_t push;
    get_sink_t get_sink;
};

struct pipeline_t secure_gateway_pipeline;

void pipeline_init(struct pipeline_t * pipline);
int pipeline_spin(struct pipeline_t * pipeline);
int pipeline_push(struct pipeline_t * pipeline, struct message_t * msg);
struct sink_t * pipeline_get_sink(struct pipeline_t * pipeline, enum sink_type_t type);



#endif /* !_SECURE_GATEWAY_H_ */
