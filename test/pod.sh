#!/bin/sh
#
#
#     10.0.0.0/24           10.0.1.0/24            10.0.2.0/24
#
#              .--- eth1:R1:eth3 --- eth4:R2:eth5 ---.
#             /                                       \
#            /                                         \
#  ED1 ---br0                                           br1--- ED2
#            \                                         /
#	      \                                       /
#              '--- eth2:R3:eth6 --- eth7:R4:eth8 ---'
#
#                           10.0.3.0/24
#

# pimd debug, when enabled pimctl calls at runtime are also enabled
#DEBUG="-l debug -d all"

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

# Requires OSPF (bird) to build the unicast rpf tree
print "Check deps ..."
check_dep ethtool
check_dep tshark
check_dep bird
check_dep keepalived

print "Creating world ..."
R1="/tmp/$NM/R1"
R2="/tmp/$NM/R2"
R3="/tmp/$NM/R3"
R4="/tmp/$NM/R4"
ED1="/tmp/$NM/ED1"
ED2="/tmp/$NM/ED2"
touch "$R1" "$R2" "$R3" "$R4" "$ED1" "$ED2"

echo "$R1"   > "/tmp/$NM/mounts"
echo "$R2"  >> "/tmp/$NM/mounts"
echo "$R3"  >> "/tmp/$NM/mounts"
echo "$R4"  >> "/tmp/$NM/mounts"
echo "$ED1" >> "/tmp/$NM/mounts"
echo "$ED2" >> "/tmp/$NM/mounts"

unshare --net="$R1" -- ip link set lo up
unshare --net="$R2" -- ip link set lo up
unshare --net="$R3" -- ip link set lo up
unshare --net="$R4" -- ip link set lo up
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

dprint "Creating $ED1 end-device ..."
nsenter --net="$ED1" -- sleep 5 &
pid0=$!
create_vpair "$ED1" eth0:eth0b@"$pid0" 10.0.0.10/24
nsenter --net="$ED1" -- ip route add default via 10.0.0.1

dprint "Creating $R2 router ..."
nsenter --net="$R2" -- sleep 5 &
pid2=$!

dprint "Creating $R4 router ..."
nsenter --net="$R4" -- sleep 5 &
pid4=$!

dprint "Creating R1 router with eth1b in PID $pid0 and eth3 in PID $pid2"
creater "$R1" eth1b@"$pid0":eth1 eth3:eth4@"$pid2" 10.0.0.2/24 10.0.1.1/24

dprint "Creating R3 router with eth2b in PID $pid0 and eth6 in PID $pid4"
creater "$R3" eth2b@"$pid0":eth2 eth6:eth7@"$pid4" 10.0.0.3/24 10.0.3.1/24

dprint "Creating $ED2 end-device ..."
nsenter --net="$ED2" -- sleep 5 &
pid5=$!
create_vpair "$ED2" eth0:eth0b@"$pid5" 10.0.2.10/24
nsenter --net="$ED2" -- ip route add default via 10.0.2.1

dprint "Finalizing R2 router ..."
create_vpair "$R2" eth5:eth5b@"$pid5" 10.0.2.2/24
nsenter --net="$R2" -- ip link set eth4 up
nsenter --net="$R2" -- ip addr add 10.0.1.2/24 broadcast + dev eth4

dprint "Finalizing R4 router ..."
create_vpair "$R4" eth8:eth8b@"$pid5" 10.0.2.3/24
nsenter --net="$R4" -- ip link set eth7 up
nsenter --net="$R4" -- ip addr add 10.0.3.2/24 broadcast + dev eth7

dprint "Finalizing $ED1 end-device ..."
nsenter --net="$ED1" -- ip link set eth0 up
nsenter --net="$ED1" -- ip link set eth0b up
nsenter --net="$ED1" -- ip link set eth1b up
nsenter --net="$ED1" -- ip link set eth2b up
nsenter --net="$ED1" -- ip link add br0 type bridge
nsenter --net="$ED1" -- ip link set br0 up
nsenter --net="$ED1" -- ip link set eth0b master br0
nsenter --net="$ED1" -- ip link set eth1b master br0
nsenter --net="$ED1" -- ip link set eth2b master br0

dprint "Finalizing $ED2 end-device ..."
nsenter --net="$ED2" -- ip link set eth0 up
nsenter --net="$ED2" -- ip link set eth0b up
nsenter --net="$ED2" -- ip link set eth5b up
nsenter --net="$ED2" -- ip link set eth8b up
nsenter --net="$ED2" -- ip link add br0 type bridge
nsenter --net="$ED2" -- ip link set br0 up
nsenter --net="$ED2" -- ip link set eth0b master br0
nsenter --net="$ED2" -- ip link set eth5b master br0
nsenter --net="$ED2" -- ip link set eth8b master br0

print "$R1: starting up VRRP ..."
cat <<EOF > "/tmp/$NM/keep-r1.conf"
vrrp_instance left {
        state MASTER
        interface eth1
        virtual_router_id 51
        priority 255
        advert_int 1
        virtual_ipaddress {
              10.0.0.1/24
        }
}
EOF
cat "/tmp/$NM/keep-r1.conf"

nsenter --net="$R1" -- keepalived -P -p "/tmp/$NM/keep-r1.pid" -r "/tmp/$NM/vrrp-r1.pid" -f "/tmp/$NM/keep-r1.conf" -l -D -n &
echo $! >> "/tmp/$NM/PIDs"

print "$R2: starting up VRRP ..."
cat <<EOF > "/tmp/$NM/keep-r2.conf"
vrrp_instance right {
        state MASTER
        interface eth5
        virtual_router_id 52
        priority 255
        advert_int 1
        virtual_ipaddress {
              10.0.2.1/24
        }
}
EOF
cat "/tmp/$NM/keep-r2.conf"

nsenter --net="$R2" -- keepalived -P -p "/tmp/$NM/keep-r2.pid" -r "/tmp/$NM/vrrp-r2.pid" -f "/tmp/$NM/keep-r2.conf" -l -D -n &
echo $! >> "/tmp/$NM/PIDs"

print "$R3: starting up VRRP ..."
cat <<EOF > "/tmp/$NM/keep-r3.conf"
vrrp_instance left {
        state BACKUP
        interface eth2
        virtual_router_id 51
        priority 254
        advert_int 1
        virtual_ipaddress {
              10.0.0.1/24
        }
}
EOF
cat "/tmp/$NM/keep-r3.conf"

nsenter --net="$R3" -- keepalived -P -p "/tmp/$NM/keep-r3.pid" -r "/tmp/$NM/vrrp-r3.pid" -f "/tmp/$NM/keep-r3.conf" -l -D -n &
echo $! >> "/tmp/$NM/PIDs"

print "$R4: starting up VRRP ..."
cat <<EOF > "/tmp/$NM/keep-r4.conf"
vrrp_instance right {
        state BACKUP
        interface eth8
        virtual_router_id 52
        priority 254
        advert_int 1
        virtual_ipaddress {
              10.0.2.1/24
        }
}
EOF
cat "/tmp/$NM/keep-r4.conf"

nsenter --net="$R4" -- keepalived -P -p "/tmp/$NM/keep-r4.pid" -r "/tmp/$NM/vrrp-r4.pid" -f "/tmp/$NM/keep-r4.conf" -l -D -n &
echo $! >> "/tmp/$NM/PIDs"

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
nsenter --net="$R4" -- bird -c "/tmp/$NM/bird.conf" -d -s "/tmp/$NM/r4-bird.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

print "Disabling rp_filter on routers ..."
nsenter --net="$R1" -- sysctl -w net.ipv4.conf.all.rp_filter=0
nsenter --net="$R2" -- sysctl -w net.ipv4.conf.all.rp_filter=0
nsenter --net="$R3" -- sysctl -w net.ipv4.conf.all.rp_filter=0
nsenter --net="$R4" -- sysctl -w net.ipv4.conf.all.rp_filter=0


print "Creating PIM configs ..."
cat <<EOF > "/tmp/$NM/conf1"
# Bigger value means  "higher" priority
bsr-candidate priority 5 interval 5

# Smaller value means "higher" priority
rp-candidate priority 20 interval 5

# Switch to shortest-path tree after first packet
spt-threshold packets 0 interval 0
EOF
cat <<EOF > "/tmp/$NM/conf2"
# Bigger value means  "higher" priority
bsr-candidate priority 6 interval 5

# Smaller value means "higher" priority
rp-candidate priority 19 interval 5

# Switch to shortest-path tree after first packet
spt-threshold packets 0 interval 0
EOF
cat <<EOF > "/tmp/$NM/conf3"
# Bigger value means  "higher" priority
bsr-candidate priority 7 interval 5

# Smaller value means "higher" priority
rp-candidate priority 18 interval 5

# Switch to shortest-path tree after first packet
spt-threshold packets 0 interval 0
EOF
cat <<EOF > "/tmp/$NM/conf4"
# Bigger value means  "higher" priority
bsr-candidate priority 8 interval 5

# Smaller value means "higher" priority
rp-candidate priority 17 interval 5

# Switch to shortest-path tree after first packet
spt-threshold packets 0 interval 0
EOF

cat "/tmp/$NM/conf1"

print "Starting pimd ..."
nsenter --net="$R1" -- ../src/pimd -i "one" -f "/tmp/$NM/conf1" -n -p "/tmp/$NM/r1.pid" $DEBUG -u "/tmp/$NM/r1.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R2" -- ../src/pimd -i "two" -f "/tmp/$NM/conf2" -n -p "/tmp/$NM/r2.pid" $DEBUG -u "/tmp/$NM/r2.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R3" -- ../src/pimd -i "tre" -f "/tmp/$NM/conf3" -n -p "/tmp/$NM/r3.pid" $DEBUG -u "/tmp/$NM/r3.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R4" -- ../src/pimd -i "fyr" -f "/tmp/$NM/conf4" -n -p "/tmp/$NM/r4.pid" $DEBUG -u "/tmp/$NM/r4.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

print "Router link states"
dprint "$ED1"
nsenter --net="$ED1" -- ip -br l
nsenter --net="$ED1" -- ip -br a
nsenter --net="$ED1" -- bridge link
dprint "$R1"
nsenter --net="$R1" -- ip -br l
nsenter --net="$R1" -- ip -br a
dprint "$R2"
nsenter --net="$R2" -- ip -br l
nsenter --net="$R2" -- ip -br a
dprint "$R3"
nsenter --net="$R3" -- ip -br l
nsenter --net="$R3" -- ip -br a
dprint "$R4"
nsenter --net="$R4" -- ip -br l
nsenter --net="$R4" -- ip -br a
dprint "$ED2"
nsenter --net="$ED2" -- ip -br l
nsenter --net="$ED2" -- ip -br a
nsenter --net="$ED2" -- bridge link

print "Routing tables on end-devices"
dprint "$ED1"
nsenter --net="$ED1" -- ip -br r
nsenter --net="$ED1" -- bridge fdb show
nsenter --net="$ED1" -- ping -c 10 -W 1 10.0.0.1
nsenter --net="$ED1" -- ip neigh
dprint "$ED2"
nsenter --net="$ED2" -- ip -br r
nsenter --net="$ED2" -- bridge fdb show
nsenter --net="$ED2" -- ip neigh

# Wait for routers to peer
print "Waiting for OSPF routers to peer (30 sec) ..."

dprint "R1 <-> R2"
tenacious 30 nsenter --net="$R1" -- ping -qc 1 -W 1 10.0.1.2 >/dev/null
dprint "OK"
dprint "R2 <-> R4"
tenacious 30 nsenter --net="$R2" -- ping -qc 1 -W 1 10.0.3.2 >/dev/null
dprint "OK"
dprint "R1 <-> R4"
tenacious 30 nsenter --net="$R2" -- ping -qc 1 -W 1 10.0.3.2 >/dev/null
dprint "OK"

if [ -n "$DEBUG" ]; then
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    dprint "PIM Status $R4"
    nsenter --net="$R4" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    echo
    echo
    print "Sleeping 10 sec to allow pimd instances to peer ..."
    sleep 10
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    dprint "PIM Status $R4"
    nsenter --net="$R4" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    dprint "OK"
else
    print "Sleeping 10 sec to allow pimd instances to peer ..."
    sleep 10
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

# dprint "OSPF State & Routing Table $R4:"
# nsenter --net="$R4" -- echo "show ospf state" | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R4" -- echo "show ospf int"   | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R4" -- echo "show ospf neigh" | birdc -s "/tmp/$NM/r3-bird.sock"
# nsenter --net="$R4" -- ip route

# print "Verifying $ED1 ability to reach its gateway ..."
# tenacious 30 nsenter --net="$ED1" -- ping -qc 1 -W 1 10.0.0.1 >/dev/null

# print "Verifying $ED2 ability to reach its gateway ..."
# tenacious 30 nsenter --net="$ED2" -- ping -qc 1 -W 1 10.0.2.1 >/dev/null

print "Verifying end-to-end unicast connectivity (30 sec) ..."
tenacious 30 nsenter --net="$ED1" -- ping -qc 1 -W 1 10.0.2.10 >/dev/null
dprint "OK"

print "Starting emitter ..."
nsenter --net="$ED2" -- ./mping -qr -d -i eth0 -t 5 -W 30 225.1.2.3 &
echo $! >> "/tmp/$NM/PIDs"
sleep 5

if ! nsenter --net="$ED1"  -- ./mping -s -d -i eth0 -t 5 -c 30 -w 60 225.1.2.3; then
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
    dprint "PIM Status $R3"
    nsenter --net="$R3" -- ../src/pimctl -u "/tmp/$NM/r3.sock" show compat detail
    dprint "PIM Status $R4"
    nsenter --net="$R4" -- ../src/pimctl -u "/tmp/$NM/r4.sock" show compat detail
    echo "Failed routing, expected at least 30 multicast ping replies"
    FAIL
fi

OK
