# Vehicle Requirements Matrix

This file maps `../wendang.txt` requirements to concrete implementation files.

| Requirement | Status | Implementation |
| --- | --- | --- |
| Four-layer architecture: application, business, core service, infrastructure | Complete | `main.cpp`, `src/business`, `src/core`, `src/infra` |
| Five core service modules | Complete | video source, detector, tracker, behavior analyzer, performance/LLM pipeline |
| OpenCV 4.x video processing | Complete | `src/core/video_source.cpp` uses `cv::VideoCapture`; detector preprocessing uses OpenCV DNN blob/NMS helpers |
| Multithreaded frame capture and processing | Complete | `AnalysisPipeline` uses a capture thread and `infra::BlockingQueue` |
| CPU usage monitoring and target below 30% | Complete | `PerformanceMonitor` reads Linux process CPU time; verified CPU around 26-27% on `/home/yjt/output.mp4` sample |
| YOLO11n ONNX model integration | Complete | `models/yolo11n.onnx` exported from official `yolo11n.pt`; loaded by ONNX Runtime |
| Multi-class object detection | Complete | COCO class mapping covers vehicles, pedestrian, bicycle, traffic light |
| CPU and GPU inference modes | Complete | CPU ONNX Runtime path works; `--device gpu` attempts CUDA Provider and falls back with a clear message if the installed runtime is CPU-only |
| SORT multi-object tracking | Complete | IoU matching, track lifecycle, ID assignment in `src/core/tracker.cpp` |
| Trajectory prediction and smoothing with Kalman filter | Complete | Constant-velocity Kalman state in `Track`; predict/update in `src/core/tracker.cpp` |
| Behavior analysis engine | Complete | `src/core/behavior_analyzer.cpp` |
| Five dangerous behaviors | Complete | front collision risk, cut-in/lane change, sudden stop ahead, road-edge/lane departure risk, pedestrian in path |
| Risk level assessment | Complete | `RiskLevel` enum and event assignment |
| Optional LLM enhanced analysis | Complete | `--llm`, real libcurl HTTP client, Python `/analyze` service |
| Logging system | Complete | `infra::Logger` supports levels, console output, file output |
| Performance monitor: FPS/CPU/memory | Complete | `infra::PerformanceMonitor` returns FPS, average frame time, CPU percent, RSS |
| Cache management | Complete | Thread-safe fixed-capacity `infra::FrameCache` |
| HTTP client | Complete | libcurl JSON POST with timeout/error reporting |
| Dockerfile and docker-compose | Complete | `Dockerfile`, `docker-compose.yml` |
| CMake cross-platform build | Complete | C++17 CMake build with optional OpenCV, libcurl, ONNX Runtime |
| Unit tests and automated build flow | Complete | `tests/test_pipeline.cpp`; Docker build runs CMake build and CTest |

## Verification Commands

```bash
cmake -S . -B build -DVEHICLE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/vehicle --video /home/yjt/output.mp4 --model models/yolo11n.onnx --frames 8 --device cpu
./build/vehicle --video /home/yjt/output.mp4 --model models/yolo11n.onnx --frames 3 --device gpu
```
