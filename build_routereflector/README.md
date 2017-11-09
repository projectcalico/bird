# Calico Docker BIRD Route Reflector image

## Introduction

This directory contains the build scripts to create a Calico BIRD Route 
Reflector image that can be used to set up a cluster of Route Reflectors with a
[Calico Docker][calico-docker] cluster of nodes.

The default target of the Makefile creates a Docker image called 
calico/routereflector, the Calico DockerHub repo creates a publically 
accessible image of the same name.  If you just wish to use the current Route 
Reflector image, then you will not need to build the image locally.

For more details, refer to the main [Calico documentation](http://docs.projectcalico.org/master/usage/routereflector/calico-routereflector).

### Route reflector cluster

When starting a cluster of route reflectors, the Calico BIRD Route Reflector
takes care of creating a full mesh between all of the route reflectors in the 
cluster. When adding a new Route Reflector instance, add an entry into etcd.
All Route Reflector instances watch for new Route Reflectors and update their
peerings accordingly.

### Route reflector peering with Calico Docker nodes

The Calico BIRD Route Reflector image hooks into the same etcd datastore used
by the Calico Docker nodes to determine which nodes to peer with.

The peering between the Calico Docker nodes and each Route Reflector is 
entirely configured on the Calico Docker nodes (using [calicoctl][calicoctl]),
and not through any additional Route Reflector configuration.

## Using the Route Reflector image (with etcd datastore)

### Starting up a Route Reflector

On your Route Reflector host, ensure you have [Docker v1.6][docker] or greater
installed.

Run the following command to start the Route Reflector container image.

```
docker run -privileged -net=host -d                                \ 
           -e IP=<IPv4_RR>                                         \
           -e ETCD_ENDPOINTS=<http://ETCD_IP:PORT>                 \
           [-e CLUSTER_ID=<CLUSTER_ID>]                            \
           calico/routereflector
```

Where:

-  `[]` indicates an optional parameter
-  `<IPv4_RR>` is the IPv4 address of the RR host (the BIRD instance binds to 
   the hosts IPv4 address)
-  `<ETCD_IP:PORT>` is the colon separated IPv4 address and port of an etcd
   node in the etcd cluster.
-  `CLUSTER_ID` is the optional cluster ID required when running multiple route
   reflectors as a cluster.

```

Repeat for every Route Reflector in the cluster.

### Route Reflector with TLS/SSL Etcd

If you are running secure etcd, you will need to pass in additional options
and set environment variables for the certificate and key files associated
with your etcd instance.

When starting the Route Reflector container image, you need to mount the
certificate files and environment variable filepaths for each file:
```
docker run --privileged --net=host -d                              \
           -e IP=<IPv4_RR>                                         \
           -e ETCD_ENDPOINTS=<https://ETCD_IP:PORT>                \
           -v <FULL_PATH_TO_CERT_DIR>:/certs                       \
           -e ETCD_CA_CERT_FILE=/certs/<CA_FILE>                   \
           -e ETCD_CERT_FILE=/certs/<CERT_FILE>                    \
           -e ETCD_KEY_FILE=/certs/<KEY_FILE>                      \
           [-e CLUSTER_ID=<CLUSTER_ID>]                            \
           calico/routereflector
```
Where `<FULL_PATH_TO_CERT_DIR>` is a directory on the host that contains
the certificate files (you can mount multiple directories with additional
`-v <DIR>` parameters if they are in separate directories, but be sure
to choose different `<MOUNT_DIR>` locations if this is the case).

## Using the Route Reflector image (with Kubernetes API datastore)

If you are using Kubernetes as the datastore for Calico, the routereflector
image does support this, but only for a single route reflector.  It is not
possible with this image to set up a cluster of route reflectors.

### Starting up the Route Reflector

On your Route Reflector host, ensure you have [Docker v1.6][docker] or greater
installed.

You will need a kubeconfig file that you need to mount into the route reflector
container.

Run the following command to start the Route Reflector container image.

```
docker run --privileged --net=host -d                              \
           -e DATASTORE_TYPE=kubernetes                            \
           -e KUBECONFIG=/kubeconfig                               \
           -e IP=<IPv4_RR>                                         \
           -v <KUBECONFIG_FILE_PATH>:/kubeconfig                   \
           calico/routereflector
```

Where:

-  `<IPv4_RR>` is the IPv4 address of the RR host (the BIRD instance binds to
   the hosts IPv4 address)
-  `<KUBECONFIG_FILE_PATH>` is the path to the kubeconfig file.

When using Kubernetes API as the datastore, this route reflector image only works
as a single standalone reflector.

