#!/usr/bin/env python
from __future__ import print_function
import time
from dronekit import connect, VehicleMode, LocationGlobalRelative, Command

#NOTE: This code is extracted from the 'simple_goto' example in dronekit

#Direct connection to SITL (localhost). That is, no secure gateway.
connection_string = "udp:127.0.0.1:14551"
vehicle = connect(connection_string, wait_ready=True)

#connection_string = "udpout:127.0.0.1:14660"
#vehicle = connect(connection_string, wait_ready=True)

#Through secure gateway
#connection_string = "tcp:127.0.0.1:12001"
#vehicle = connect(connection_string, wait_ready=True)

#Direct connection through uart (from RPI). That is, no secure gateway.
#connection_string = "/dev/ttyS0"
#vehicle = connect("/dev/ttyS0", wait_ready=True, baud=115200)
#vehicle.parameters['RTL_AUTOLAND']=1


#Disable geofence
vehicle.parameters['FENCE_ENABLE']=0

#Load the current mission
cmds = vehicle.commands
cmds.download()
cmds.wait_ready()
missionlist=[]
for cmd in cmds:
    missionlist.append(cmd)

print("Before (x,y) = (%f, %f)" % (missionlist[4].x, missionlist[4].y))

#Modify the last waypoint (outside of geofence)
missionlist[4].x = -35.37008899
missionlist[4].y = 149.16652867

#Upload the modified mission
cmds.clear()
for cmd in missionlist:
    cmds.add(cmd)
cmds.upload()

#Reload (just to check if the waypoint has been overwritten)
cmds = vehicle.commands
cmds.download()
cmds.wait_ready()
missionlist=[]
for cmd in cmds:
    missionlist.append(cmd)

print("After (x,y) = (%f, %f)" % (missionlist[4].x, missionlist[4].y))

for i in range(1,5):
    lat = vehicle.location.global_relative_frame.lat
    lon = vehicle.location.global_relative_frame.lon
    alt = vehicle.location.global_relative_frame.alt
    print(lat, lon, alt)
    time.sleep(1)

vehicle.close()

