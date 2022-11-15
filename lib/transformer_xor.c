#include <secure_gateway.h>
#include <string.h>

static void xor_crypto(char* message, uint8_t len)
{
    char key = 'X';

    for (int i = 0; i < len; i++)
    {
        message[i] = message[i] ^ key;
    }
}

void xor_encode(struct message_t* msg)
{
    int len = msg->msg.len;

    xor_crypto((char *) msg->msg.payload64, len);
    mavlink_finalize_message_buffer(&msg->msg, msg->msg.sysid,
        msg->msg.compid, &msg->status, len, len,
        mavlink_get_crc_extra(&msg->msg));
}

void xor_decode(struct message_t* msg)
{
    int len = msg->msg.len;

    xor_crypto((char *) msg->msg.payload64, len);
    mavlink_finalize_message_buffer(&msg->msg, msg->msg.sysid,
        msg->msg.compid, &msg->status, len, len,
        mavlink_get_crc_extra(&msg->msg));
}
