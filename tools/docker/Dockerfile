# Build environment for ff9-ds: BlocksDS (arm-none-eabi-gcc, libnds), Python for the build tools, and a 32-bit
# host gcc.
# Build/run via tools/docker.sh.
FROM skylyrac/blocksds:slim-v1.24.0

# gcc-multilib + gdb: the 32-bit Linux host build (make pc) used for fast iteration and lockstep tests.
RUN apt-get update && apt-get install -y --no-install-recommends python3 gcc-multilib gdb binutils \
    && rm -rf /var/lib/apt/lists/*

ENV PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:$PATH

WORKDIR /ff9ds
