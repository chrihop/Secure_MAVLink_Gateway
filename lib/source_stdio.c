#include "secure_gateway.h"

#ifdef _STD_LIBC_
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#endif

static int
stdio_init(void* opaque)
{
#ifdef _STD_LIBC_
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
#endif
    return SUCC;
}

static void
stdio_cleanup(void* opaque)
{
#ifdef _STD_LIBC_
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) & ~O_NONBLOCK);
#endif
}

static int
stdio_route(struct sink_t* sink, struct message_t* msg)
{
    ASSERT(sink != NULL);

    printf(">> message [%s (%lu) -> ", source_name(msg->source), msg->source);
    for (size_t i = 0; i < MAX_SINKS; i++)
    {
        if (bitmap_test(&msg->sinks, i))
        {
            printf("%s (%lu), ", sink_name(i), i);
        }
    }
    printf("]: attr 0x%lx ", msg->attribute);

    const mavlink_message_info_t* info = mavlink_get_message_info(&msg->msg);
    if (info != NULL)
    {
        printf("%s (%d) {", info->name, info->msgid);
        for (unsigned i = 0; i < info->num_fields; i++)
        {
            printf("%s: ", info->fields[i].name);
            switch (info->fields[i].type)
            {
                case MAVLINK_TYPE_CHAR:
                case MAVLINK_TYPE_UINT8_T:
                case MAVLINK_TYPE_INT8_T:
                    printf("%02x", *((uint8_t *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                case MAVLINK_TYPE_UINT16_T:
                case MAVLINK_TYPE_INT16_T:
                    printf("%04x", *((uint16_t *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                case MAVLINK_TYPE_UINT32_T:
                case MAVLINK_TYPE_INT32_T:
                    printf("%08x", *((uint32_t *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                case MAVLINK_TYPE_UINT64_T:
                case MAVLINK_TYPE_INT64_T:
                    printf("%016lx", *((uint64_t *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                case MAVLINK_TYPE_FLOAT:
                    printf("%f", *((float *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                case MAVLINK_TYPE_DOUBLE:
                    printf("%f", *((double *)((uint8_t*) msg->msg.payload64) + info->fields[i].structure_offset));
                    break;
                default:
                    printf("unknown type %d", info->fields[i].type);
                    break;
            }
            printf(", ");
        }
        printf("}");
    }
    printf("\n");

    return SUCC;
}

void
hook_stdio_sink(struct pipeline_t* pipeline, enum sink_type_t sink_type)
{
    ASSERT(pipeline != NULL);

    struct sink_t* sink = pipeline->get_sink(pipeline, sink_type);
    sink->init          = stdio_init;
    sink->cleanup       = stdio_cleanup;
    sink->route         = stdio_route;
}
