#!/bin/sh
set -e

[ ! $ARCH ] && echo ERROR: ARCH is not set. && exit 1
[ ! $DIST ] && echo ERROR: DIST is not set. && exit 1

[ -z "$TARGETARCH" ] && TARGETARCH=$ARCH

autoreconf


for i in $TARGETARCH; do
	# where we save our output
	dirarch=$i
	[ "$dirarch" = "x86_64" ] && dirarch=amd64
	[ "$dirarch" = "aarch64" ] && dirarch=arm64
	[ "$dirarch" = "ppc64el" ] && dirarch=ppc64le
	[ "$dirarch" = "powerpc64le" ] && dirarch=ppc64le
	[ "$dirarch" = "s390x" ] && dirarch=s390x

	# where we place our output binaries
	distarch=$DIST/$dirarch
	mkdir -p $distarch
	distarch=$(readlink -f $distarch)
	# by making our obj/ dir be distinct per arch, we avoid potentially trouncing with .o files of wrong arch
	objarch=$OBJ/$dirarch
	mkdir -p $objarch
	objarch=$(readlink -f $objarch)

	# initial pwd
	initpwd=$(pwd)

	(
	cd $objarch
	# if target arch is our arch, then no --host=<triple>
	HOSTARCH=
	GCC=gcc
	# are we cross-compiling
	if [ "$i" != "$ARCH" ]; then
		HOSTARCH="$i-linux-gnu"
		GCC="$HOSTARCH-gcc"
	fi
	$initpwd/configure  --with-protocols="bfd bgp pipe static" --enable-ipv6=yes --enable-client=yes --enable-pthreads=yes --with-sysconfig=linux-v6 --build=$ARCH --host=$HOSTARCH
	make
	# Remove the dynamic binaries and rerun make to create static binaries and store off the results
	rm bird birdcl

	# we need to force static compilation
	# because of the circular dependency when bird runs in its own directory (birdc -> ./birdc), it will try to rebuild birdc, which fails when static
	#    thus, always build explicit targets when doing --static
	make CC="$GCC --static" bird birdcl
	cp bird $distarch/bird6
	cp birdcl $distarch/birdcl6


	# Rerun the build but without IPv6 (or the client) and store off the result.
	make clean
	$initpwd/configure  --with-protocols="bfd bgp pipe static" --enable-client=no --enable-pthreads=yes -with-sysconfig=linux --build=$ARCH --host=$HOSTARCH
	make
	rm bird birdcl
	# because of the circular dependency when bird runs in its own directory (birdc -> ./birdc), it will try to rebuild birdc, which fails when static
	#    thus, always build explicit targets when doing --static
	make CC="$GCC --static" bird birdcl
	cp bird $distarch/bird
	cp birdcl $distarch/birdcl
	)

done
