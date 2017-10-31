#!/bin/sh

[ ! $ARCH ] && echo ERROR: ARCH is not set. && exit 1
[ ! $DIST ] && echo ERROR: DIST is not set. && exit 1


autoconf
# Configure just the protocols we need, and enable the client and IPv6
./configure  --with-protocols="bgp pipe static" --enable-ipv6=yes --enable-client=yes --enable-pthreads=yes --with-sysconfig=linux-v6 --build=$ARCH
make

# Remove the dynmaic binaries and rerun make to create static binaries and store off the results
rm bird birdcl
make CC="gcc -static"
cp bird $DIST/bird6
cp birdcl $DIST/birdcl

# Rerun the build but without IPv6 (or the client) and store off the result.
make clean
./configure  --with-protocols="bgp pipe static" --enable-client=no --enable-pthreads=yes -with-sysconfig=linux --build=$ARCH
make
rm bird
make CC="gcc -static"
cp bird $DIST/bird

