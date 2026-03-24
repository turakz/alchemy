FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    curl \
    git \
    lsb-release \
    clang-14 \
    clang-format-14 \
    clang-tidy-14 \
    llvm-14 \
    llvm-14-dev \
    libclang-14-dev \
    libclang-common-14-dev \
    libfmt-dev \
    libcapture-tiny-perl \
    libdatetime-perl \
    libtimedate-perl \
  && rm -rf /var/lib/apt/lists/*

RUN mkdir -m755 -p /usr/local/share/ca-certificates/zscaler/ && \
  curl \
    -L http://artifactory.wireless.msasafety.com:80/artifactory/tools/zscaler/ZscalerRootCertificate-2048-SHA256.crt \
    -o /usr/local/share/ca-certificates/zscaler/ZscalerRootCertificate-2048-SHA256.crt \
    && \
  chmod 0644 /usr/local/share/ca-certificates/zscaler/ZscalerRootCertificate-2048-SHA256.crt && \
  update-ca-certificates

# lcov 2.x required for --ignore-errors inconsistent (Ubuntu 22.04 apt ships 1.14)
RUN curl -L https://github.com/linux-test-project/lcov/releases/download/v2.1/lcov-2.1.tar.gz \
      -o /tmp/lcov.tar.gz && \
    tar -xzf /tmp/lcov.tar.gz -C /tmp && \
    make -C /tmp/lcov-2.1 install && \
    rm -rf /tmp/lcov.tar.gz /tmp/lcov-2.1

# llvm-gcov.sh calls unversioned llvm-cov; llvm-14 provides llvm-cov-14
RUN ln -sf /usr/bin/llvm-cov-14 /usr/local/bin/llvm-cov
