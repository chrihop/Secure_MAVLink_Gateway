#!/usr/bin/env python
from __future__ import print_function
import time
from dronekit import connect, VehicleMode, LocationGlobalRelative

#NOTE: This code is extracted from the 'simple_goto' example in dronekit

#Direct connection to SITL (localhost). That is, no secure gateway.
#connection_string = "udp:127.0.0.1:14551"
#vehicle = connect(connection_string, wait_ready=True)


#Through secure gateway
#connection_string = "tcp:127.0.0.1:12001"
#vehicle = connect(connection_string, wait_ready=True)

#Direct connection through uart (from RPI). That is, no secure gateway.
connection_string = "/dev/ttyS0"
vehicle = connect("/dev/ttyS0", wait_ready=True, baud=115200)


def arm_and_takeoff(aTargetAltitude):
    """
    Arms vehicle and fly to aTargetAltitude.
    """

    print("Basic pre-arm checks")
    # Don't try to arm until autopilot is ready
    while not vehicle.is_armable:
        print(" Waiting for vehicle to initialise...")
        time.sleep(1)

    print("Arming motors")
    # Copter should arm in GUIDED mode
    vehicle.mode = VehicleMode("GUIDED")
    vehicle.armed = True

    # Confirm vehicle armed before attempting to take off
    while not vehicle.armed:
        print(" Waiting for arming...")
        time.sleep(1)

    print("Taking off!")
    vehicle.simple_takeoff(aTargetAltitude)  # Take off to target altitude

    # Wait until the vehicle reaches a safe height before processing the goto
    #  (otherwise the command after Vehicle.simple_takeoff will execute
    #   immediately).
    while True:
        print(" Altitude: ", vehicle.location.global_relative_frame.alt)
        # Break and return from function just below target altitude.
        if vehicle.location.global_relative_frame.alt >= aTargetAltitude * 0.95:
            print("Reached target altitude")
            break
        time.sleep(1)

vehicle.parameters['FENCE_ENABLE']=0
print("DISABLED GEOFENCE. Read FENCE_ENABLE=", vehicle.parameters['FENCE_ENABLE'])

arm_and_takeoff(10)

print("Set default/target airspeed to 3")
vehicle.airspeed = 3

print("Going towards first point ...")
point1 = LocationGlobalRelative(-35.35727701, 149.17063664, 20)
vehicle.simple_goto(point1)

# sleep so we can see the change in map
#time.sleep(60*60)
while True:
    lat = vehicle.location.global_relative_frame.lat
    lon = vehicle.location.global_relative_frame.lon
    alt = vehicle.location.global_relative_frame.alt

    print(lat, lon, alt)
    time.sleep(1)


#print("Going towards second point for 30 seconds (groundspeed set to 10 m/s) ...")
#point2 = LocationGlobalRelative(-35.363244, 149.168801, 20)
#vehicle.simple_goto(point2, groundspeed=10)

## sleep so we can see the change in map
#time.sleep(30)

#print("Returning to Launch")
#vehicle.mode = VehicleMode("RTL")

# Close vehicle object before exiting script
print("Close vehicle object")
vehicle.close()

