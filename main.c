#include <secure_gateway.h>

/**
 * Subsystem
 */
mavlink_system_t mavlink_system = {
    1, // System ID
    1, // Component ID
};

static struct pipeline_t pipeline;

int main()
{
    /* initialize the pipeline */
    pipline_init(&pipeline);
    pipeline.push = pipline_push;

    pipline_spin(&pipeline);

    return 0;
}
