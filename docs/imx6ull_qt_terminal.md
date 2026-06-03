# i.MX6ULL Qt Terminal Deployment

`vehicle_terminal` is the lightweight display/control endpoint for an i.MX6ULL Linux board. The existing `vehicle`
executable remains on the inference host, publishes detection telemetry over MQTT, and exposes annotated video over
HTTP MJPEG. The terminal does not link OpenCV or ONNX Runtime.

## Data Flow

```text
camera -> inference host: vehicle --stream-port -> HTTP MJPEG -> i.MX6ULL: vehicle_terminal -> LCD
                          \--mqtt -----------> MQTT broker -------------------------/
CAN bus -> i.MX6ULL: can0 -> vehicle_terminal -> LCD and MQTT terminal/can/state
```

The current inference executable publishes visual risk results. The terminal publishes its locally decoded CAN state
for another service or a later inference-side subscriber to consume; the existing inference executable does not yet
fold `terminal/can/state` back into its risk rules.

## Board Dependencies

The board root filesystem or cross-compilation sysroot must contain:

- Qt 5/6 Widgets and Network with an embedded platform plugin such as `linuxfb` or `eglfs`
- `libmosquitto` runtime and development files
- Linux SocketCAN support and a configured `can0` network device

No ONNX Runtime package is required on i.MX6ULL. When cross compiling, the bundled x86-64 ONNX Runtime in this
repository is intentionally excluded from dependency lookup.

## Cross Compile

This workspace includes a toolchain file for the relocated `100ask_imx6ull-sdk` under `/home/yjt`. If the SDK is
moved again, run its `relocate-sdk.sh` script and update `cmake/imx6ull-toolchain.cmake`.

```bash
cmake -S . -B build-imx6ull \
  -DCMAKE_TOOLCHAIN_FILE=cmake/imx6ull-toolchain.cmake \
  -DVEHICLE_BUILD_QT_TERMINAL=ON \
  -DVEHICLE_BUILD_TESTS=OFF
cmake --build build-imx6ull --target vehicle_terminal --parallel
```

Only `vehicle_terminal` needs to be deployed to the board together with Qt and `libmosquitto` runtime libraries.
If CMake cannot find `libmosquitto`, rebuild that Buildroot package and regenerate or update the SDK sysroot first.

## Run The Inference Host

Start an MQTT broker reachable from both devices, then run detection on the PC or edge compute board:

```bash
./build/vehicle --video 0 --model models/yolo11n.onnx --frames 0 \
  --stream-port 8080 --stream-width 640 --stream-quality 72 \
  --mqtt --mqtt-broker tcp://192.168.1.10:1883 \
  --mqtt-topic-prefix vehicle/vehicle-001
```
Allow TCP port `8080` through the inference host firewall if the board is on another network segment.

## Run The i.MX6ULL Terminal

Configure real CAN according to the board pinmux and transceiver. A typical Linux setup is:

```bash
ip link set can0 type can bitrate 500000
ip link set can0 up
export QT_QPA_PLATFORM=linuxfb
./vehicle_terminal --broker 192.168.1.10 --port 1883 \
  --topic-prefix vehicle/vehicle-001 --video-url http://192.168.1.10:8080/stream \
  --can-interface can0 --fullscreen
```

For a PC-side UI smoke test without a real CAN adapter, use `vcan0` as documented in the main README.

## MQTT Topics

| Topic suffix | Publisher | Terminal behavior |
| --- | --- | --- |
| `/frames` | Inference host | Shows frame count, detections, tracks and live FPS |
| `/events` | Inference host | Adds risk alerts to the event list |
| `/status` | Inference host | Shows final FPS and CPU status |
| `/can/state` | Inference host, when its CAN input is enabled | Used as fallback display state when local CAN is unavailable |
| `/terminal/can/state` | i.MX6ULL terminal | Publishes locally decoded `can0` state for upstream consumers |
