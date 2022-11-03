# ArduPilot SITL Simulator (Software in the Loop)
(See more detail: https://ardupilot.org/dev/docs/sitl-simulator-software-in-the-loop.html)

![Alt text](SITL_setup.png)

Desktop runs ArduPilot Executable + Physics Simulator (e.g., FlighGear)

RPI4 ius a companion computer (communicaing with the ArduPilot executable using mavlink protocol)

In addition to the manual parsing/composing MAVLink messages, there are several libraries that help facilitate communication with a (simulated) drone from RPI4. Examples (both are installed in the RPI4@301):
- DroneKit:
https://dronekit-python.readthedocs.io/en/latest/develop/installation.html
- MavProxy:
https://ardupilot.org/mavproxy/docs/getting_started/download_and_installation.html#mavproxy-downloadinstalllinux

--------

RPI4 IP address: 172.28.228.145 (vspells/flint1234)

Desktop IP address: 172.28.228.229 (vspells/flint1234)


## How to run SITL
Also see this tutorial: https://ardupilot.org/dev/docs/copter-sitl-mavproxy-tutorial.html

#### Desktop
1) (If you are using remote desktop) VNC into the desktop
2) Open a terminal and run 
  ```
  ~/ardupilot/Tools/autotest/fg_quad_view.sh
  ```
3) (Optional) Open a new terminal and run QGroundControl (or a GCS program of your choice):
  ```
  cd ~/
  ./QGroundControl.AppImage
  ```
4) Open a new terminal and run :
  ```
  cd ~/ardupilot/ArduCopter
  ../Tools/autotest/sim_vehicle.py --map --console -A -—serial1=uart:/dev/ttyUSB0:115200
  ```
  You do not need `-A -—serial1=uart:/dev/ttyUSB0:115200` option if not using RPI4 as a companion computer.
 
5) Create a textfile with name `fence.txt` and the following data:
  ```
  -35.363796	149.166306
  -35.358871	149.170227
  -35.358871	149.170166
  -35.364075	149.172638
  -35.367920	149.170853
  -35.368717	149.163315
  -35.363327	149.159988
  -35.359200	149.162003
  -35.358963	149.170166
  -35.358963	149.170166
  -35.358871	149.170227
  ```
6) In the SITL console that is launched in Step 4), load the geofence file, `fence.txt`.
  ```
  fence load fence.txt
  ```

#### RPI4 (NEW -- using DroneKit)

1) Install DroneKit on your PI: See https://dronekit-python.readthedocs.io/en/latest/examples/index.html
2) Run our DroneKit code (from https://github.com/chrihop/Secure_MAVLink_Gateway/tree/main/test/dronekit_code)
  - *Important!* Make sure to change the connection string in the dronekit codes.

#### RPI4 (OLD)
1) Open a terminal and run
  ```
  mavproxy.py --master=/dev/ttyAMA0
  ```
  Then you will see outputs like this:
  ```
  Connect /dev/ttyAMA0 source_system=255
  Log Directory: 
  Telemetry log: mav.tlog
  Waiting for heartbeat from /dev/ttyAMA0
   MAV> Detected vehicle 1:1 on link 0
  online system 1
  STABILIZE> Mode STABILIZE
  AP: ArduCopter V4.3.0-dev (05bda895)
  AP: d0fc95ac4f1948f688d1dc19a008ee72
  AP: Frame: QUAD/PLUS
  Received 1287 parameters (ftp)
  Saved 1287 parameters to mav.parm
  fence present
  Flight battery 100 percent
  ```

2) Try these commands:
  ```
  mode guided
  arm throttle
  takeoff 40
  ```
  See the behavior of the drone from flightgear running on the desktop.
  
3) Also try:
  ```
  mode circle
  param set circle_radius 20000
  ```
  The drone should be circling around (radius: 20000 cm = 200 m).
  
4) Try:
  ```
  RTL
  ```
  or
  ```
  LAND
  ```
  

#### DroneKit example code

You can contol a drone (from RPI4 over mavlink) programmatically using DroneKit. See the examples: https://dronekit-python.readthedocs.io/en/latest/examples/index.html


#### mavros

(for test only) In the desktop, 
```
roslaunch mavros apm.launch
```
and in a new terminal, execute
```
rostopic echo /mavros/imu/data
```
Note: Make sure that `fcu_url` is set to `udp://@127.0.0.1:14551` in `/opt/ros/melodic/share/mavros/launch/apm.launch`. When using the companion computer (i.e., RPI4 over UART), set it to the right serial port name. 
