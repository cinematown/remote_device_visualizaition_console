# Project Handoff

## Current Direction

`C++ Remote Device Visualization Console` is now focused on remote device observability:

- epoll-based C++ telemetry server
- device simulator
- high-concurrency load generator
- Qt Widgets viewer
- QPainter-based fleet map
- server metrics dashboard
- optional MQTT bridge

The project direction changed from a generic device visualization roadmap to a fleet observability console. VTK is no longer the main next step. It remains an optional future extension for industrial 3D asset/model viewing.

## Important Decisions

- TLS/OpenSSL transport is delayed.
- REST API Phase 9 was removed by user request.
- MQTT bridge was preserved.
- VTK is optional, not the primary visualization path.
- Main focus is now:
  - load generation
  - server throughput/connection metrics
  - Qt performance dashboard
  - fleet map / observability UI
  - possible replay/time-machine mode later
- Client-observed latency percentiles belong in `rdvc_loadgen`.
- Server metrics should focus on server-side facts: active connections, throughput, ACK count, parse errors, stored devices.

## Current Components

### server

Path: `apps/server/main.cpp`

Current responsibilities:

- TCP listen on `127.0.0.1:5000`
- non-blocking sockets
- epoll event loop
- accepts simulator, viewer, and loadgen connections
- parses `STATUS` lines through `libs/protocol`
- stores latest device state in `DeviceStatusStore`
- sends `ACK` for `ack=1` messages
- broadcasts `STATUS` lines, including loadgen `ack=1` lines, to registered viewer clients
- streams `METRICS` lines to registered viewer clients
- publishes status to MQTT when built with `libmosquitto`

Viewer clients register by sending:

```text
HELLO role=viewer
```

### simulator

Path: `apps/simulator/main.cpp`

Single-device smoke test client.

Example:

```bash
./build/rdvc_simulator sim-001 10 0 0
./build/rdvc_simulator sim-001 10 0 0 --ack
```

### loadgen

Path: `apps/loadgen/main.cpp`

Current load generator version:

- non-blocking TCP clients
- epoll-based
- supports repeated traffic over persistent connections
- measures client-observed ACK RTT latency
- prints p50/p95/p99 latency

Example:

```bash
./build/rdvc_loadgen --connections 100 --rate 1000 --duration 10
```

Metrics produced by loadgen:

- connections
- target_rate_per_sec
- duration_sec
- sent
- acked
- errors
- elapsed_ms
- traffic_ms
- throughput_ack_per_sec
- latency_ms_p50
- latency_ms_p95
- latency_ms_p99

### viewer

Paths:

- `apps/viewer/main.cpp`
- `apps/viewer/network_worker.hpp`
- `apps/viewer/network_worker.cpp`
- `apps/viewer/fleet_map_widget.hpp`
- `apps/viewer/fleet_map_widget.cpp`
- `apps/viewer/metrics_chart_widget.hpp`
- `apps/viewer/metrics_chart_widget.cpp`

Current viewer features:

- Qt Widgets app
- `QThread` + `NetworkWorker` for socket work
- sends `HELLO role=viewer` after connecting
- parses `STATUS` lines into device table
- batches high-rate `STATUS` UI updates every 100ms so loadgen traffic does not swamp the UI thread
- displays device markers in a 2D `FleetMapWidget`
- supports fleet map auto-fit, mouse wheel zoom, drag pan, hover, and selection
- fades stale devices and pulses recently updated devices
- parses `METRICS` lines through `libs/protocol`
- updates dashboard labels
- draws a labeled time-series chart for `msg_per_sec` and `active`
- marks table rows as `STALE` when no status update arrives for more than 5 seconds

Dashboard fields:

- Active
- Msg/s
- Devices
- Received
- ACK
- Errors

Table fields:

- Device ID
- State (`STALE` after 5 seconds without a new `STATUS`)
- Battery
- X
- Y
- Z
- Age

### protocol

Paths:

- `libs/protocol/include/rdvc/protocol/device_status.hpp`
- `libs/protocol/include/rdvc/protocol/server_metrics.hpp`
- `libs/protocol/include/rdvc/protocol/status_parser.hpp`
- `libs/protocol/src/server_metrics.cpp`
- `libs/protocol/src/status_parser.cpp`

Current `STATUS` format:

```text
STATUS device_id=<id> state=<state> battery=<percent> x=<x> y=<y> z=<z> seq=<n> sent_ms=<ms> ack=1
```

Fields `seq`, `sent_ms`, and `ack` are optional and mainly used by loadgen.

Server ACK response:

```text
ACK device_id=<id> seq=<n> server_ms=<ms>
```

Server metrics stream:

```text
METRICS uptime_sec=<s> active=<n> accepted=<n> disconnected=<n> devices=<n> received=<n> ack_sent=<n> parse_errors=<n> broadcast_errors=<n> msg_per_sec=<n>
```

`libs/protocol` now owns the wire-level `ServerMetrics` structure plus
`format_metrics_line()` and `parse_metrics_line()`. The server keeps its
internal cumulative counters separately as `ServerCounters` in
`apps/server/main.cpp`.

### common

Paths:

- `libs/common/include/rdvc/common/device_status_store.hpp`
- `libs/common/src/device_status_store.cpp`

Current responsibility:

- store latest `DeviceStatus` per `device_id`
- lookup by `device_id`
- report store size

REST-only `all()` was removed when REST Phase 9 was reverted.

### MQTT

Paths:

- `apps/server/mqtt_bridge.hpp`
- `apps/server/mqtt_bridge.cpp`

Current behavior:

- optional compile depending on `libmosquitto`
- CMake defines `RDVC_HAS_MQTT=1` when available
- publishes status to:

```text
devices/{id}/status
```

- subscribes to:

```text
devices/+/commands/reset
```

Current MQTT command handling is a placeholder log only.

## Phase History Summary

Completed major phases:

- Phase 0: C++20/CMake environment
- Phase 1: project layout and hello targets
- Phase 2: first TCP STATUS line
- Phase 3: protocol parser and `DeviceStatus`
- Phase 4: non-blocking epoll server
- Phase 5: `DeviceStatusStore`
- Phase 6: minimal Qt Widgets viewer
- Phase 7: Qt network worker thread
- Phase 8: OpenGL viewport with x/y/z device markers
- Phase 9: REST API was added, then removed by user request
- Phase 10: optional MQTT bridge
- Phase 11: ACK protocol for load testing
- Phase 12: load generator v1
- Phase 13: load generator v2 with `--rate` and `--duration`
- Phase 14: server metrics stdout reporting
- Phase 15: viewer performance dashboard
- Phase 16: viewer time-series metrics chart and shared metrics parser
- Phase 17: QPainter fleet map for loadgen-backed real TCP connection fleets
- Phase 17.1: viewer responsiveness pass with batched UI updates, clearer chart labels, and stale table rows

## Current Build Commands

```bash
cd /srv/samba/cpp/remote_device_visualizaition_console
cmake -S . -B build
cmake --build build
```

If Qt cannot be found after moving paths, use the local Qt prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/gcc_64
cmake --build build
```

## Current Run Commands

Server:

```bash
./build/rdvc_server
```

Viewer:

```bash
./build/rdvc_viewer
```

Simulator:

```bash
./build/rdvc_simulator sim-001 10 0 0
./build/rdvc_simulator sim-001 10 0 0 --ack
```

Load generator:

```bash
./build/rdvc_loadgen --connections 100 --rate 1000 --duration 10
```

MQTT optional setup:

```bash
sudo apt-get update
sudo apt-get install -y libmosquitto-dev mosquitto mosquitto-clients
```

Broker/subscriber examples:

```bash
mosquitto -v
mosquitto_sub -h 127.0.0.1 -t 'devices/+/status'
```

## Next Suggested Phase

Recommended next step: add detailed loadgen/network observability before replay.

Possible Phase 18 options:

1. Loadgen metrics stream
   - periodically publish loadgen-side throughput and latency percentiles
   - include p50/p95/p99 ACK RTT, ack/sec, errors, and connection count
   - server should forward this to registered viewers without mixing it into server-side `METRICS`
   - viewer can add a second chart for latency percentiles

2. Replay/time-machine foundation
   - persist STATUS/METRICS stream to log
   - replay later in viewer

3. Server main.cpp decomposition
   - split connection bookkeeping from the event loop
   - keep `ServerCounters` internal to the server
   - preserve `libs/protocol::ServerMetrics` as the wire snapshot type

4. Fleet map polish
   - add a visible auto-fit control
   - link table selection and map selection both ways
   - show selected device detail fields beside the map

Best immediate next phase:

```text
Phase 18 - Loadgen Metrics Stream
```

Reason: the current viewer shows server-side facts such as active connections
and `msg_per_sec`. Client-observed latency percentiles still only exist in
`rdvc_loadgen`, so detailed network observability needs a loadgen metrics
protocol before replay work.

## Notes for Next Context

- Before editing, read this file first.
- Keep changes small and phase-based.
- Do not reintroduce REST unless explicitly requested.
- Do not make VTK the next main phase unless the user changes direction.
- Preserve loadgen/server metric role separation:
  - loadgen owns RTT p50/p95/p99
  - server owns active/throughput/error/store metrics
- Continue adding concise comments around new implementation blocks explaining what/why.
