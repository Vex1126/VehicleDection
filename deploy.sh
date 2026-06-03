#!/usr/bin/env bash
set -euo pipefail  # 出错即停止

cd /home/yjt/Vehicle

# 拉取最新代码
git pull

# 编译和测试
cmake -S . -B build -DVEHICLE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# 构建并启动 Docker
docker compose build
docker compose up -d --remove-orphans

# 清理无用镜像
docker image prune -f
