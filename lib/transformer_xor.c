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
    xor_crypto((char *) msg->msg.payload64, msg->msg.len);
}

void xor_decode(struct message_t* msg)
{
    xor_crypto((char *) msg->msg.payload64, msg->msg.len);
}
