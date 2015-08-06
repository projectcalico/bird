# Calico Docker BIRD Route Reflector image

## Introduction

This directory contains the build scripts to create a Calico BIRD Route 
Reflector image that can be used to set up a cluster of Route Reflectors with a
[Calico Docker][calico-docker] cluster of nodes.

The default target of the Makefile creates a Docker image called 
calico/routereflector, the Calico DockerHub repo creates a publically 
accessible image of the same name.  If you just wish to use the current Route 
Reflector image, then you will not need to build the image locally.

### Route reflector cluster

When starting a cluster of route reflectors, the Calico BIRD Route Reflector
takes care of creating a full mesh between all of the route reflectors in the 
cluster. The IP addresses for the full set of route reflectors is passed in as 
an environment variable parameter on the `docker run` command and therefore 
needs to be known in advance.  If you need to add a new Route Reflector to the 
cluster you will need to restart each Route Reflector that is already running, 
updating the environment variable parameter before restarting the instance.

### Route reflector peering with Calico Docker nodes

The Calico BIRD Route Reflector image hooks into the same etcd datastore used
by the Calico Docker nodes to determine which nodes to peer with.

The peering between the Calico Docker nodes and each Route Reflector is 
entirely configured on the Calico Docker nodes (using [calicoctl][calicoctl]),
and not through any additional Route Reflector configuration.

## Using the Route Reflector image

### Starting up a Route Reflector

On your Route Reflector host, ensure you have [Docker v1.6][docker] or greater
installed.

Run the following command to start the Route Reflector container image.

```
docker run -privileged -net=host -d                                \ 
           -e IP=<IPv4_RR>                                         \
           [-e IP6=<IPv6_RR>]                                      \
           -e ETCD_AUTHORITY=<ETCD_IP:PORT>                        \
           [-e RR_IP_ADDRS=<RR_IPv4_ADDRS>]                        \
           [-e RR_IP6_ADDRS=<RR_IPv6_ADDRS>]                       \
           calico/routereflector
```

Where:

-  `[]` indicates an optional parameter
-  `<IPv4_RR>` is the IPv4 address of the RR host (the BIRD instance binds to 
   the hosts IPv4 address)
-  `<IPv6_RR>` is the *optional* IPv6 address of the RR host (the BIRD6 instance
   binds to the hosts IPv6 address)
-  `<ETCD_IP:PORT>` is the colon separated IPv4 address and port of an etcd
   node in the etcd cluster.
-  `<RR_IP_ADDRS>` is a comma delimited set of IPv4 addresses of all of the
   Route Reflectors in the cluster.  You can include the Route Reflectors own
   IPv4 address in this list (but it will be ignored) - this means you can use
   the same list for all Route Reflectors in the cluster.  This may be omitted
   if you only have a single Route Reflector.
-  `<RR_IP6_ADDRS>` is a comma delimited set of IPv6 addresses of all of the
   Route Reflectors in the cluster.  You can include the Route Reflectors own
   IPv6 address in this list (but it will be ignored) - this means you can use
   the same list for all Route Reflectors in the cluster.  This may be omitted
   if you are not using IPv6, or you only have a single Route Reflector.

Repeat the above instructions for every Route Reflector in the cluster.


## Global Calico Docker configuration

Run through this section  to set up the global Calico Docker configuration
before configuring any nodes.  This only needs to be done once.

-  Disable the full node-to-node BGP mesh
-  Configure the default node AS number for your network (this is used by
   the Route Reflector image when setting up the Route Reflector full mesh).
   
If you have a small cluster of Route Reflectors and you intend to have every
Calico Docker node peer with every Route Reflector, set this up one time as
global configuration.


### Turn off the full node-to-node mesh

From any Calico Docker node, run the following:

    calicoctl bgp node-mesh off

You may need to set the ETCD_AUTHORITY environment variable to run the 
calicoctl commands.


### Set the required AS number for the network

From any Calico Docker node, run the following:

    calicoctl bgp default-node-as
    
This returns the value of the current default node AS number.  If you need to 
change the value used in your network run the command again, this time passing
in the new AS number value.  For example, to set it to 65123:

    calicoctl bgp default-node-as 65123


### Peering with every Route Reflector (optional)

If you have a small cluster of Route Reflectors (e.g. a single RR or a pair of
RRs for redundancy) and you intend to have every Calico Docker node peer with 
each of the Route Reflectors, you can set up the peerings as a one-time set of
global configuration.

From any Calico Docker node, run the following for each Route Reflector:

```
calicoctl bgppeer add <IP_RR> as <AS_NUM>
``` 

Where:
-  `<IP_RR>` is the IPv4 or IPv6 address of the Route Reflector.
-  `<AS_NUM>` is the AS number to use for the network (set or determined 
   above).


## Setting up node-specific peering

If you are deploying a cluster of Route Reflectors, with each Calico node
peering to a subset of Route Reflectors it will be necessary to set up the
peerings on a node-by-node basis.

This would be the typical situation when scaling out to a very large size.  For
example, you may have:

-  a cluster of 100 route reflectors connected in a full mesh
-  a network of 100,000 Calico Docker nodes
-  each Calico Docker node is connected to two or three different Route 
   Reflectors.

### Configuring a node-specific Route Reflector peering
  
To configure a Route Reflector as a peer of a specific node, run the following
*from the node*:

```
calicoctl node bgppeer add <IP_RR> as <AS_NUM>
``` 

Where:
-  `<IP_RR>` is the IPv4 or IPv6 address of the Route Reflector.
-  `<AS_NUM>` is the AS number to use for the network (set or determined 
   above).


Run this separately for each Route Reflector that you want to peer with the
node.
   
[calico-docker]: http://github.com/projectcalico/calico-docker
[calicoctl]: https://github.com/projectcalico/calico-docker#how-does-it-work
[docker]: http://www.docker.com
