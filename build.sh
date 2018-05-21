#!/bin/bash
set -e
set -x

###############################################################################
# The build architecture is select by setting the ARCH variable.
# For example: When building on ppc64le you could use ARCH=ppc64le ./build.sh.
# When ARCH is undefined it defaults to amd64.
###############################################################################

# get our machine architecture
[ ! $ARCH ] &&  ARCH=$(uname -m)

# convert to desired name format
case $ARCH in
	all)
		ARCH=all
		BUILDARCH=amd64
		TARGETARCH="amd64 aarch64 powerpc64le"
		DOCKERFILE=Dockerfile-all
		;;
	amd64|x86_64)
		ARCH=amd64
		BUILDARCH=amd64
		TARGETARCH=
		DOCKERFILE=Dockerfile
		;;
	arm64|aarch64)
		ARCH=arm64
		BUILDARCH=aarch64
		TARGETARCH=
		DOCKERFILE=Dockerfile
		;;
	ppc64le|ppc64el|powerpc64le)
		ARCH=ppc64le
		BUILDARCH=ppc64le
		TARGETARCH=
		DOCKERFILE=Dockerfile
		;;
	s390x)
	  	ARCH=s390x
	  	BUILDARCH=s390x
		TARGETARCH=
		DOCKERFILE=Dockerfile-s390x
		;;
	*)
		echo "Unknown architecture $ARCH."
		exit 1
esac

DIST=dist/
IMAGE=birdbuild-$ARCH

docker build -t $IMAGE -f $DOCKERFILE .
mkdir -p $DIST
docker run --rm --name bird-build -e ARCH=$BUILDARCH -e TARGETARCH="$TARGETARCH" -e DIST=$DIST -v `pwd`:/code $IMAGE ./create_binaries.sh
