# Secure_MAVLink_Gateway
Secure MAVLink Gateway

## Build

### Prerequisites

* [CMake](https://cmake.org/)
* [Python 3](https://www.python.org/)
* [GCC](https://gcc.gnu.org/)

### Instructions

```shell
mkdir build
cd build

cmake ..
```

Create MAVLink header files:
```shell
make mavlink_headers
```

Build the project:
```shell
make secure_gateway
```

## Run

```shell
./secure_gateway
```
