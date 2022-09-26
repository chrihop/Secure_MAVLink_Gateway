#include <ardupilotmega/mavlink.h>

#ifdef _STD_LIBC_
#include <stdio.h>
#endif

int main() {
    mavlink_message_t msg;
    msg.checksum = 1;
    return msg.checksum;
}
