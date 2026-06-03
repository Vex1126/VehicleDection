# VehicleDection

VehicleDection 是一个面向 Linux/嵌入式场景的车辆前方目标检测与行为风险分析系统。主程序从摄像头、视频文件或模拟帧读取图像，使用 YOLO11n ONNX 做目标检测，再结合多目标跟踪和规则分析输出前向碰撞、行人进入路径、前车急停、切入变道等风险事件。

项目采用四层结构：

- 应用层：`main.cpp`，负责命令行参数解析和程序入口。
- 业务逻辑层：`src/business`，负责检测、跟踪、行为分析、遥测发布等流水线编排。
- 核心服务层：`src/core`，包含 YOLO 检测适配器、SORT 风格跟踪、行为风险分析、可视化和视频源。
- 基础设施层：`src/infra`，包含日志、性能监控、缓存、HTTP、SocketCAN、MQTT 和 MJPEG 推流。

## 功能特性

- 支持 OpenCV 读取视频文件、USB 摄像头和 RTSP/设备输入。
- 支持 ONNX Runtime CPU 推理，仓库内置 Linux x64 CPU 版 ONNX Runtime。
- ONNX Runtime 不可用时可退回 OpenCV DNN；推理后端不可用时可使用模拟检测结果，便于演示和测试。
- 使用 SORT 风格 IoU 匹配进行多目标 ID 跟踪。
- 内置前向碰撞风险、行人进入路径、前车急停、切入/急变道、道路边缘/偏离风险等规则。
- 支持实时窗口显示检测框、轨迹、目标 ID、风险标签和 FPS。
- 支持保存标注视频、MJPEG HTTP 推流、MQTT 遥测发布和 Linux SocketCAN 车况输入。
- 提供 i.MX6ULL Qt 终端程序，用于板端显示视频流和风险状态。
- 提供 CTest 单元测试，覆盖跟踪、CAN 解码、JSON 事件、队列、缓存和基础流水线。

## 环境要求

推荐 Linux 环境：

- Ubuntu 20.04/22.04/24.04 或其他常见 Linux 发行版
- CMake 3.18 或更高版本
- GCC/G++ 支持 C++17
- OpenCV 4
- 可选：libcurl、libmosquitto、Qt5/Qt6、can-utils

Ubuntu 依赖安装示例：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libopencv-dev libcurl4-openssl-dev libmosquitto-dev can-utils
```

如果系统自带 CMake 低于 3.18，可以安装新版 CMake，例如：

```bash
sudo snap install cmake --classic
cmake --version
```

## 快速构建与测试

```bash
cmake -S . -B build-linux -DVEHICLE_BUILD_TESTS=ON
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

测试通过时会看到类似输出：

```text
100% tests passed, 0 tests failed out of 1
```

不接摄像头时，可以使用模拟帧源验证主程序：

```bash
./build-linux/vehicle --frames 60 --device cpu --print-detections
```

## 摄像头实时检测

打开默认摄像头 `/dev/video0`：

```bash
./build-linux/vehicle \
  --video 0 \
  --model models/yolo11n.onnx \
  --device cpu \
  --frames 0 \
  --display
```

说明：

- `--video 0` 表示打开默认摄像头；`--video 1` 表示第二个摄像头。
- `--frames 0` 表示一直处理，直到视频结束、摄像头断开，或用户退出。
- 实时窗口里按 `q` 或 `Esc` 退出。
- 如果在 SSH 或无桌面环境中运行，不要加 `--display`，可以用 `--output-video` 或 `--stream-port` 验证结果。

检查摄像头设备：

```bash
ls -l /dev/video*
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --all
```

如果没有摄像头权限，把当前用户加入 `video` 组后重新登录：

```bash
sudo usermod -aG video $USER
```

## 视频文件与标注输出

处理视频文件：

```bash
./build-linux/vehicle \
  --video test.mp4 \
  --model models/yolo11n.onnx \
  --device cpu \
  --frames 100 \
  --print-detections
```

保存带检测框和风险标签的标注视频：

```bash
./build-linux/vehicle \
  --video test.mp4 \
  --model models/yolo11n.onnx \
  --device cpu \
  --frames 100 \
  --output-video output_annotated.mp4
```

处理整个视频到文件结束：

```bash
./build-linux/vehicle \
  --video test.mp4 \
  --model models/yolo11n.onnx \
  --device cpu \
  --frames 0 \
  --output-video output_annotated.mp4
```

## ONNX Runtime 与模型

仓库内置 ONNX Runtime CPU 版：

```text
third_party/onnxruntime-linux-x64-1.26.0
```

正常配置时 CMake 会自动启用它。出现下面输出表示 ONNX Runtime 已启用：

```text
ONNX Runtime enabled: .../libonnxruntime.so
```

如果要使用其他 ONNX Runtime 版本：

```bash
export ONNXRUNTIME_ROOT=/opt/onnxruntime-linux-x64
cmake -S . -B build-linux -DVEHICLE_BUILD_TESTS=ON -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"
cmake --build build-linux --parallel
export LD_LIBRARY_PATH="$ONNXRUNTIME_ROOT/lib:$LD_LIBRARY_PATH"
```

如果 `models/yolo11n.onnx` 不存在，可以用 Ultralytics 重新导出：

```bash
python3 -m venv .venv
.venv/bin/pip install ultralytics onnx
curl -L -o models/yolo11n.pt https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n.pt
.venv/bin/yolo export model=models/yolo11n.pt format=onnx imgsz=640 opset=12 simplify=False
```

有效模型输出形状应为：

```text
output0: [1, 84, 8400]
```

`--device gpu` 会优先请求 ONNX Runtime CUDA Provider。如果当前 ONNX Runtime 包不含 CUDA Provider，程序会记录原因并回退 CPU。要使用真实 GPU 推理，需要把 `ONNXRUNTIME_ROOT` 指向 onnxruntime-gpu 发行包。

## 常用参数

| 参数 | 说明 |
| --- | --- |
| `--frames 120` | 处理固定帧数 |
| `--frames 0` | 一直处理到视频结束或用户退出 |
| `--video test.mp4` | 输入视频文件 |
| `--video 0` | 打开默认摄像头 |
| `--model models/yolo11n.onnx` | ONNX 模型路径 |
| `--device cpu|gpu` | 推理设备标识 |
| `--display` | 显示实时检测窗口 |
| `--output-video output_annotated.mp4` | 保存标注视频 |
| `--print-detections` | 逐帧打印检测数、跟踪数和事件数 |
| `--queue-size 8` | 采集线程到处理线程之间的队列容量 |
| `--cache-size 30` | 最近帧缓存容量 |
| `--log-level debug|info|warn|error` | 日志等级 |
| `--log-file logs/vehicle.log` | 同时写入日志文件 |
| `--llm` | 启用 LLM 增强分析 |
| `--llm-endpoint http://localhost:8000/analyze` | LLM 分析服务地址 |
| `--can` | 启用 SocketCAN 车况输入 |
| `--can-interface vcan0` | CAN 网卡名 |
| `--mqtt` | 启用 MQTT 遥测发布 |
| `--mqtt-broker tcp://127.0.0.1:1883` | MQTT broker 地址 |
| `--mqtt-topic-prefix vehicle/vehicle-001` | MQTT topic 前缀 |

## 实时显示问题排查

如果 `--output-video` 正常，但 `--display` 只显示一帧或立即退出，重点检查：

```bash
echo $DISPLAY
echo $XDG_SESSION_TYPE
```

桌面环境下通常应看到类似：

```text
:0
x11
```

如果摄像头只读取一帧，先不加 `--display` 测试连续处理：

```bash
./build-linux/vehicle \
  --video 0 \
  --model models/yolo11n.onnx \
  --device cpu \
  --frames 30 \
  --print-detections
```

如果可以处理满 30 帧，摄像头和模型都正常，问题在窗口显示环境。可以改用 `--output-video` 或 MJPEG 推流。当前版本已经避免 OpenCV 窗口第一帧可见性误判导致的提前退出。

## CAN 与 MQTT

启动本地 MQTT broker：

```bash
docker compose up mqtt-broker
```

订阅项目消息：

```bash
docker run -it --rm --network host eclipse-mosquitto:2 \
  mosquitto_sub -h 127.0.0.1 -p 1883 -t 'vehicle/#' -v
```

没有真实 CAN 设备时，可以使用 `vcan0`：

```bash
sudo modprobe can
sudo modprobe can_raw
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

发送测试 CAN 帧：

```bash
cansend vcan0 100#1388
cansend vcan0 101#FF9C
cansend vcan0 102#0132
cansend vcan0 103#0301
```

启用 CAN + MQTT：

```bash
./build-linux/vehicle --frames 60 --device cpu \
  --can --can-interface vcan0 \
  --mqtt --mqtt-broker tcp://127.0.0.1:1883
```

当前 CAN ID 约定：

- `0x100`：车速，前 2 字节大端无符号整数，比例 `0.01 km/h`
- `0x101`：方向盘角，前 2 字节大端有符号整数，比例 `0.1 deg`
- `0x102`：踏板，byte0 为刹车开关，byte1 为油门百分比
- `0x103`：档位/转向灯，byte0 为有符号档位，byte1 为转向灯 `0=none,1=left,2=right,3=hazard`

MQTT topic：

- `vehicle/vehicle-001/frames`：每帧摘要
- `vehicle/vehicle-001/events`：逐条风险事件
- `vehicle/vehicle-001/can/state`：CAN 解码后的车况状态
- `vehicle/vehicle-001/status`：运行结束时的性能摘要

## LLM 风险复核

项目提供 Python 服务 `python/llm_behavior_analyzer.py`，可对规则系统筛出的风险事件做 LLM 复核。

启动服务示例：

```bash
export ARK_API_KEY=你的_api_key
export ARK_API_BASE_URL=https://ark.cn-beijing.volces.com/api/v3/responses
export ARK_MODEL=doubao-seed-2-0-mini-260428
export ARK_TIMEOUT_SECONDS=60
python3 python/llm_behavior_analyzer.py serve 8000
```

主程序调用：

```bash
./build-linux/vehicle \
  --video test.mp4 \
  --model models/yolo11n.onnx \
  --frames 100 \
  --llm \
  --llm-endpoint http://127.0.0.1:8000/analyze \
  --http-timeout-ms 120000
```

## i.MX6ULL Qt 终端

项目包含轻量板端程序 `vehicle_terminal`。推荐部署方式：

```text
PC/边缘计算板：运行 vehicle，负责摄像头采集、YOLO 检测、风险分析、MQTT 发布和 MJPEG 标注视频输出。
i.MX6ULL：运行 vehicle_terminal，显示 MJPEG 视频流和风险状态，读取本地 can0，并通过 MQTT 发布车况。
```

在具备 Qt 与 libmosquitto 开发库的环境中构建终端：

```bash
cmake -S . -B build-terminal -DVEHICLE_BUILD_QT_TERMINAL=ON -DVEHICLE_BUILD_TESTS=OFF
cmake --build build-terminal --target vehicle_terminal --parallel
```

推理端运行：

```bash
./build-linux/vehicle --video 0 --model models/yolo11n.onnx --frames 0 \
  --stream-port 8080 --stream-width 640 --stream-quality 72 \
  --mqtt --mqtt-broker tcp://192.168.1.10:1883 \
  --mqtt-topic-prefix vehicle/vehicle-001
```

板端运行：

```bash
export QT_QPA_PLATFORM=linuxfb
./vehicle_terminal --broker 192.168.1.10 --topic-prefix vehicle/vehicle-001 \
  --video-url http://192.168.1.10:8080/stream \
  --can-interface can0 --fullscreen
```

更多交叉编译、CAN 初始化和主题约定见 `docs/imx6ull_qt_terminal.md`。

## 目录结构

```text
.
├── CMakeLists.txt
├── main.cpp
├── include/vehicle
├── src/business
├── src/core
├── src/infra
├── src/qt_terminal
├── tests
├── models
├── third_party
├── python
├── docker
└── docs
```

## 开发流程

常用开发命令：

```bash
cmake -S . -B build-linux -DVEHICLE_BUILD_TESTS=ON
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

同步到另一台机器时，不要同步构建目录：

```bash
rsync -av \
  --exclude 'build*/' \
  --exclude '.git/' \
  --exclude '.vscode/' \
  ./ yyy@192.168.88.131:/home/yyy/Vehicle/
```

目标机器重新编译：

```bash
cd ~/Vehicle
cmake -S . -B build-linux -DVEHICLE_BUILD_TESTS=ON
cmake --build build-linux --parallel
```
