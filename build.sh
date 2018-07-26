#!/bin/bash
set -e
set -x

###############################################################################
# The build architecture is select by setting the ARCH variable.
# For example: When building on ppc64le you could use ARCH=ppc64le ./build.sh.
# When ARCH is undefined it defaults to amd64.
###############################################################################

# always are building on our current platform
BUILDARCH=$(uname -m)
# get our target machine architecture
ARCH=${ARCH:-${BUILDARCH}}

# standardize the name of the architecture on which we are building, as expected by gcc
case $BUILDARCH in
	amd64|x86_64)
		BUILDARCH=amd64
		;;
	arm64|aarch64)
		BUILDARCH=aarch64
		;;
	ppc64le|ppc64el|powerpc64le)
		BUILDARCH=ppc64le
		;;
	s390x)
	  	BUILDARCH=s390x
		;;
	*)
		echo "Unsupported build architecture $BUILDARCH" >&2
		exit 1
		;;
esac
	
# get the correct TARGETARCH
case $ARCH in
	all)
		TARGETARCH="amd64 aarch64 powerpc64le"
		;;
	amd64|x86_64)
		TARGETARCH=$ARCH
		;;
	arm64|aarch64)
		TARGETARCH=aarch64
		;;
	ppc64le|ppc64el|powerpc64le)
		TARGETARCH=powerpc64le
		;;
	s390x)
		TARGETARCH=s390x
		;;
	*)
		echo "Unknown target architecture $ARCH."
		exit 1
esac

# get the right dockerfile
DOCKERFILE=Dockerfile
IMAGE=birdbuild-$ARCH
if [ $BUILDARCH != $TARGETARCH ]; then
	DOCKERFILE=Dockerfile-cross
	IMAGE=birdbuild-$ARCH-cross
fi


DIST=dist/

docker build -t $IMAGE -f builder-images/$DOCKERFILE .
mkdir -p $DIST
# create_binaries expects:
#    ARCH = architecture on which I am running
#    TARGETARCH = architecture(s) for which I am building, if different than ARCH (blank if the same) 
#    DIST = root directory in which to put binaries, structured as $DIST/$TARGETARCH
docker run --rm --name bird-build -e ARCH=$BUILDARCH -e TARGETARCH="$TARGETARCH" -e DIST=$DIST -v `pwd`:/code $IMAGE ./create_binaries.sh
