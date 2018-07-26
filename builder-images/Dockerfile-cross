FROM gcc:latest
MAINTAINER Tom Denham <tom@projectcalico.org>

RUN dpkg --add-architecture arm64
RUN dpkg --add-architecture ppc64el
RUN dpkg --add-architecture s390x
RUN apt update
RUN apt install -y autoconf flex bison \
	libncurses-dev libreadline-dev \
	binutils-aarch64-linux-gnu gcc-aarch64-linux-gnu libncurses-dev:arm64 libreadline-dev:arm64 \
	binutils-powerpc64le-linux-gnu gcc-powerpc64le-linux-gnu libncurses-dev:ppc64el libreadline-dev:ppc64el \
	binutils-s390x-linux-gnu gcc-s390x-linux-gnu libncurses-dev:s390x libreadline-dev:s390x


WORKDIR /code
