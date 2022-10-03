#include "secure_gateway.h"

struct route_table_t default_route_table =
    {
        .table = {
            [SOURCE_ID_NULL] = {.data = 0},
            [SOURCE_ID_VMC]  = BITMAP_CONST(BIT_OF(SINK_TYPE_LEGACY) | BIT_OF(SINK_TYPE_ENCLAVE)),
            [SOURCE_ID_LEGACY] = BITMAP_CONST(BIT_OF(SINK_TYPE_VMC)),
            [SOURCE_ID_ENCLAVE(0)] = BITMAP_CONST(BIT_OF(SINK_TYPE_VMC)),
        }
    };
