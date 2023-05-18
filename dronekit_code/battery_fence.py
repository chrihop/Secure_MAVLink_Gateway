#!/usr/bin/env python
from __future__ import print_function
import getopt, sys
import time
import math
from dronekit import connect, VehicleMode, LocationGlobal, LocationGlobalRelative, Command

connection_string = "udp:127.0.0.1:14551"

#Connect to vehicle
vehicle = connect(connection_string, wait_ready=True)
vehicle.parameters['FENCE_ENABLE']=0


#From https://dronekit-python.readthedocs.io/en/latest/examples/guided-set-speed-yaw-demo.html
def arm_and_takeoff(aTargetAltitude):
    """
    Arms vehicle and fly to aTargetAltitude.
    """

    print("Basic pre-arm checks")
    # Don't let the user try to arm until autopilot is ready
    while not vehicle.is_armable:
        print(" Waiting for vehicle to initialise...")
        time.sleep(1)

        
    print("Arming motors")
    # Copter should arm in GUIDED mode
    vehicle.mode = VehicleMode("GUIDED")
    vehicle.armed = True

    while not vehicle.armed:      
        print(" Waiting for arming...")
        time.sleep(1)

    print("Taking off!")
    vehicle.simple_takeoff(aTargetAltitude) # Take off to target altitude

    # Wait until the vehicle reaches a safe height before processing the goto (otherwise the command 
    #  after Vehicle.simple_takeoff will execute immediately).
    while True:
        print(" Altitude: ", vehicle.location.global_relative_frame.alt)      
        if vehicle.location.global_relative_frame.alt>=aTargetAltitude*0.95: #Trigger just below target alt.
            print("Reached target altitude")
            break
        time.sleep(1)

#From https://dronekit-python.readthedocs.io/en/latest/examples/guided-set-speed-yaw-demo.html
def get_location_metres(original_location, dNorth, dEast):
    """
    Returns a LocationGlobal object containing the latitude/longitude `dNorth` and `dEast` metres from the 
    specified `original_location`. The returned LocationGlobal has the same `alt` value
    as `original_location`.

    The function is useful when you want to move the vehicle around specifying locations relative to 
    the current vehicle position.

    The algorithm is relatively accurate over small distances (10m within 1km) except close to the poles.

    For more information see:
    http://gis.stackexchange.com/questions/2951/algorithm-for-offsetting-a-latitude-longitude-by-some-amount-of-meters
    """
    earth_radius = 6378137.0 #Radius of "spherical" earth
    #Coordinate offsets in radians
    dLat = dNorth/earth_radius
    dLon = dEast/(earth_radius*math.cos(math.pi*original_location.lat/180))

    #New position in decimal degrees
    newlat = original_location.lat + (dLat * 180/math.pi)
    newlon = original_location.lon + (dLon * 180/math.pi)
    if type(original_location) is LocationGlobal:
        targetlocation=LocationGlobal(newlat, newlon,original_location.alt)
    elif type(original_location) is LocationGlobalRelative:
        targetlocation=LocationGlobalRelative(newlat, newlon,original_location.alt)
    else:
        raise Exception("Invalid Location object passed")
        
    return targetlocation;

#From https://dronekit-python.readthedocs.io/en/latest/examples/guided-set-speed-yaw-demo.html
def get_distance_metres(aLocation1, aLocation2):
    """
    Returns the ground distance in metres between two LocationGlobal objects.

    This method is an approximation, and will not be accurate over large distances and close to the 
    earth's poles. It comes from the ArduPilot test code: 
    https://github.com/diydrones/ardupilot/blob/master/Tools/autotest/common.py
    """
    dlat = aLocation2.lat - aLocation1.lat
    dlong = aLocation2.lon - aLocation1.lon
    return math.sqrt((dlat*dlat) + (dlong*dlong)) * 1.113195e5







latest_battery_status = None
@vehicle.on_message('BATTERY_STATUS')
def callback_battery_status(self, name, msg):
    global latest_battery_status
    latest_battery_status = msg


while not vehicle.home_location:
    cmds = vehicle.commands
    cmds.download()
    cmds.wait_ready()
    if not vehicle.home_location:
        print(" Waiting for home location ...")


while not latest_battery_status:
    print(" Waiting for battery status message")
    time.sleep(0.5)


battery_capacity_mAh = vehicle.parameters['BATT_CAPACITY']  # not really needed unless calculating remaining level manually

distance_traveled = 0
initial_remaining_level = latest_battery_status.battery_remaining
avg_battery_consumption = 0

BATTERY_BUFFER = 5  #percent

# Take off
altitude = 15
arm_and_takeoff(altitude)
vehicle.groundspeed=10

# Fly far away
dNorth = 10000 # meters
dEast = 0 # meters
currentLocation = vehicle.location.global_relative_frame
targetLocation = get_location_metres(currentLocation, dNorth, dEast)
targetDistance = get_distance_metres(currentLocation, targetLocation)
vehicle.simple_goto(targetLocation) #FLY!


prev_location = vehicle.location.global_relative_frame

# Monitor
while vehicle.mode.name=="GUIDED" or vehicle.mode.name=="RTL":

    current_location = vehicle.location.global_relative_frame
    home_location = vehicle.home_location

    delta_distance = get_distance_metres(current_location, prev_location) 
    distance_traveled = distance_traveled + delta_distance


    distance_to_home = get_distance_metres(current_location, home_location) 
    distance_to_target = get_distance_metres(current_location, targetLocation)
    
    print("-------------")
    print("delta_distance = %f m, distance_traveled = %f m, distance to home = %f m" % (delta_distance, distance_traveled, distance_to_home))
    
    if latest_battery_status:

        voltage = latest_battery_status.voltages[0]   # mV
        current_battery = latest_battery_status.current_battery   # cA
        current_consumed = latest_battery_status.current_consumed # mAh
        energy_consumed = latest_battery_status.energy_consumed   
        battery_remaining = latest_battery_status.battery_remaining
        
        #NOTE: There is no reason to use Wh/meter. mAh/meter can be used or just the remaining level
        #NOTE: Also we don't need to calculate the remaining capacity manually. Let's just use battery_remaining field
        # wh = current_consumed*0.001 * voltage*0.001        
        # print('%f Ah, %f V --> %f Wh' % (current_consumed*0.001, voltage*0.001, wh))
        # remaining_capacity = 1 - current_consumed/battery_capacity_mAh        
        # print('remaining_capacity (%): ', remaining_capacity)

        print("battery_status.battery_remaining: %.0f%% (after buffer: %.0f%%)" % (battery_remaining, battery_remaining-BATTERY_BUFFER))
        print('  capacity: %f mAh, consumed: %f mAh' % (battery_capacity_mAh, current_consumed))

        battery_remaining -= BATTERY_BUFFER # give some buffer

        if battery_remaining<0:
            battery_remaining = 0
        
        battery_consumed = initial_remaining_level - battery_remaining
        if battery_consumed>0 and distance_traveled>0:
            avg_battery_consumption = distance_traveled/battery_consumed
            print('avg_battery_consumption: %f meters/%%' % avg_battery_consumption)
            
            remaining_range = battery_remaining * avg_battery_consumption
            print('remaining range:  %f meters' % remaining_range)

            print('distance_to_home: %f meters' % distance_to_home)

            print('current speed: %f m/s, altitude: %f m' % (vehicle.groundspeed, current_location.alt))

            if vehicle.mode.name=="RTL":
                print("!!! RETURNING !!!")
                if vehicle.armed == False:
                    break
            else:
                if remaining_range < distance_to_home:    
                    print("!!!NEED TO RETURN TO HOME!!!")                
                    vehicle.mode = VehicleMode("RTL")

    prev_location = current_location

    time.sleep(0.5)


print("disarmed")
vehicle.close()
