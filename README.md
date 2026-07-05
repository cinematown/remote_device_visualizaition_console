# C++ Remote Device Visualization Console

Portfolio project for a Linux C++ remote device monitoring and visualization
system.

## Phase 0 - Environment Check

Goal:

- Create a minimal C++20/CMake project.
- Verify that the Ubuntu VM can configure, build, and run a small executable.

Files:

- `CMakeLists.txt`
- `src/main.cpp`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/rdvc_smoke
```

Expected output:

```text
RDVC Phase 0: C++20/CMake environment check
__cplusplus = 202002
```

Temporary compiler-only check if CMake is not installed yet:

```bash
mkdir -p build
g++ -std=c++20 -Wall -Wextra -Wpedantic src/main.cpp -o build/rdvc_smoke
./build/rdvc_smoke
```

Ubuntu package to install before the next CMake verification:

```bash
sudo apt-get update
sudo apt-get install -y cmake
```

## Phase 1 - Project Layout and Hello Targets

Goal:

- Create the first project layout for apps and libraries.
- Build separate hello-level executables for the server, simulator, and viewer.

Directories:

- `apps/server`
- `apps/simulator`
- `apps/viewer`
- `libs/common`
- `libs/protocol`
- `libs/net`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/rdvc_server
./build/rdvc_simulator
./build/rdvc_viewer
```

## Phase 2 - First TCP Status Line

Goal:

- Let the simulator connect to the server over TCP.
- Send one `STATUS` line from the simulator.
- Print the received line on the server.

Behavior:

- Server listens on `127.0.0.1:5000`.
- Simulator sends:

```text
STATUS device_id=sim-001 state=OK battery=87
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run in terminal 1:

```bash
./build/rdvc_server
```

Run in terminal 2:

```bash
./build/rdvc_simulator
```

Expected server output:

```text
RDVC server listening on 127.0.0.1:5000
Received: STATUS device_id=sim-001 state=OK battery=87
```

Expected simulator output:

```text
Sent: STATUS device_id=sim-001 state=OK battery=87
```

## Phase 3 - STATUS Parser and DeviceStatus

Goal:

- Move `STATUS` line parsing into `libs/protocol`.
- Introduce a reusable `DeviceStatus` structure.
- Let the server print parsed device fields after receiving a line.

Protocol format:

```text
STATUS device_id=<id> state=<state> battery=<percent>
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run in terminal 1:

```bash
./build/rdvc_server
```

Run in terminal 2:

```bash
./build/rdvc_simulator
```

Expected server output:

```text
RDVC server listening on 127.0.0.1:5000
Received: STATUS device_id=sim-001 state=OK battery=87
Parsed device status: id=sim-001 state=OK battery=87
```

## Phase 4 - Non-blocking epoll Server

Goal:

- Change the server from one blocking client to a non-blocking `epoll` loop.
- Accept multiple simulator connections.
- Parse each received `STATUS` line with `libs/protocol`.

Behavior:

- Server keeps running until `Ctrl+C`.
- Server listens on `127.0.0.1:5000`.
- Simulator accepts an optional device id argument.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run in terminal 1:

```bash
./build/rdvc_server
```

Run in terminal 2:

```bash
./build/rdvc_simulator sim-001
./build/rdvc_simulator sim-002
./build/rdvc_simulator sim-003
```

Expected server output includes:

```text
RDVC server listening on 127.0.0.1:5000
Client connected: fd=...
Received: STATUS device_id=sim-001 state=OK battery=87
Parsed device status: id=sim-001 state=OK battery=87
Client disconnected: fd=...
Received: STATUS device_id=sim-002 state=OK battery=87
Parsed device status: id=sim-002 state=OK battery=87
```

## Phase 5 - Device Status Store

Goal:

- Add an in-memory store for the latest status of each device.
- Update the store whenever the server receives a valid `STATUS` line.
- Keep parsing in `libs/protocol` and state storage in `libs/common`.

Implementation:

- `DeviceStatusStore` stores one latest `DeviceStatus` per `device_id`.
- Repeated reports from the same `device_id` replace the previous snapshot.
- New `device_id` values increase the stored device count.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run in terminal 1:

```bash
./build/rdvc_server
```

Run in terminal 2:

```bash
./build/rdvc_simulator sim-001
./build/rdvc_simulator sim-002
./build/rdvc_simulator sim-001
```

Expected server output includes:

```text
Parsed device status: id=sim-001 state=OK battery=87
Stored devices: 1
Parsed device status: id=sim-002 state=OK battery=87
Stored devices: 2
Parsed device status: id=sim-001 state=OK battery=87
Stored devices: 2
```

## Phase 6 - Minimal Qt Widgets Viewer

Goal:

- Replace the viewer hello executable with a minimal Qt Widgets GUI.
- Add a server connect/disconnect button.
- Add a `QTableView` prepared for device status rows.

Implementation:

- `rdvc_viewer` uses `QApplication`, `QTcpSocket`, `QPushButton`,
  `QLabel`, `QTableView`, and `QStandardItemModel`.
- The viewer connects to `127.0.0.1:5000`.
- Incoming newline-delimited `STATUS` lines are parsed with `libs/protocol`
  and displayed in the table.
- The current server stores device states but does not broadcast them to the
  viewer yet. Live table updates from server-published data are a later step.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in a VS Code SSH terminal:

```bash
./build/rdvc_server
```

Run viewer from the Ubuntu desktop session:

```bash
./build/rdvc_viewer
```

Optional simulator test:

```bash
./build/rdvc_simulator sim-001
./build/rdvc_simulator sim-002
```

Expected result:

- Viewer window opens.
- Pressing `Connect` changes the connection state.
- The table shows columns for `Device ID`, `State`, and `Battery`.

## Phase 7 - Qt Network Worker Thread

Goal:

- Move viewer socket handling out of the UI thread.
- Use a `QThread` and worker object for network events.
- Update the UI through Qt signal/slot connections.

Implementation:

- `NetworkWorker` owns `QTcpSocket` inside the worker thread.
- The UI asks the worker to connect/disconnect using queued calls.
- The worker parses complete newline-delimited `STATUS` lines and emits
  `DeviceStatus` objects to the UI.
- The UI updates `QTableView` rows only from the main thread.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in a VS Code SSH terminal:

```bash
./build/rdvc_server
```

Run viewer from the Ubuntu desktop session:

```bash
./build/rdvc_viewer
```

Expected result:

- Viewer window opens.
- Pressing `Connect` changes the state without putting socket work in the UI
  object.
- The current server still stores simulator status internally and does not
  broadcast status rows to connected viewers yet.

## Phase 8 - QOpenGLWidget Device Viewport

Goal:

- Add a `QOpenGLWidget` viewport to the Qt viewer.
- Extend `DeviceStatus` with `x`, `y`, and `z` coordinates.
- Draw simple coordinate axes and device markers.

Implementation:

- Simulator sends `x=... y=... z=...` in each `STATUS` line.
- Server parses, stores, logs, and broadcasts valid status lines to connected
  clients.
- Viewer receives status lines in the network worker, updates the table, and
  upserts device markers in the OpenGL viewport.
- The viewport uses a lightweight isometric projection. VTK and real model
  loading are intentionally left for later phases.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run viewer from the Ubuntu desktop session:

```bash
./build/rdvc_viewer
```

Run simulators in terminal 2:

```bash
./build/rdvc_simulator sim-001 10 0 0
./build/rdvc_simulator sim-002 0 12 4
./build/rdvc_simulator sim-003 -8 -6 10
```

Expected result:

- The table shows `Device ID`, `State`, `Battery`, `X`, `Y`, and `Z`.
- The OpenGL viewport shows coordinate axes and one marker per simulator.

## Phase 10 - MQTT Bridge

Goal:

- Publish device status updates to MQTT.
- Subscribe to a command topic shape for future device commands.
- Keep MQTT optional so the project still builds when Mosquitto development
  files are not installed.

Topics:

- Publish: `devices/{id}/status`
- Subscribe: `devices/+/commands/reset`

Implementation:

- `MqttBridge` wraps the Mosquitto C client with RAII-style cleanup.
- If `mosquitto.h` and `libmosquitto` are found, CMake builds the server with
  `RDVC_HAS_MQTT=1`.
- If Mosquitto is not installed, CMake builds the server with
  `RDVC_HAS_MQTT=0` and logs that MQTT is disabled.
- Published payloads use the same JSON shape as the REST device response.

Optional Ubuntu packages:

```bash
sudo apt-get update
sudo apt-get install -y libmosquitto-dev mosquitto mosquitto-clients
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run broker in terminal 1:

```bash
mosquitto -v
```

Subscribe in terminal 2:

```bash
mosquitto_sub -h 127.0.0.1 -t 'devices/+/status'
```

Run server in terminal 3:

```bash
./build/rdvc_server
```

Send simulator statuses in terminal 4:

```bash
./build/rdvc_simulator sim-001 10 0 0
./build/rdvc_simulator sim-002 0 12 4
```

Expected MQTT payload:

```json
{"device_id":"sim-001","state":"OK","battery":87,"x":10.000000,"y":0.000000,"z":0.000000}
```

Optional command-topic smoke test:

```bash
mosquitto_pub -h 127.0.0.1 -t devices/sim-001/commands/reset -m '{}'
```


## Phase 11 - ACK Protocol for Load Testing

Goal:

- Add an optional ACK path for latency measurement.
- Prepare the protocol for a high-concurrency load generator.
- Keep existing simulator/viewer behavior compatible.

Protocol extension:

```text
STATUS device_id=<id> state=<state> battery=<percent> x=<x> y=<y> z=<z> seq=<n> sent_ms=<ms> ack=1
```

Server response when `ack=1`:

```text
ACK device_id=<id> seq=<n> server_ms=<ms>
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run ACK smoke test in terminal 2:

```bash
./build/rdvc_simulator sim-001 10 0 0 --ack
```

Expected simulator output includes:

```text
Sent: STATUS device_id=sim-001 state=OK battery=87 x=10 y=0 z=0 seq=1 sent_ms=... ack=1
Received: ACK device_id=sim-001 seq=1 server_ms=...
```


## Phase 12 - Load Generator v1

Goal:

- Add a dedicated load generator executable.
- Open many non-blocking TCP connections from one process.
- Send one ACK-requesting `STATUS` message per connection.
- Report basic latency and throughput numbers.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run load generator in terminal 2:

```bash
./build/rdvc_loadgen --connections 100
```

Expected output shape:

```text
connections: 100
acked: 100
errors: 0
elapsed_ms: ...
throughput_ack_per_sec: ...
latency_ms_p50: ...
latency_ms_p95: ...
latency_ms_p99: ...
```

Notes:

- v1 measures one ACK round trip per connection.
- Rate-controlled, duration-based traffic is intentionally left for the next
  load generator phase.


## Phase 13 - Load Generator v2

Goal:

- Keep TCP connections open during the test.
- Send repeated ACK-requesting `STATUS` messages.
- Separate connection count, target message rate, and duration.
- Reduce server per-message logging for ACK load-test traffic.

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run load generator in terminal 2:

```bash
./build/rdvc_loadgen --connections 100 --rate 1000 --duration 10
```

Expected output shape:

```text
connections: 100
target_rate_per_sec: 1000
duration_sec: 10
sent: ...
acked: ...
errors: 0
elapsed_ms: ...
traffic_ms: ...
throughput_ack_per_sec: ...
latency_ms_p50: ...
latency_ms_p95: ...
latency_ms_p99: ...
```


## Phase 14 - Server Metrics

Goal:

- Report server-side health and throughput while loadgen is running.
- Keep client-observed latency percentiles in `rdvc_loadgen`.
- Prepare metrics as the data source for a future viewer dashboard.

Server metrics output:

```text
METRICS uptime_sec=10 active=100 accepted=100 disconnected=0 devices=100 received=9200 ack_sent=9200 parse_errors=0 broadcast_errors=0 msg_per_sec=997
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run load generator in terminal 2:

```bash
./build/rdvc_loadgen --connections 100 --rate 1000 --duration 10
```

Expected result:

- `rdvc_loadgen` prints client-side RTT latency p50/p95/p99.
- `rdvc_server` prints server-side connection and throughput metrics once per second.


## Phase 15 - Viewer Performance Dashboard

Goal:

- Stream server-side metrics to the Qt viewer.
- Show a compact dashboard above the device table.
- Keep load generator latency percentiles in `rdvc_loadgen`.

Protocol addition:

```text
HELLO role=viewer
```

Server metrics stream:

```text
METRICS uptime_sec=10 active=100 accepted=100 disconnected=0 devices=100 received=9200 ack_sent=9200 parse_errors=0 broadcast_errors=0 msg_per_sec=997
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run server in terminal 1:

```bash
./build/rdvc_server
```

Run viewer from the Ubuntu desktop session:

```bash
./build/rdvc_viewer
```

Run load generator in terminal 2:

```bash
./build/rdvc_loadgen --connections 100 --rate 1000 --duration 10
```

Expected result:

- The viewer dashboard updates `Active`, `Msg/s`, `Devices`, `Received`, `ACK`, and `Errors`.
- Load generator sockets receive ACK lines only; dashboard metrics are sent only to registered viewer clients.
