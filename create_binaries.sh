#!/bin/sh
set -e

[ ! $ARCH ] && echo ERROR: ARCH is not set. && exit 1
[ ! $DIST ] && echo ERROR: DIST is not set. && exit 1

[ -z "$TARGETARCH" ] && TARGETARCH=$ARCH

autoconf


for i in $TARGETARCH; do
	# where we save our output
	distarch=$DIST/$i
	mkdir -p $distarch
	# if target arch is our arch, then no --host=<triple>



	HOSTARCH=
	GCC=gcc
	# are we cross-compiling
	if [ "$i" != "$ARCH" ]; then
		HOSTARCH="$i-linux-gnu"
		GCC="$HOSTARCH-gcc"
	fi
	./configure  --with-protocols="bgp pipe static" --enable-ipv6=yes --enable-client=yes --enable-pthreads=yes --with-sysconfig=linux-v6 --build=$ARCH --host=$HOSTARCH
	make
	# Remove the dynamic binaries and rerun make to create static binaries and store off the results
	rm bird birdcl

	# we need to force static compilation
	make CC="$GCC --static"
	cp bird $distarch/bird6
	cp birdcl $distarch/birdcl


	# Rerun the build but without IPv6 (or the client) and store off the result.
	make clean
	./configure  --with-protocols="bgp pipe static" --enable-client=no --enable-pthreads=yes -with-sysconfig=linux --build=$ARCH --host=$HOSTARCH
	make
	rm bird
	make CC="$GCC --static"
	cp bird $distarch/bird

done
