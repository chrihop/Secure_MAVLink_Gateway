#include <secure_gateway.h>

/**
 * Subsystem
 */
mavlink_system_t mavlink_system = {
    1, // System ID
    1, // Component ID
};

int main()
{
    pipeline_init(&secure_gateway_pipeline);
//    hook_tcp(&secure_gateway_pipeline, 12001, SOURCE_ID_LEGACY, SINK_TYPE_LEGACY);
    hook_udp(&secure_gateway_pipeline, 12002, SOURCE_ID_LEGACY, SINK_TYPE_LEGACY);

    hook_tcp(&secure_gateway_pipeline, 12011, SOURCE_ID_VMC, SINK_TYPE_VMC);

    pipeline_spin(&secure_gateway_pipeline);

    return 0;
}
