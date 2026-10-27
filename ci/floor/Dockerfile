# stasis-ci-floor, the image CI builds and tests the gcc and clang presets in at each compiler's
# floor. It's the SDK of Valve's Steam Runtime 3, sniper, as Valve ships it: GCC 10.3, Clang 11.0.1,
# CMake 3.25.1, Ninja 1.10.1 and glibc 2.31. So Stasis is built and tested in the Steam Runtime, not
# just assumed to work there. The image adds only what the tests build scripts with, the pinned
# wasi-sdk and wasm-tools, which tools/install.cmake installs under /opt.
#
# It's built from the repository's root and pushed by hand, and .circleci/config.yml names it by
# its digest, which the push prints:
#
#   docker build -f ci/floor/Dockerfile -t ghcr.io/takumu-dev/stasis-ci-floor:<yyyy-mm-dd> .
#   docker push ghcr.io/takumu-dev/stasis-ci-floor:<yyyy-mm-dd>
#
# It moves when a pin of its tools moves. Sniper moves only for a reason, which its commit gives.

FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:3.0.20261021.266025@sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e

LABEL org.opencontainers.image.source=https://github.com/takumu-dev/stasis
LABEL org.opencontainers.image.description="Stasis's CI at the compilers' floors: Steam Runtime 3's SDK, with wasi-sdk and wasm-tools"

RUN --mount=type=bind,source=tools,target=/tmp/stasis-tools \
    cmake -DPREFIX=/opt "-DTOOLS=wasi-sdk;wasm-tools" -P /tmp/stasis-tools/install.cmake

ENV WASI_SDK_PATH=/opt/wasi-sdk
ENV PATH=/opt/wasm-tools:$PATH

RUN --mount=type=bind,source=ci/check-image.sh,target=/tmp/check-image.sh \
    sh /tmp/check-image.sh \
        gcc=10.3.0 g++=10.3.0 cc=10.3.0 clang=11.0.1 clang++=11.0.1 \
        cmake=3.25.1 ctest=3.25.1 ninja=1.10.1 wasm-tools=wasm-tools
