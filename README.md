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

## Test

### Case 1 - reject all MEMINFO messages

```mermaid
flowchart LR;
  src1([Replay/UDP :12002])
  src2([Replay/UDP :12022])
  pipeline[[Secure Gateway]]
  sink0([Discard/Log])
  sink1([Header/TCP :12011])
  src1 --> pipeline
  src2 --> pipeline
  pipeline --> sink1
  subgraph Gateway Internal
    direction TB
    pipeline --> sink0
  end
```


```shell
# build secure_gateway
# in terminal 1
./secure_gateway

# in terminal 2, connect VMC header
cd test
./mavlink_msg_header.py --header tcp --port 12011

# in terminal 3, connect legacy replay
# note: `-n -1` means replay forever
cd test
./mavlink_msg_replay.py --adapter udp --udp 12002 -n -1

# in terminal 4, connect CEE replay
cd test
./mavlink_msg_replay.py --adapter udp --udp 12022 -n -1
```

