[![CircleCI branch](https://img.shields.io/circleci/project/projectcalico/bird/feature-ipinip.svg?label=bird)](https://circleci.com/gh/projectcalico/bird/tree/feature-ipinip)

[![Slack Status](https://slack.projectcalico.org/badge.svg)](https://slack.projectcalico.org)
[![IRC Channel](https://img.shields.io/badge/irc-%23calico-blue.svg)](https://kiwiirc.com/client/irc.freenode.net/#calico)

# Calico BIRD

This is a fork of the [BIRD internet routing daemon](BIRD-README) which provides
the following additional function used by Calico:

-  Support for routing using IP-in-IP

## Build

### TL;DR

To build `bird` on your platform, run `./build.sh`. To build for all platforms, run `ARCH=all ./build.sh`.

### Details
`bird` can be built for one platform - your local one, by default - or for all supported platforms.

When you run the build script - `build.sh` - it tries to determine what platforms you want to build it for by looking at the environment variable `ARCH`.

* If `ARCH` is unset, it will determine it from the local platform using `uname -m`.
* If `ARCH` is set to a supported platform, it will assume you are running on that platform and build for it.
* If `ARCH` is set to `all`, it will try to build for *all* supported architectures.

As of this writing, the supported architectures are as follows. Multiple entries on each line are aliases:

* `amd64` / `x86_64`
* `arm64` / `aarch64`
* `ppc64le` / `ppc64el` / `powerpc64le`

### How the Build Works
The build works as follows.

If you are building natively, it builds an alpine-linux-based build image from `Dockerfile`. It then runs a container bind-mounting the source directory as `/code`, builds the binaries statically linked, and saves them to `./dist/<arch>/`.

If you are cross-building, it builds a debian-linux-based build image from `Dockerfile-all`. It then runs a container bind-mounting the source directory `/code`, builds the binaries statically linked for each architecture, and saves themt to `./dist/<arch>/`.

In the list of architectures above, the first name in the alias as the one used for `dist/<arch>`, no matter what architecture you entered. Thus if you use `ARCH=x86_64 ./build.sh`, the binaries will be under `dist/amd64/`. If you use `ARCH=all`, it will create for each.
