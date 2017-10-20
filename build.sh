#!/bin/bash
###############################################################################
# The build architecture is select by setting the ARCH variable.
# For example: When building on ppc64le you could use ARCH=ppc64le ./build.sh.
# When ARCH is undefined it defaults to amd64.
###############################################################################
[ ! $ARCH ] &&  ARCH=amd64

if [ $ARCH = amd64 ]; then
	unset ARCHTAG
fi

if [ $ARCH = ppc64le ]; then
	ARCHTAG=-ppc64le
fi

DIST=dist/$ARCH

docker build -t birdbuild$ARCHTAG -f Dockerfile$ARCHTAG . 
mkdir -p $DIST
docker run --name bird-build -e ARCH=$ARCH -e DIST=$DIST -v `pwd`:/code birdbuild$ARCHTAG ./create_binaries.sh
docker rm -f bird-build || true
