#!/bin/sh
#
# End-to-end tests for gwsim: run the real gw2dmk/dmk2gw/gwhist
# tools against the simulator and verify the results.
#
# Usage: sim/tests/run-tests.sh [build_dir]

top=$(cd "$(dirname "$0")/../.." && pwd)
bld=${1:-$top/build}
tmp=$(mktemp -d)

gwsim_pid=

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

cleanup() {
	[ -n "$gwsim_pid" ] && kill "$gwsim_pid" 2>/dev/null
	rm -rf "$tmp"
}

trap cleanup EXIT INT TERM

for b in gwsim gw2dmk dmk2gw gwhist mkdmk; do
	[ -x "$bld/$b" ] || fail "$bld/$b not built"
done

# Start gwsim with the given arguments; wait for the pty to appear.
start_gwsim() {
	"$bld/gwsim" --fast -p "$tmp/pty" -s "$tmp/sock" "$@" \
		< /dev/null > "$tmp/gwsim.log" 2>&1 &
	gwsim_pid=$!

	i=0
	while [ ! -e "$tmp/pty" ]; do
		i=$((i + 1))
		[ "$i" -gt 50 ] && fail "gwsim did not start"
		sleep 0.1
	done
}

# Stop gwsim, flushing any dirty media.
stop_gwsim() {
	kill -TERM "$gwsim_pid" 2>/dev/null
	wait "$gwsim_pid" 2>/dev/null
	gwsim_pid=
	rm -f "$tmp/pty" "$tmp/sock"
}

# Send control commands on the socket.
ctl() {
	python3 -c '
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.sendall(("\n".join(sys.argv[2:]) + "\n").encode())
time.sleep(0.3)
s.setblocking(False)
try:
    print(s.recv(65536).decode(), end="")
except BlockingIOError:
    pass
' "$tmp/sock" "$@"
}

echo "=== test 1: gw2dmk read path (5.25\" DD, IBM PC bus)"
"$bld/mkdmk" -t 40 -s 2 "$tmp/golden.dmk"
start_gwsim -D 0:525dd -i "0:$tmp/golden.dmk"
timeout 120 "$bld/gw2dmk" -G "$tmp/pty" -t 40 --force "$tmp/out.dmk" \
	> "$tmp/gw2dmk.log" 2>&1 || \
	{ cat "$tmp/gw2dmk.log"; fail "gw2dmk"; }
"$bld/mkdmk" -c "$tmp/golden.dmk" "$tmp/out.dmk" || \
	fail "read-path sector compare"
stop_gwsim

echo "=== test 2: dmk2gw write path round trip"
"$bld/mkdmk" -t 40 -s 2 -n 1 "$tmp/target.dmk"
start_gwsim -D 0:525dd -i "0:$tmp/target.dmk"
timeout 120 "$bld/dmk2gw" -G "$tmp/pty" -d a "$tmp/golden.dmk" \
	> "$tmp/dmk2gw.log" 2>&1 || \
	{ cat "$tmp/dmk2gw.log"; fail "dmk2gw"; }
stop_gwsim	# flushes written media
"$bld/mkdmk" -c "$tmp/golden.dmk" "$tmp/target.dmk" || \
	fail "write-path sector compare"

echo "=== test 3: gwhist"
start_gwsim -D 0:525dd -i "0:$tmp/golden.dmk"
timeout 60 "$bld/gwhist" -G "$tmp/pty" > "$tmp/gwhist.log" 2>&1 || \
	{ cat "$tmp/gwhist.log"; fail "gwhist"; }
grep -i "rpm" "$tmp/gwhist.log" || fail "gwhist reported no RPM"
stop_gwsim

echo "=== test 4: control socket insert/eject/wp"
start_gwsim -D 0:525dd
ctl status | grep -q "no diskette" || fail "status before insert"
ctl "insert 0 $tmp/golden.dmk" | grep -q "inserted" || fail "insert"
timeout 120 "$bld/gw2dmk" -G "$tmp/pty" -t 5 --force "$tmp/out2.dmk" \
	> "$tmp/gw2dmk2.log" 2>&1 || \
	{ cat "$tmp/gw2dmk2.log"; fail "gw2dmk after insert"; }
# dmk2gw ignores ACK_WRPROT (error checking is stubbed), so verify
# write protection by checking that the image is left untouched.
cp "$tmp/golden.dmk" "$tmp/golden.bak"
"$bld/mkdmk" -t 40 -s 2 -n 1 "$tmp/small.dmk"
ctl "wp 0 on" > /dev/null
timeout 60 "$bld/dmk2gw" -G "$tmp/pty" -d a "$tmp/small.dmk" \
	> "$tmp/dmk2gw2.log" 2>&1
ctl "eject 0" | grep -q "ejected" || fail "eject"
cmp -s "$tmp/golden.dmk" "$tmp/golden.bak" || \
	fail "write-protected diskette was modified"
stop_gwsim

echo "=== test 5: Shugart bus, unit 1, V4.1 model"
start_gwsim -m v4.1 -D 1:525dd -i "1:$tmp/golden.dmk"
timeout 120 "$bld/gw2dmk" -G "$tmp/pty" -B shugart -t 5 --force \
	"$tmp/out3.dmk" > "$tmp/gw2dmk3.log" 2>&1 || \
	{ cat "$tmp/gw2dmk3.log"; fail "gw2dmk on Shugart bus"; }
stop_gwsim

echo "=== test 6: 8\" drive via FDADAP (kind 3)"
"$bld/mkdmk" -t 77 -s 2 -n 26 -l 0x2940 "$tmp/golden8.dmk"
start_gwsim -D 0:8dd -i "0:$tmp/golden8.dmk"
timeout 240 "$bld/gw2dmk" -G "$tmp/pty" -t 77 --force "$tmp/out8.dmk" \
	> "$tmp/gw2dmk8.log" 2>&1 || \
	{ cat "$tmp/gw2dmk8.log"; fail "gw2dmk 8-inch"; }
"$bld/mkdmk" -n 26 -c "$tmp/golden8.dmk" "$tmp/out8.dmk" || \
	fail "8-inch sector compare"
stop_gwsim

echo "=== all tests passed"
