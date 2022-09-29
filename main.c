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
    pipeline_spin(&secure_gateway_pipeline);

    return 0;
}
