FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libssl-dev \
    libboost-system-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN git clone --depth 1 https://github.com/reo7sp/tgbot-cpp.git \
    && cmake -B /opt/tgbot-cpp/build -S /opt/tgbot-cpp -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /opt/tgbot-cpp/build -j"$(nproc)" \
    && cmake --install /opt/tgbot-cpp/build \
    && rm -rf /opt/tgbot-cpp

WORKDIR /app
COPY CMakeLists.txt .
COPY src/ ./src/

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"


FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3t64 \
    libboost-system1.83.0 \
    libcurl4t64 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/my_bot /app/my_bot

ENV DATA_DIR=/app/data
RUN mkdir -p /app/data

CMD ["/app/my_bot"]