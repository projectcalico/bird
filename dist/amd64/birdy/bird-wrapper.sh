#!/bin/sh

set -ex

ROUTER_ID=`ip -4 -o a | while read num intf inet addr; do
    case $intf in
	# Allow for "eth0", "ens5", "enp0s3" etc.; avoid "lo" and
	# "docker0".
	e*)
	    echo ${addr%/*}
	    break
	    ;;
    esac
done`

if [ -z "$ROUTER_ID" ]; then
    ROUTER_ID='127.0.0.1'
fi

# Update bird.conf and bird6.conf
sed -i "s/BIRD_ROUTERID/${ROUTER_ID}/g" /etc/bird.conf
sed -i "s/BIRD_ROUTERID/${ROUTER_ID}/g" /etc/bird6.conf

# Given one of the NIC-specific addresses for this node, output -
# following ssh-agent style - variable settings for this node's stable
# address and AS number, and for the ToR addresses to peer with.  For
# example:
#   DUAL_TOR_STABLE_ADDRESS=172.31.20.3
#   DUAL_TOR_PEERING_ADDRESS_1=172.31.21.100/24
#   DUAL_TOR_PEERING_ADDRESS_2=172.31.22.100/24
#   DUAL_TOR_AS_NUMBER=65002
get_dual_tor_details()
{
    # Calculate loopback address.  Do this by rounding the 3rd octet
    # of the router ID down to a multiple of 10.
    nic_address=$1
    ip1=`echo $nic_address | cut -d. -f1`
    ip2=`echo $nic_address | cut -d. -f2`
    ip3=`echo $nic_address | cut -d. -f3`
    ip4=`echo $nic_address | cut -d. -f4`
    rack_base=$((ip3 - (ip3 % 10)))
    echo "DUAL_TOR_STABLE_ADDRESS=${ip1}.${ip2}.${rack_base}.${ip4}"

    # Calculate the ToR addresses.
    echo "DUAL_TOR_PEERING_ADDRESS_1=${ip1}.${ip2}.$((rack_base + 1)).100/24"
    echo "DUAL_TOR_PEERING_ADDRESS_2=${ip1}.${ip2}.$((rack_base + 2)).100/24"

    # Calculate the AS number.
    echo "DUAL_TOR_AS_NUMBER=$((65000 + (rack_base / 10)))"
}

# Given an address and interface in the same subnet as a ToR address/prefix,
# update the address in the ways that we need for dual ToR operation, and ensure
# that we still have the routes that we'd expect through that interface.
try_update_nic_addr()
{
    addr=$1
    intf=$2
    tor_addr_prefix=$3
    change_to_scope_link=$4

    # Calculate the IP network of the subnet that $tor_addr_prefix is in.
    eval `ipcalc -n $tor_addr_prefix`
    subnet_network=$NETWORK

    # Calculate the IP network of the given address, assuming the same prefix
    # length as in $tor_addr_prefix.
    subnet_prefix_len=${tor_addr_prefix#*/}
    eval `ipcalc -n ${addr}/${subnet_prefix_len}`
    addr_network=$NETWORK

    # If the networks are the same...
    if [ "$addr_network" = "$subnet_network" ]; then

	if $change_to_scope_link; then
	    # Delete the given address and re-add it with scope link.
	    ip a d $addr dev $intf
	    ip a a $addr dev $intf scope link
	fi

	# Ensure that the subnet route is present.
	ip r a ${subnet_network}/${subnet_prefix_len} dev $intf || true

	# Try to add a default route via the ToR.  (This will fail if we already
	# have a default route, e.g. via the other ToR.)
	ip r a default via ${tor_addr_prefix%/*} || true

    fi
}

# If run with "dual-tor" argument, do setup for a dual ToR node.
echo Invoked with: "$@"
if [ "$1" = dual-tor ]; then

    echo "NIC-specific address is $ROUTER_ID"
    eval `get_dual_tor_details $ROUTER_ID`
    echo "Stable (loopback) address is $DUAL_TOR_STABLE_ADDRESS"
    echo "AS number is $DUAL_TOR_AS_NUMBER"
    echo "ToR addresses are $DUAL_TOR_PEERING_ADDRESS_1 and $DUAL_TOR_PEERING_ADDRESS_2"

    # Configure the stable address.
    ip a a ${DUAL_TOR_STABLE_ADDRESS}/32 dev lo

    # Generate BIRD peering config.
    cat >/etc/bird/peers.conf <<EOF
template bgp tors {
  description "Connection to ToR";
  local as $DUAL_TOR_AS_NUMBER;
  direct;
  gateway recursive;
  import all;
  export all;
  add paths on;
  connect delay time 2;
  connect retry time 5;
  error wait time 5,30;
  next hop self;
  bfd on;
}
protocol bgp tor1 from tors {
  neighbor ${DUAL_TOR_PEERING_ADDRESS_1%/*} as $DUAL_TOR_AS_NUMBER;
}
protocol bgp tor2 from tors {
  neighbor ${DUAL_TOR_PEERING_ADDRESS_2%/*} as $DUAL_TOR_AS_NUMBER;
}
EOF

    # Listen on port 8179 so as not to clash with calico-node.
    sed -i '/router id/a listen bgp port 8179;' /etc/bird.conf

    # Enable ECMP programming into kernel.
    sed -i '/protocol kernel {/a merge paths on;' /etc/bird.conf

    # Change interface-specific addresses to be scope link.
    ip -4 -o a | while read num intf inet addr rest; do
	case ${intf}_${addr} in
	    # Allow for "eth0", "ens5", "enp0s3" etc.; avoid "lo" and
	    # "docker0".
	    e*_* )
		try_update_nic_addr $addr $intf $DUAL_TOR_PEERING_ADDRESS_1 true
		try_update_nic_addr $addr $intf $DUAL_TOR_PEERING_ADDRESS_2 true
		;;
	esac
    done
fi

# Use multiple ECMP paths based on hashing 5-tuple.
sysctl -w net.ipv4.fib_multipath_hash_policy=1
sysctl -w net.ipv4.fib_multipath_use_neigh=1

# Ensure that /var/run/calico (which is where the control socket file
# will be) exists.
mkdir -p /var/run/calico

# Loop deciding whether to run early BIRD or not.
early_bird_running=false
while true; do
    if grep 00000000:00B3 /proc/net/tcp; then
	# Calico BIRD is running.
	if $early_bird_running; then
	    birdcl down
	    birdcl6 down
	    early_bird_running=false
	fi
    else
	# Calico BIRD is not running.
	if ! $early_bird_running; then
	    # Start bird and bird6 (which will both daemonize)
	    bird
	    bird6
	    early_bird_running=true
	fi
    fi
    # Ensure subnet routes are present.
    ip -4 -o a | while read num intf inet addr rest; do
	case ${intf}_${addr} in
	    # Allow for "eth0", "ens5", "enp0s3" etc.; avoid "lo" and
	    # "docker0".
	    e*_* )
		try_update_nic_addr $addr $intf $DUAL_TOR_PEERING_ADDRESS_1 false
		try_update_nic_addr $addr $intf $DUAL_TOR_PEERING_ADDRESS_2 false
		;;
	esac
    done
    sleep 10
done
