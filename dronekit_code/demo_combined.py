#!/usr/bin/env python3
from __future__ import print_function
import time
from dronekit import connect, VehicleMode, LocationGlobalRelative, Command
from util import upload_mission, distance_to_current_waypoint

connection_string = "udpout:127.0.0.1:14660"   #through thinros_app_udp
#connection_string = "udp:127.0.0.1:14551"   #direction connection to SITL (no secure gateway)
#connection_string = "/dev/ttyS0"   #RPI
vehicle = connect(connection_string, wait_ready=True, timeout=100, rate=1, heartbeat_timeout=100)

vehicle.parameters['RTL_AUTOLAND']=1
vehicle.mode = VehicleMode("MANUAL")

#Enable geofence
print("==============================")
print("Enabling geo fence")
vehicle.parameters['FENCE_ENABLE']=1
print("Fence enabled? ", vehicle.parameters['FENCE_ENABLE'])
print("==============================")

#Upload mission and takeoff
upload_mission(vehicle, 'mission.txt')
vehicle.mode = VehicleMode("AUTO")
vehicle.armed   = True

print("-----------------------------")

while True:
    mode = vehicle.mode.name
    lat = vehicle.location.global_relative_frame.lat
    lon = vehicle.location.global_relative_frame.lon
    alt = vehicle.location.global_relative_frame.alt
    airspeed = vehicle.airspeed
    fence = vehicle.parameters['FENCE_ENABLE']
    nextwaypoint=vehicle.commands.next
    dist_to_next_wp = distance_to_current_waypoint(vehicle)
    print("[%s] lat=%f, lon=%f, alt=%f, airspeed=%f, fence=%d, next wp=%d (%f m)" % (mode, lat, lon, alt, airspeed, fence, nextwaypoint, dist_to_next_wp))
    if (nextwaypoint==3 and dist_to_next_wp < 300.0):
        break

    time.sleep(1)



#Disable geofence
print("==============================")
print("Disabling geo fence")
vehicle.parameters['FENCE_ENABLE']=0
print("Fence enabled? ", vehicle.parameters['FENCE_ENABLE'])
print("==============================")


#Load the current mission
cmds = vehicle.commands
cmds.download()
cmds.wait_ready()
missionlist=[]
for cmd in cmds:
    missionlist.append(cmd)

print("-----------------------------")

print("Before WP4(x,y) = (%f, %f)" % (missionlist[4].x, missionlist[4].y))

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

print("After WP4(x,y) = (%f, %f)" % (missionlist[4].x, missionlist[4].y))

print("-----------------------------")
#for i in range(1,5):
while True:
    mode = vehicle.mode.name
    lat = vehicle.location.global_relative_frame.lat
    lon = vehicle.location.global_relative_frame.lon
    alt = vehicle.location.global_relative_frame.alt
    airspeed = vehicle.airspeed
    fence = vehicle.parameters['FENCE_ENABLE']
    nextwaypoint=vehicle.commands.next
    dist_to_next_wp = distance_to_current_waypoint(vehicle)
    print("[%s] lat=%f, lon=%f, alt=%f, airspeed=%f, fence=%d, next wp=%s (%f m)" % (mode, lat, lon, alt, airspeed, fence, nextwaypoint, dist_to_next_wp))
    time.sleep(1)


vehicle.close()
