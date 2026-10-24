FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc gcc-arm-linux-gnueabihf make bc flex bison libssl-dev libelf-dev xz-utils perl python3 ca-certificates file binutils-arm-linux-gnueabihf kmod cpio rsync && rm -rf /var/lib/apt/lists/*
