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

# If run with "dual-tor" argument, do setup for a dual ToR node.
echo Invoked with: "$@"
if [ "$1" = dual-tor ]; then

    # Provision loopback address.  Do this by rounding the 3rd octet
    # of the router ID down to a multiple of 10.
    echo "ROUTER_ID is $ROUTER_ID"
    ip1=`echo $ROUTER_ID | cut -d. -f1`
    ip2=`echo $ROUTER_ID | cut -d. -f2`
    ip3=`echo $ROUTER_ID | cut -d. -f3`
    ip4=`echo $ROUTER_ID | cut -d. -f4`
    rack_base=$((ip3 - (ip3 % 10)))
    loop_addr=${ip1}.${ip2}.${rack_base}.${ip4}
    echo "Stable (loopback) address is $loop_addr"
    ip a a ${loop_addr}/32 dev lo

    # Calculate the ToR addresses.
    tor1_addr=${ip1}.${ip2}.$((rack_base + 1)).100
    tor2_addr=${ip1}.${ip2}.$((rack_base + 2)).100
    echo "ToR addresses are $tor1_addr and $tor2_addr"

    # Calculate the AS number.
    as_num=$((65000 + (rack_base / 10)))
    echo "AS number is $as_num"

    # Generate BIRD peering config.
    cat >/etc/bird/peers.conf <<EOF
filter calico_dual_tor {
  if ( net ~ [ 192.168.0.0/16+] ) then accept;
  if ( net ~ [ 172.31.0.0/16+] ) then accept;
  accept;
}
template bgp tors {
  description "Connection to ToR";
  local as $as_num;
  direct;
  gateway recursive;
  import filter calico_dual_tor;
  export filter calico_dual_tor;
  add paths on;
  connect delay time 2;
  connect retry time 5;
  error wait time 5,30;
  next hop self;
  bfd on;
}
protocol bgp tor1 from tors {
  neighbor $tor1_addr as $as_num;
}
protocol bgp tor2 from tors {
  neighbor $tor2_addr as $as_num;
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
	    e*_172.31.* )
		ip a d $addr dev $intf
		ip a a $addr dev $intf scope link
		ip r a ${addr%.*}.0/24 dev $intf || true
		ip r a default via ${addr%.*}.100 || true
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
	    e*_172.31.* )
		ip r a ${addr%.*}.0/24 dev $intf || true
		;;
	esac
    done
    sleep 10
done
