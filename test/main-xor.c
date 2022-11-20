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
#ifdef _STD_LIBC_

//    hook_tcpout(&secure_gateway_pipeline, "127.0.0.1", 5760, SOURCE_TYPE_VMC, SINK_TYPE_VMC);
    hook_udp(&secure_gateway_pipeline, 14551, SOURCE_TYPE_VMC, SINK_TYPE_VMC);
//    hook_udp(&secure_gateway_pipeline, 12002, SOURCE_TYPE_LEGACY, SINK_TYPE_LEGACY);
    //hook_udp(&secure_gateway_pipeline, 12022, SOURCE_TYPE_ENCLAVE(0), SINK_TYPE_ENCLAVE);
    hook_udp(&secure_gateway_pipeline, 12001, SOURCE_TYPE_LEGACY, SINK_TYPE_LEGACY);
//    hook_uart(&secure_gateway_pipeline, "/dev/ttyAMA0", SOURCE_ID_VMC, SINK_TYPE_VMC);
#endif
    hook_stdio_sink(&secure_gateway_pipeline, SINK_TYPE_DISCARD);
#ifdef USE_XOR
    add_transformer(&secure_gateway_pipeline, PORT_TYPE_SOURCE, SOURCE_TYPE_VMC, xor_decode);
    add_transformer(&secure_gateway_pipeline, PORT_TYPE_SINK, SINK_TYPE_VMC, xor_encode);
    secure_gateway_pipeline.transformer_enabled = true;
#endif

    pipeline_connect(&secure_gateway_pipeline);

    int rv;
    while (! secure_gateway_pipeline.terminated)
    {
        rv = pipeline_spin(&secure_gateway_pipeline);
        if (rv != SUCC)
        {
            WARN("pipeline_spin() failed with %d\n", rv);
            break;
        }
    }

    return 0;
}
