#!/usr/bin/env python
from __future__ import print_function
import time
from dronekit import connect, VehicleMode, LocationGlobalRelative, Command

connection_string = "udpout:127.0.0.1:14660"   #through thinros_app_udp
#connection_string = "udp:127.0.0.1:14551"   #direction connection to SITL (no secure gateway)
#connection_string = "/dev/ttyS0"   #RPI
vehicle = connect(connection_string, wait_ready=True, timeout=100, rate=1, heartbeat_timeout=100)

vehicle.mode = VehicleMode("MANUAL")

#Enable geofence
vehicle.parameters['FENCE_ENABLE']=1

#Upload mission and takeoff
upload_mission('mission.txt')
vehicle.mode = VehicleMode("AUTO")
vehicle.armed   = True

print("-----------------------------")

for i in range(1,10):
    mode = vehicle.mode.name
    lat = vehicle.location.global_relative_frame.lat
    lon = vehicle.location.global_relative_frame.lon
    alt = vehicle.location.global_relative_frame.alt
    airspeed = vehicle.airspeed
    nextwaypoint=vehicle.commands.next
    print("[%s] lat=%f, lon=%f, alt=%f, airspeed=%f, next wp=%s" % (mode, lat, lon, alt, airspeed, nextwaypoint))
    time.sleep(1)

print("step1 done")

vehicle.close()



def readmission(aFileName):
    """
    Load a mission from a file into a list.

    This function is used by upload_mission().
    """
    print("Reading mission from file: %s\n" % aFileName)
    cmds = vehicle.commands
    missionlist=[]
    with open(aFileName) as f:
        for i, line in enumerate(f):
            if i==0:
                if not line.startswith('QGC WPL 110'):
                    raise Exception('File is not supported WP version')
            else:
                linearray=line.split('\t')
                ln_index=int(linearray[0])
                ln_currentwp=int(linearray[1])
                ln_frame=int(linearray[2])
                ln_command=int(linearray[3])
                ln_param1=float(linearray[4])
                ln_param2=float(linearray[5])
                ln_param3=float(linearray[6])
                ln_param4=float(linearray[7])
                ln_param5=float(linearray[8])
                ln_param6=float(linearray[9])
                ln_param7=float(linearray[10])
                ln_autocontinue=int(linearray[11].strip())
                cmd = Command( 0, 0, 0, ln_frame, ln_command, ln_currentwp, ln_autocontinue, ln_param1, ln_param2, ln_param3, ln_param4, ln_param5, ln_param6, ln_param7)
                missionlist.append(cmd)
    return missionlist


def upload_mission(aFileName):
        """
        Upload a mission from a file.
        """
        #Read mission from file
        missionlist = readmission(aFileName)

        print("\nUpload mission from a file: %s" % aFileName)
        #Clear existing mission from vehicle
        print(' Clear mission')
        cmds = vehicle.commands
        cmds.clear()
        #Add new mission to vehicle
        for command in missionlist:
            cmds.add(command)
        print(' Upload mission')
        vehicle.commands.upload()


