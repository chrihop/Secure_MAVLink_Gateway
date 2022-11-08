You will use two python files: demo_step1_fly_plane.py and demo_step2_wp_change_and_disable_fence.py.

Make sure to change the `connection_string` in these python codes according to your connection type. 

0. Copy the geofence file into ArduPlane directory
```
cp fence.txt ~/ardupilot/ArduPlane
```

1. Run the SITL
```
cd ~/ardupilot/ArduPlane
../Tools/autotest/sim_vehicle.py
```
You can run the SITL with `--map --console` option to see the map. 

2. In the SITL console, run
```
fence load fence.txt
```
If loaded successfully, you should be able to see a polygon-shape geofence on the map. 

3. Step 1: normal fly mission
```
./demo_step1_fly_plane.py
```
This code will enable the geofence, load the mission (from mission.txt), and take off the plane. 

4. Step 2: disable the geofence and modify the mission
```
./demo_step2_wp_change_and_disable_fence.py
```
This code will disable the geofence and modify the last waypoint which is outside of the geofence. If disabling the geofence is blocked successfully by the secure gateway, you will see 'Fence Breached' message in the SITL console, and the plane will return to home and land. 


