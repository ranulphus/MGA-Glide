#!/usr/bin/env bash
# Loop C: read-only probe of the G200eR2 on cuda6 (PRD §4.4, Q9 stage 1).
# Writes out/rig/probe.txt. Touches nothing on the host except reads of the
# device's sysfs files.
set -euo pipefail
host=${RIG_HOST:-retro@cuda6}
bdf=${RIG_BDF:-0000:0a:00.0}
out=out/rig; mkdir -p "$out"
ssh -o BatchMode=yes -o ConnectTimeout=10 "$host" bash -s "$bdf" > "$out/probe.txt" <<'REMOTE'
set -e
bdf=$1
d=/sys/bus/pci/devices/$bdf
echo "# rig-probe $(date -u +%FT%TZ) host=$(hostname) kernel=$(uname -r)"
rev=$(sudo -n od -An -tx1 -j 8 -N 1 $d/config | tr -d ' ')
echo "vendor=$(cat $d/vendor) device=$(cat $d/device) subsystem=$(cat $d/subsystem_vendor):$(cat $d/subsystem_device) revision=0x$rev"
if [ -e $d/driver ]; then echo "driver=$(basename "$(readlink -f $d/driver)")"; else echo "driver=none"; fi
echo "boot_vga=$(cat $d/boot_vga 2>/dev/null || echo ?) enable=$(cat $d/enable)"
echo "# resources (start end flags)"
head -3 $d/resource
echo "# config space"
sudo -n od -An -tx1 -v -N 256 $d/config
REMOTE
cat "$out/probe.txt"
