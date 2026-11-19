FROM debian:13
LABEL maintainer="maintainers@tigera.io"

# The same base and compiler as Dockerfile-cross, so that a native build and a
# cross build give the same kind of binary: static, linked against glibc.
RUN apt-get update && \
    apt-get --no-install-recommends install -y \
	autoconf bison flex gcc libc6-dev m4 make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /code
