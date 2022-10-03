# Secure MAVLink Gateway

```mermaid
flowchart LR;
  src1([Legacy OS])
  src2([CEE-Enclave])
  src3([VM-Enclave])
  msg1(MAVLink Msg.)
  sp[(Security Policies)]
  pipeline[[Pipeline Routetable]]
  pipeline2[[Pipeline]]
  sink1([AutoPilot])
  sink2([Log])
  subgraph Source
    direction LR
    src1 --> pipeline
    src2 --> pipeline
    src3 --> pipeline
  end
  pipeline === msg1
  msg1 === pipeline2
  subgraph Sink
    direction LR
    pipeline2 --> sink1
    pipeline2 --> sink2
  end
  subgraph Verifier
    direction TB
    msg1 -.- sp
  end
```

## Build

### Prerequisites

* [CMake](https://cmake.org/)
* [Python 3](https://www.python.org/)
* [GCC](https://gcc.gnu.org/)

### Instructions

1. Pull the repositories
```shell
git submodule update --init --recursive
```

2. Create a build directory
```shell
mkdir build
cd build

cmake ..
```

3. Create MAVLink header files:
```shell
make mavlink_headers
```

4. Build the project:
```shell
make secure_gateway
```

## Run

```shell
./secure_gateway
```
