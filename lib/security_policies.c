#include "secure_gateway.h"

bool
security_policy_match_all(
    struct security_policy_t* policy, struct message_t* msg)
{
    return true;
}

bool security_policy_check_reject(
    struct security_policy_t* policy, struct message_t* msg,
    size_t* sink, size_t* attribute)
{
    *sink = SINK_TYPE_DISCARD;
    return false;
}

bool security_policy_check_route_to_vmc(
    struct security_policy_t* policy, struct message_t* msg,
    size_t* sink, size_t* attribute)
{
    if (msg->source == SOURCE_ID_LEGACY || msg->source >= SOURCE_ID_ENCLAVE(0))
    {
        *sink = SINK_TYPE_VMC;
    }

    return true;
}

bool security_policy_check_route_to_mmc(
    struct security_policy_t* policy, struct message_t* msg,
    size_t* sink, size_t* attribute)
{
    if (msg->source == SOURCE_ID_VMC)
    {
        *sink = SINK_TYPE_VMC;
    }

    return true;
}



void security_policy_init(void)
{
}
