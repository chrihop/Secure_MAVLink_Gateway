#include "secure_gateway.h"
#include <ardupilotmega/ardupilotmega.h>

int
security_policy_match_all(
    const struct security_policy_t* policy, const struct message_t* msg)
{
    return true;
}

int
security_policy_match_vmc(
    const struct security_policy_t* policy, const struct message_t* msg)
{
    return msg->source == SOURCE_ID_VMC;
}

int
security_policy_match_mmc(
    const struct security_policy_t* policy, const struct message_t* msg)
{
    return msg->source == SOURCE_ID_LEGACY || msg->source >= SOURCE_ID_ENCLAVE(0);
}

int security_policy_check_reject(
    const struct security_policy_t* policy, const struct message_t* msg, size_t* attribute)
{
    return false;
}

int security_policy_check_accept(
    const struct security_policy_t* policy, const struct message_t* msg, size_t* attribute)
{
    return true;
}

int
security_policy_reject_mavlink_cmd_meminfo(
    const struct security_policy_t* policy, const struct message_t* msg,
    size_t* attribute)
{
    if (msg->msg.msgid == MAVLINK_MSG_ID_MEMINFO)
        return false;
    return true;
}

int security_policy_reject_mavlink_cmd_waypoint(
    const struct security_policy_t* policy, const struct message_t* msg, size_t* attribute)
{
    if (msg->msg.msgid != MAVLINK_MSG_ID_COMMAND_LONG)
        return true;

    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg->msg, &cmd);

    if (cmd.command != MAV_CMD_NAV_WAYPOINT)
        return true;

    return false;
}

int security_policy_reject_mavlink_cmd_disable_geofence(
    const struct security_policy_t* policy, const struct message_t* msg, size_t* attribute)
{
    if (msg->msg.msgid != MAVLINK_MSG_ID_COMMAND_LONG)
        return true;

    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg->msg, &cmd);

    if (cmd.command != MAV_CMD_DO_FENCE_ENABLE)
        return true;

    if (cmd.param1 != 0)
        return true;

    return false;
}

enum policy_id_t
{
    POLICY_ID_ACCEPT_VMC,
    POLICY_ID_REJECT_NAV_WAYPOINT,
    POLICY_ID_REJECT_DISABLE_GEOFENCE,
    POLICY_ID_REJECT_MEMINFO,
};

void security_policy_init(struct pipeline_t * pipeline)
{
    policy_register(&pipeline->policies, POLICY_ID_ACCEPT_VMC, security_policy_match_vmc, security_policy_check_accept);
    policy_register(&pipeline->policies, POLICY_ID_REJECT_NAV_WAYPOINT, security_policy_match_mmc, security_policy_reject_mavlink_cmd_waypoint);
    policy_register(&pipeline->policies, POLICY_ID_REJECT_DISABLE_GEOFENCE, security_policy_match_mmc, security_policy_reject_mavlink_cmd_disable_geofence);
    policy_register(&pipeline->policies, POLICY_ID_REJECT_MEMINFO, security_policy_match_mmc, security_policy_reject_mavlink_cmd_meminfo);
    /* ... */
}
