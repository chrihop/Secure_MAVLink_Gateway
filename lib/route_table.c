#include "secure_gateway.h"

struct route_table_t default_route_table =
    {
        .table = {
            [SOURCE_TYPE_NULL] = {.data = {0}},
            [SOURCE_TYPE_VMC]  = BITMAP_CONST(BIT_OF(SINK_TYPE_LEGACY) | BIT_OF(SINK_TYPE_ENCLAVE)),
            [SOURCE_TYPE_LEGACY] = BITMAP_CONST(BIT_OF(SINK_TYPE_VMC)),
            [SOURCE_TYPE_ENCLAVE] = BITMAP_CONST(BIT_OF(SINK_TYPE_VMC)),
        }
    };
