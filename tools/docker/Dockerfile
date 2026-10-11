# Build environment for ff9-ds: BlocksDS (arm-none-eabi-gcc, libnds) plus Python for the build tools.
# Build/run via tools/docker.sh.
FROM skylyrac/blocksds:slim-v1.24.0

RUN apt-get update && apt-get install -y --no-install-recommends python3 \
    && rm -rf /var/lib/apt/lists/*

ENV PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:$PATH

WORKDIR /ff9ds
