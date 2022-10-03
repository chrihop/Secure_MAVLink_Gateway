#!/bin/env python3

import time
import sys
from pymavlink import mavutil
import pickle
from pymavlink.dialects.v20 import ardupilotmega as mavlink2
import datetime

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

start_time = datetime.datetime.now()

cnt = 0
NUM_MSGS = 1000
msg_list = []
while (cnt < NUM_MSGS):
    msg = conn.recv_match(blocking=True)
    if msg is None:
        break

    curr_time = datetime.datetime.now()
    rel_time_ms = (curr_time-start_time).total_seconds() * 1000
    rel_time_ms = int(rel_time_ms)
    print(rel_time_ms, msg)
    buf = msg.get_msgbuf()
    msg_list.append( (rel_time_ms, buf) )
    cnt += 1

pickle.dump(msg_list, f)
f.close()


print("--------------------------------------")

f = open('mavmsg_dump.bin', 'rb')
msg_list = pickle.load(f)
f.close()

mav = mavlink2.MAVLink(fifo())
for rel_time_ms, b in msg_list:
    #print(b)
    m2 = mav.decode(b)
    print(rel_time_ms, m2)



