FROM ubuntu:24.04 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make libopencv-dev libcurl4-openssl-dev libmosquitto-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cmake -S . -B build -DVEHICLE_BUILD_TESTS=ON \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04 AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends libopencv-dev libcurl4 libmosquitto1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/vehicle /usr/local/bin/vehicle
COPY --from=build /app/third_party/onnxruntime-linux-x64-1.26.0/lib/libonnxruntime.so* /usr/local/lib/
COPY config ./config
COPY python ./python
COPY models ./models
RUN ldconfig

ENTRYPOINT ["vehicle"]
CMD ["--frames", "60", "--device", "cpu"]
