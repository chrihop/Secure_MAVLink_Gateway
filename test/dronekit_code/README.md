Make sure to change the `connection_string` in goto.py and fly_out_of_geofence.py depending on the test mode. 


In the SITL console, run
```
fence load path_to_fence.txt
```
where path_to_fence.txt is the path to the geofence file, fence.txt. If loaded successfully, you should be able to see a polygon-shape geofence on the map. 

In a terminal, run
```
./goto.py
```
This dronekit code will 1) enable geofence and then 2) fly the copter to a location within the geofence. 

Once the copter reaches the target position, run
```
./fly_out_of_geofence.py
```
This code will disable geofence and try to fly the copter out of the geofence region. When the secure gateway is enabled, this code will NOT fly the copter out of the geofence. 

