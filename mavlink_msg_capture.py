import time
import sys
from pymavlink import mavutil
import pickle
from pymavlink.dialects.v20 import ardupilotmega as mavlink2

class fifo(object):
    def __init__(self):
        self.buf = []
    def write(self, data):
        self.buf += data
        return len(data)
    def read(self):
        return self.buf.pop(0)


conn = mavutil.mavlink_connection('udpin:localhost:14550')
conn.wait_heartbeat()
print("heartbeat received")

f = open('mavmsg_dump.bin', 'wb')

cnt = 0
NUM_MSGS = 1000
msg_list = []
while (cnt < NUM_MSGS):
    msg = conn.recv_match(blocking=True)
    if msg is None:
        break
    print(msg)
    buf = msg.get_msgbuf()
    msg_list.append(buf)
    cnt += 1

pickle.dump(msg_list, f)
f.close()


print("--------------------------------------")

f = open('mavmsg_dump.bin', 'rb')
msg_list = pickle.load(f)
f.close()

mav = mavlink2.MAVLink(fifo())
for b in msg_list:
    #print(b)
    m2 = mav.decode(b)
    print(m2)



