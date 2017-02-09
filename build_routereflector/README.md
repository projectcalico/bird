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
cluster. When adding a new Route Reflector instance, add an entry into etcd.
All Route Reflector instances watch for new Route Reflectors and update their
peerings accordingly.

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

#### Adding the Route Reflector into etcd

Add an entry in etcd for this Route Reflector.  This tells the Route Reflector
to participate in peering, and provides enough information to allow the Route
Reflector instances to automatically form a full BGP mesh.

The configuration for the Route Reflector is stored for IPv4 at:

	/calico/bgp/v1/rr_v4/<RR IPv4 address>
	
and IPv6 at:

	/calico/bgp/v1/rr_v6/<RR IPv6 address>

In all cases, the data is a JSON blob in the form:

        {
          "ip": "<IP address of BGP Peer>",
          "cluster_id": "<Cluster ID for this RR (see notes)>"
        }

To add this entry into etcd, you could use the following commands:
```
# IPv4 entries
curl -L http://<ETCD_IP:PORT>:2379/v2/keys/calico/bgp/v1/rr_v4/<IPv4_RR> -XPUT -d value="{\"ip\":\"<IPv4_RR>\",\"cluster_id\":\"<CLUSTER_ID>\"}"

# IPv6 entries
curl -L http://<ETCD_IP:PORT>:2379/v2/keys/calico/bgp/v1/rr_v6/<IPv6_RR> -XPUT -d value="{\"ip\":\"<IPv6_RR>\",\"cluster_id\":\"<CLUSTER_ID>\"}"
```
See [below](#topology-with-multiple-calico-bird-route-reflectors) for details 
about large networks and the use and format of the cluster ID.

Repeat the above instructions for every Route Reflector in the cluster.

### Route Reflector with TLS/SSL Etcd

If you are running secure etcd, you will need to pass in additional options
and set environment variables for the certificate and key files associated
with your etcd instance.

When starting the Route Reflector container image, you need to mount the
certificate files and environment variable filepaths for each file:
```
docker run -privileged -net=host -d                                \
           -e IP=<IPv4_RR>                                         \
           [-e IP6=<IPv6_RR>]                                      \
           -e ETCD_AUTHORITY=<ETCD_IP:PORT>                        \
           -v <FULL_PATH_TO_CERT_DIR>:<MOUNT_DIR>                  \
           -e ETCD_SCHEME=https                                    \
           -e ETCD_CA_CERT_FILE=<MOUNT_DIR>/<CA_FILE>              \
           -e ETCD_CERT_FILE=<MOUNT_DIR>/<CERT_FILE>               \
           -e ETCD_KEY_FILE=<MOUNT_DIR>/<KEY_FILE>                 \
           calico/routereflector
```
Where `<FULL_PATH_TO_CERT_DIR>` is a directory on the host that contains
the certificate files (you can mount multiple directories with additional
`-v <DIR>` parameters if they are in separate directories, but be sure
to choose different `<MOUNT_DIR>` locations if this is the case).

You will also need to pass the certificate and key files as parameters
in the curl statement when adding entries:
```
# IPv4 entries
curl --cacert <path_to_ca_cert> --cert <path_to_cert> --key <path_to_key> -L https://<ETCD_IP:PORT>:2379/v2/keys/calico/bgp/v1/rr_v4/<IPv4_RR> -XPUT -d value="{\"ip\":\"<IPv4_RR>\",\"cluster_id\":\"<CLUSTER_ID>\"}"
# IPv6 entries
curl --cacert <path_to_ca_cert> --cert <path_to_cert> --key <path_to_key> -L https://<ETCD_IP:PORT>:2379/v2/keys/calico/bgp/v1/rr_v6/<IPv6_RR> -XPUT -d value="{\"ip\":\"<IPv6_RR>\",\"cluster_id\":\"<CLUSTER_ID>\"}"
```

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
calicoctl bgp peer add <IP_RR> as <AS_NUM>
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
calicoctl node bgp peer add <IP_RR> as <AS_NUM>
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


## Topology with multiple Calico BIRD Route Reflectors

When the topology includes a cluster of Route Reflectors, BGP uses the concept
of a cluster ID to ensure there are no routing loops when distributing routes.

The Route Reflector image provided assumes that it has a fixed cluster ID for
each Route Reflector rather than being configurable on a per peer basis.  This 
simplifies the overall configuration of the network, but does place some 
limitations on the topology as described here.

The topology is based on the Top of Rack model where you would have a set of 
redundant route reflectors peering with all of the servers in the rack.

-  Each rack is assigned its own cluster ID (a unique number in IPv4 address
   format).
-  Each node (server in the rack) peers with a redundant set of route
   reflectors specific to that set rack.
-  All of the Route Reflectors across all racks form a full BGP mesh (this is
   handled automatically by the Calico BIRD Route Reflector image and does not
   require additional configuration).
   
![Example scale topology](mesh-topology.png)

For example, to set up the topology described above, you would:

-  Spin up nodes N1 - N9
-  Spin up Route Reflectors RR1 - RR6
-  Add [node specific peers](#configuring-a-node-specific-route-reflector-peering),
   peering:
  * N1, N2 and N3 with RR1 and RR2
  * N4, N5 and N6 with RR3 and RR4
  * N7, N8 and N9 with RR5 and RR6
-  Add [etcd config](#adding-the-route-reflector-into-etcd) for the Route 
   Reflectors:
  * RR1 and RR2 both using the cluster ID 1.0.0.1
  * RR2 and RR3 both using the cluster ID 1.0.0.2  
  * RR4 and RR5 both using the cluster ID 1.0.0.3

