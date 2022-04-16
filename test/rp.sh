#!/bin/sh
# Verify Rendez-vous Point functionality incl. SPT switchover
#
#               eth1  eth2    eth4
#         10.0.1/24    10.0.12/24                ROLES
#     ED1 --------- R1 ---------- R2             R2 :: Rendez-vous Point
#     eth0       eth3 \          / eth5          R3 :: Last Hop Router
#                      \        / 
#            10.0.13/24 \      / 10.0.23/24  
#                        \    /   
#	             eth6 \  / eth7
#                          R3 --------- ED2
#                             10.0.3/24
#                            eth8

# pimd debug, when enabled pimctl calls at runtime are also enabled
DEBUG="-l debug -d mrt,rpf"
#DEBUG="-l debug -d all"

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

# Requires OSPF (bird) to build the unicast rpf tree
print "Check deps ..."
check_dep ethtool
check_dep tshark
check_dep bird

print "Creating PIM configs, let R2 become RP and R1 BSR ..."
cat <<EOF > "/tmp/$NM/conf1"
bsr-candidate priority 12 interval 5
rp-candidate priority  30 interval 5

spt-threshold packets 5 interval 5
EOF
cat <<EOF > "/tmp/$NM/conf2"
bsr-candidate priority 1  interval 5
rp-candidate priority  20 interval 5

spt-threshold packets 5 interval 5
EOF
cat <<EOF > "/tmp/$NM/conf3"
spt-threshold packets 5 interval 5
EOF
cat "/tmp/$NM/conf1"

print "Creating world ..."
R1="/tmp/$NM/R1"
R2="/tmp/$NM/R2"
R3="/tmp/$NM/R3"
ED1="/tmp/$NM/ED1"
ED2="/tmp/$NM/ED2"
touch "$R1" "$R2" "$R3" "$ED1" "$ED2"

echo "$R1"   > "/tmp/$NM/mounts"
echo "$R2"  >> "/tmp/$NM/mounts"
echo "$R3"  >> "/tmp/$NM/mounts"
echo "$ED1" >> "/tmp/$NM/mounts"
echo "$ED2" >> "/tmp/$NM/mounts"

unshare --net="$R1" -- ip link set lo up
unshare --net="$R2" -- ip link set lo up
unshare --net="$R3" -- ip link set lo up
unshare --net="$ED1" -- ip link set lo up
unshare --net="$ED2" -- ip link set lo up


# Creates a VETH pair, one end named eth0 and the other is eth7:
#
#     created /tmp/foo eth0 eth7 1.2.3.4/24 1.2.3.1
#
# Disabling UDP checksum offloading with ethtool, frames are leaving
# kernel space on these VETH pairs.  (Silence noisy ethtool output)
created()
{
    in=$2
    if echo "$3" | grep -q '@'; then
	ut=$(echo "$3" | cut -f1 -d@)
	id=$(echo "$3" | cut -f2 -d@)
    else
	ut=$3
    fi

    echo "Creating device interfaces $in and $ut ..."
    nsenter --net="$1" -- ip link add "$in" type veth peer "$ut"
    ifsetup "$1" "$in" "$ut"

    nsenter --net="$1" -- ip addr add "$4" broadcast + dev "$2"
    nsenter --net="$1" -- ip route add default via "$5"

    if [ -n "$id" ]; then
	echo "$1 moving $ut to netns PID $id"
	nsenter --net="$1" -- ip link set "$ut" netns "$id"
    fi

    return $!
}

# Syntax:
#     a:b
#     a@pid:b
#     a:b@pid
#
# Unsupported:
#     a@pid:b@pid
create_vpair()
{
    ns=$1
    pair=$2
    addr=$3

    # a:b with possible @ to denote that either side should move @ pid netns
    if echo "$pair" |grep -q ':'; then
	echo "$ns: veth pair $pair ============================"
	x=$(echo "$pair" | cut -f1 -d:)
	y=$(echo "$pair" | cut -f2 -d:)
	echo "Found x=$x and y=$y ..."

	if echo "$x" | grep -q '@'; then
	    a=$(echo "$x" | cut -f1 -d@)
	    p=$(echo "$x" | cut -f2 -d@)
	    b=$y
	elif echo "$y" | grep -q '@'; then
	    a=$(echo "$y" | cut -f1 -d@)
	    p=$(echo "$y" | cut -f2 -d@)
	    b=$x
	fi

	echo "   Found a=$a and p=$p, with b=$b ..."

	echo "   creating interfaces $a and $b ..."
	nsenter --net="$ns" -- ip link add "$a" type veth peer "$b"
	ifsetup "$ns" "$a" "$a"

	echo "   moving $a to netns PID $p"
	nsenter --net="$ns" -- ip link set "$a" netns "$p"
    else
	# Not a pair, an after-the-fact set-address on an interface
	b=$pair
    fi

    echo "   Bringing up $b with addr $addr"
    nsenter --net="$ns" -- ip link set "$b" up
    nsenter --net="$ns" -- ip addr add "$addr" broadcast + dev "$b"
}

creater()
{
    create_vpair $1 $2 $4
    create_vpair $1 $3 $5
}

### Creating nodes
# ED1 eth0
# R1  eth1, eth2, eth3
# R2  eth4, eth5
# R3  eth6, eth7, eth8
# ED2 eth0

nsenter --net="$ED1" -- sleep 5 &
pid0=$!
dprint "$ED1 @$pid0"

nsenter --net="$ED2" -- sleep 5 &
pid1=$!
dprint "$ED2 @$pid1"

nsenter --net="$R2" -- sleep 5 &
pid2=$!
dprint "$R2 @$pid2"

nsenter --net="$R3" -- sleep 5 &
pid3=$!
dprint "$R3 @$pid3"

creater "$R1" eth0@"$pid0":eth1 eth2:eth4@"$pid2" 10.0.1.1/24 10.0.12.1/24
create_vpair "$R1" eth3:eth6@"$pid3" 10.0.13.1/24

create_vpair "$R2" eth5:eth7@"$pid3" 10.0.23.1/24
create_vpair "$R3" eth8:eth0@"$pid1" 10.0.3.1/24

### Finalizing nodes
nsenter --net="$ED1" -- ip link set eth0 up
nsenter --net="$ED1" -- ip addr add 10.0.1.10/24 broadcast + dev eth0
nsenter --net="$ED1" -- ip route add default via 10.0.1.1

nsenter --net="$ED2" -- ip link set eth0 up
nsenter --net="$ED2" -- ip addr add 10.0.3.10/24 broadcast + dev eth0
nsenter --net="$ED2" -- ip route add default via 10.0.3.1

nsenter --net="$R2" -- ip link set eth4 up
nsenter --net="$R2" -- ip addr add 10.0.12.2/24 broadcast + dev eth4

nsenter --net="$R3" -- ip link set eth6 up
nsenter --net="$R3" -- ip addr add 10.0.13.2/24 broadcast + dev eth6
nsenter --net="$R3" -- ip link set eth7 up
nsenter --net="$R3" -- ip addr add 10.0.23.2/24 broadcast + dev eth7

# for ns in "$ED1" "$ED2" "$R1" "$R2" "$R3"; do
#     echo "NS: $ns"
#     nsenter --net="$ns" -- ip -br link show
#     nsenter --net="$ns" -- ip -br addr show
# done

print "Creating OSPF config ..."
cat <<EOF > "/tmp/$NM/bird.conf"
protocol device {
}
protocol direct {
	ipv4;
}
protocol kernel {
	ipv4 {
		export all;
	};
	learn;
}
protocol ospf {
	ipv4 {
		import all;
	};
	area 0 {
		interface "eth*" {
			type broadcast;
			hello 1;
			wait  3;
			dead  5;
		};
	};
}
EOF
cat "/tmp/$NM/bird.conf"

print "Starting Bird OSPF ..."
nsenter --net="$R1" -- bird -c "/tmp/$NM/bird.conf" -d -s "/tmp/$NM/r1-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R2" -- bird -c "/tmp/$NM/bird.conf" -d -s "/tmp/$NM/r2-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R3" -- bird -c "/tmp/$NM/bird.conf" -d -s "/tmp/$NM/r3-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

print "Disabling rp_filter on routers ..."
nsenter --net="$R1" -- sysctl -w net.ipv4.conf.all.rp_filter=0
nsenter --net="$R2" -- sysctl -w net.ipv4.conf.all.rp_filter=0
nsenter --net="$R3" -- sysctl -w net.ipv4.conf.all.rp_filter=0


print "Starting pimd ..."
nsenter --net="$R1" -- ../src/pimd -i "one" -f "/tmp/$NM/conf1" -n -p "/tmp/$NM/r1.pid" $DEBUG -u "/tmp/$NM/r1.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R2" -- ../src/pimd -i "two" -f "/tmp/$NM/conf2" -n -p "/tmp/$NM/r2.pid" $DEBUG -u "/tmp/$NM/r2.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R3" -- ../src/pimd -i "tre" -f "/tmp/$NM/conf3" -n -p "/tmp/$NM/r3.pid" $DEBUG -u "/tmp/$NM/r3.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

# Wait for routers to peer
print "Waiting for OSPF routers to peer (30 sec) ..."
tenacious 30 nsenter --net="$ED1" -- ping -qc 1 -W 1 10.0.3.10 >/dev/null
dprint "OK"

if [ -n "$DEBUG" ]; then
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    echo
    echo
    print "Sleeping 5 sec to allow pimd instances to peer ..."
    sleep 5
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    dprint "OK"
else
    print "Sleeping 5 sec to allow pimd instances to peer ..."
    sleep 5
    dprint "PIM RP Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show crp
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show rp
    dprint "PIM RP Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show crp
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show rp
    dprint "PIM RP Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show crp
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show rp
    dprint "OK"
fi

# dprint "OSPF State & Routing Table $R1:"
# nsenter --net="$R1" -- echo "show ospf state" | birdc -s "/tmp/$NM/r1-bird.sock"
# nsenter --net="$R1" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r1-bird.sock"
# nsenter --net="$R1" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r1-bird.sock"
# nsenter --net="$R1" -- ip route

# dprint "OSPF State & Routing Table $R2:"
# nsenter --net="$R2" -- echo "show ospf state" | birdc -s "/tmp/$NM/r2-bird.sock"
# nsenter --net="$R2" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r2-bird.sock"
# nsenter --net="$R2" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r2-bird.sock"
# nsenter --net="$R2" -- ip route

# dprint "OSPF State & Routing Table $R3:"
# nsenter --net="$R3" -- echo "show ospf state" | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R3" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R3" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R3" -- ip route

# print "Verifying $ED1 ability to reach its gateway ..."
# tenacious 30 nsenter --net="$ED1" -- ping -qc 1 -W 1 10.0.0.1 >/dev/null

# print "Verifying $ED2 ability to reach its gateway ..."
# tenacious 30 nsenter --net="$ED2" -- ping -qc 1 -W 1 10.0.2.1 >/dev/null

print "Starting receiver ..."
nsenter --net="$ED2" -- ./mping -r -i eth0 -t 5 -W 30 225.1.2.3 &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

dprint "PIM Status $R3"
nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail

print "Starting emitter ..."
if ! nsenter --net="$ED1"  -- ./mping -s -d -i eth0 -t 5 -c 30 -w 60 225.1.2.3; then
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail

    print "Starting receiver #2 ..."
    nsenter --net="$ED2" -- ./mping -r -d -i eth0 -t 5 -W 30 225.1.2.4 &
    echo $! >> "/tmp/$NM/PIDs"
    sleep 5

    print "Starting emitter #2 ..."
    if ! nsenter --net="$ED1"  -- ./mping -s -d -i eth0 -t 5 -c 30 -w 60 225.1.2.4; then
	echo "Failed routing, expected at least 30 multicast ping replies"
	FAIL
    fi
fi

OK
