#!/bin/sh
# Two routers with end devices on each end.  Compared to other tests,
# sender on ED1 starts before the receiver on ED2 sends an IGMP join.
#
# ED1         R1               R2               ED2
# [ED1:eth0]--[eth1:R1:eth2]---[eth3:R2:eth4]---[eth0:ED2]
#       10.0.1.0/24     10.0.0.0/24      10.0.2.0/24

# shellcheck source=/dev/null
. "$(dirname "$0")/lib.sh"

# Requires OSPF (bird) to build the unicast rpf tree
print "Check deps ..."
check_dep ethtool
check_dep tshark
check_dep bird

print "Creating world ..."
ED1="/tmp/$NM/ED1"
ED2="/tmp/$NM/ED2"
R1="/tmp/$NM/R1"
R2="/tmp/$NM/R2"
touch "$ED1" "$ED2" "$R1" "$R2"

echo "$ED1"  > "/tmp/$NM/mounts"
echo "$ED2" >> "/tmp/$NM/mounts"
echo "$R1"  >> "/tmp/$NM/mounts"
echo "$R2"  >> "/tmp/$NM/mounts"

unshare --net="$ED1" -- ip link set lo up
unshare --net="$ED2" -- ip link set lo up
unshare --net="$R1"  -- ip link set lo up
unshare --net="$R2"  -- ip link set lo up

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

creater()
{
    if echo "$2" |grep -q ':'; then
	x=$(echo "$2" | cut -f1 -d:)
	b=$(echo "$2" | cut -f2 -d:)
	echo "1) Found x=$x and b=$b ..."
	a=$(echo "$x" | cut -f1 -d@)
	p=$(echo "$x" | cut -f2 -d@)
	echo "1) Found a=$a and p=$p ..."
	echo "Creating router interfaces $a and $b ..."
	nsenter --net="$1" -- ip link add "$a" type veth peer "$b"
	ifsetup "$1" "$a" "$a"

	echo "$1 moving $a to netns PID $p"
	nsenter --net="$1" -- ip link set "$a" netns "$p"
    else
	b=$2
    fi

    echo "Bringing up $b with addr $4"
    nsenter --net="$1" -- ip link set "$b" up
    nsenter --net="$1" -- ip addr add "$4" broadcast + dev "$b"

    if echo "$3" |grep -q ':'; then
	a=$(echo "$3" | cut -f1 -d:)
	x=$(echo "$3" | cut -f2 -d:)
	echo "2) Found x=$x and b=$b ..."
	b=$(echo "$x" | cut -f1 -d@)
	p=$(echo "$x" | cut -f2 -d@)
	echo "2) Found a=$a and p=$p ..."
	echo "Creating router interfaces $a and $b ..."
	nsenter --net="$1" -- ip link add "$a" type veth peer "$b"
	ifsetup "$1" "$a" "$a"

	echo "$1 moving $b to netns PID $p"
	nsenter --net="$1" -- ip link set "$b" netns "$p"
    else
	a=$3
    fi

    echo "Bringing up $a with addr $5"
    nsenter --net="$1" -- ip link set "$a" up
    nsenter --net="$1" -- ip addr add "$5" broadcast + dev "$a"
}

dprint "Creating $R1 router ..."
nsenter --net="$R1" -- sleep 5 &
pid1=$!
dprint "Creating $R2 router ..."
nsenter --net="$R2" -- sleep 5 &
pid2=$!

dprint "Creating ED1 with eth1 in PID $pid1"
created "$ED1" eth0 eth1@"$pid1" 10.0.1.10/24 10.0.1.1

dprint "Creating ED2 with eth4 in PID $pid2"
created "$ED2" eth0 eth4@"$pid2" 10.0.2.10/24 10.0.2.1

dprint "Creating R2 router with eth2 in PID $pid1 and eth3 in PID $pid2"
creater "$R2" eth2@"$pid1":eth3 eth4 10.0.0.2/24 10.0.2.1/24

dprint "Creating R1 router using interfaces donated by neighbors"
creater "$R1" eth1 eth2 10.0.1.1/24 10.0.0.1/24

dprint "$ED1"
nsenter --net="$ED1" -- ip -br l
nsenter --net="$ED1" -- ip -br a
dprint "$ED2"
nsenter --net="$ED2" -- ip -br l
nsenter --net="$ED2" -- ip -br a
dprint "$R1"
nsenter --net="$R1" -- ip -br l
nsenter --net="$R1" -- ip -br a
dprint "$R2"
nsenter --net="$R2" -- ip -br l
nsenter --net="$R2" -- ip -br a

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
sleep 1

print "Starting collectors ..."
nsenter --net="$ED1"  -- tshark -lni eth0 -w "/tmp/$NM/ed1.pcap" 2>/dev/null &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R2"  -- tshark -lni eth3 -w "/tmp/$NM/eth3.pcap" 2>/dev/null &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$ED2" -- tshark -lni eth0 -w "/tmp/$NM/ed2.pcap" 2>/dev/null &
echo $! >> "/tmp/$NM/PIDs"
sleep 1

print "Creating PIM config ..."
cat <<EOF > "/tmp/$NM/conf"
# Bigger value means  "higher" priority
bsr-candidate priority 5 interval 5

# Smaller value means "higher" priority
rp-candidate priority 20 interval 5

# Static rendez-vous point
#rp-address 10.0.2.1 224.0.0.0/4

# Switch to shortest-path tree after first packet, after 1 sec.
spt-threshold packets 0 interval 1
EOF
cat "/tmp/$NM/conf"

print "Starting pimd ..."
nsenter --net="$R1" -- ../src/pimd -i R1 -f "/tmp/$NM/conf" -n -p "/tmp/$NM/r1.pid" -d all -l debug -u "/tmp/$NM/r1.sock" &
echo $! >> "/tmp/$NM/PIDs"
nsenter --net="$R2" -- ../src/pimd -i R2 -f "/tmp/$NM/conf" -n -p "/tmp/$NM/r2.pid" -d all -l debug -u "/tmp/$NM/r2.sock" &
echo $! >> "/tmp/$NM/PIDs"
sleep 5

# Must start after the pimd's, doesn't exist before that ...
# nsenter --net="$R2"  -- tshark -lni pimreg -w "/tmp/$NM/pimreg.pcap" 2>/dev/null &
# echo $! >> "/tmp/$NM/PIDs"

# Wait for routers to peer
print "Waiting for OSPF routers to peer (30 sec) ..."
tenacious 30 nsenter --net="$ED1" -- ping -qc 1 -W 1 10.0.2.10 >/dev/null

dprint "PIM Status $R1"
nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
dprint "PIM Status $NR2"
nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
echo
echo
print "Sleeping 10 sec to allow pimd instances to peer ..."
sleep 10
dprint "PIM Status $R1"
nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
dprint "PIM Status $NR2"
nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail
dprint "OK"

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

print "Starting sender ..."
nsenter --net="$ED1"  -- ./mping -s -d -i eth0 -t 5 -c 30 -w 60 225.1.2.3 &
PID=$!

# Allow PIM routers to forward to the elected RP
sleep 2

# Then we start the receiver, as in GitHub issue #192, which reports a delay
# of 10 sec. when starting after the sender.
print "Starting receiver ..."
nsenter --net="$ED2" -- ./mping -qr -d -i eth0 -t 5 -W 60 225.1.2.3 &
echo $! >> "/tmp/$NM/PIDs"

wait $PID
rc=$?
dprint "Sender returns: $rc"

if [ $rc -ne 0 ]; then
    dprint "PIM Status $R1"
    nsenter --net="$R1" -- ../src/pimctl -u "/tmp/$NM/r1.sock" show compat detail
    dprint "PIM Status $R2"
    nsenter --net="$R2" -- ../src/pimctl -u "/tmp/$NM/r2.sock" show compat detail

    print "Show pcaps"
    kill_pids
    dprint "ED1 pcap"
    tshark -n -r "/tmp/$NM/ed1.pcap"
    dprint "Eth3 pcap"
    tshark -n -r "/tmp/$NM/eth3.pcap"
    # dprint "pimreg pcap"
    # tshark -n -r "/tmp/$NM/pimreg.pcap"
    dprint "ED2 pcap"
    tshark -n -r "/tmp/$NM/ed2.pcap"
    echo "Failed routing, expected at least 30 multicast ping replies"
    FAIL
fi

OK
