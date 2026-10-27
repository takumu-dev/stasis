# stasis-ci-newest, the image CI builds and tests the presets in at each compiler's newest release,
# and runs the sanitizers and the checks in. It's GCC's official image, Debian with GCC 16.2, with
# every tool that tools/pins.json pins installed under /opt by tools/install.cmake: LLVM's own
# release of Clang 23.1.4, with only clang, clang++, the resource directory with the sanitizer
# runtimes, and llvm-symbolizer for the sanitizers' reports; the newest CMake, Ninja and Doxygen;
# and wasi-sdk and wasm-tools. Plain gcc, g++, clang and clang++ are the newest releases.
#
# It's built from the repository's root and pushed by hand, and .circleci/config.yml names it by
# its digest, which the push prints:
#
#   docker build -f ci/newest/Dockerfile -t ghcr.io/takumu-dev/stasis-ci-newest:<yyyy-mm-dd> .
#   docker push ghcr.io/takumu-dev/stasis-ci-newest:<yyyy-mm-dd>
#
# It moves when GCC or Clang has a new major release, when a pin moves, or for a reason its commit
# gives.

ARG GCC=gcc:16.2.0@sha256:07280eb2e744f1b7a2d8d4a3894c089ea07266c109758afc388a634bd098e4eb

# GCC's image has no CMake to run tools/install.cmake with, so this stage runs it with Debian's, and
# only what it installs reaches the image.
FROM ${GCC} AS tools

RUN apt-get update \
    && apt-get install --yes --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/*

RUN --mount=type=bind,source=tools,target=/tmp/stasis-tools \
    cmake -DPREFIX=/opt -P /tmp/stasis-tools/install.cmake

FROM ${GCC}

LABEL org.opencontainers.image.source=https://github.com/takumu-dev/stasis
LABEL org.opencontainers.image.description="Stasis's CI at the compilers' newest releases: GCC 16, Clang 23, and every pinned tool"

COPY --from=tools /opt /opt

ENV WASI_SDK_PATH=/opt/wasi-sdk
ENV PATH=/opt/llvm/bin:/opt/cmake/bin:/opt/ninja:/opt/doxygen/bin:/opt/wasm-tools:$PATH

RUN --mount=type=bind,source=ci/check-image.sh,target=/tmp/check-image.sh \
    sh /tmp/check-image.sh \
        gcc=16.2.0 g++=16.2.0 cc=16.2.0 clang=llvm clang++=llvm llvm-symbolizer=llvm \
        cmake=cmake ctest=cmake ninja=ninja doxygen=doxygen wasm-tools=wasm-tools
