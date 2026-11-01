#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#
# Laptop side, READ-ONLY on the head unit: copies what the HUDTerminal capture plan needs (README section 9) from
# hu-omap over SSH. On the head unit it only runs find, ls and cat; nothing is written there.
#
#   - BMW's MOST library (parameter packing), the trace and DSI libraries, trace-recorder-app
#   - starter.cfg and the other configs that may start NavNBTEvoController or map logical FBlocks
#   - listings of the trace settings (/var/opt/sys) and trace output (/mnt/quota/sys/trace)
#
# Needs the shared SSH login first (owner types the password once):  ssh -F car/ssh_config -fN nbt
# Usage (from the repository root):  sh carplay-hud/tools/fetch_capture_deps.sh
# Output: car/recon/deps_<time>/ (git-ignored). Files over 40 MB are listed, not copied.

cd "$(dirname "$0")/../.." || exit 1
R() { timeout 120 ssh -F car/ssh_config nbt "$@"; }
O='on -f /net/hu-omap'

if ! timeout 10 ssh -F car/ssh_config -O check nbt 2>/dev/null; then
    echo "no shared SSH login: run  ssh -F car/ssh_config -fN nbt  first"; exit 1
fi
OUT=car/recon/deps_$(date +%Y%m%d_%H%M%S)
mkdir -p "$OUT/files" || exit 1

# One find per directory, in the -name ... -o ... form already used on this head unit; no head/tr/xargs there.
cat > "$OUT/find.sh" <<'EOF'
for d in /opt /lib /usr/lib /proc/boot /etc; do
  find $d \( -name 'libimp_sys_most*' -o -name 'libsys_trace*' -o -name 'libsys_colibry*' \
       -o -name 'libsys_dsi_trace*' -o -name 'libsys_dsi_colibry*' -o -name 'libSysMoCCA*' \
       -o -name 'trace-recorder-app*' -o -name 'starter*.cfg' -o -name '*omponent*nfo*' \
       -o -name '*og[Dd]ev*' -o -name 'servicebroker*.cfg' -o -name 'Trace*.tr' -o -name 'HEADER.txt' \) 2>/dev/null
done
EOF
{
    R "$O /bin/sh -s" < "$OUT/find.sh"
    echo /mnt/share/nav/NavCtrl_DB.ini   # the nav controller's own settings (IC/HUD drawing, README section 10)
} | sort -u > "$OUT/found.txt"
echo "found $(wc -l < "$OUT/found.txt") file(s)"

# Listings only (names and sizes), no contents.
for d in /var/opt/sys /mnt/quota/sys/trace /mnt/quota/sys/trace/ioc /opt/sys/etc; do
    echo "=== $d"; R "$O ls -l $d" 2>&1
done > "$OUT/listings.txt"

# ssh reads stdin: inside this loop every call gets /dev/null, or it would swallow the rest of found.txt.
while read -r f; do
    [ -n "$f" ] || continue
    line=$(R "$O ls -l $f" < /dev/null 2>/dev/null)
    case "$line" in l*) echo "link $f (target copied if it matched too)" >> "$OUT/skipped.txt"; continue ;; esac
    sz=$(echo "$line" | awk '{print $5}')
    case "$sz" in ''|*[!0-9]*) echo "skip $f (no size)" >> "$OUT/skipped.txt"; continue ;; esac
    if [ "$sz" -gt 41943040 ]; then echo "skip $f ($sz bytes)" >> "$OUT/skipped.txt"; continue; fi
    dst="$OUT/files/$(echo "$f" | sed 's#^/##; s#/#_#g')"
    R "$O cat $f" < /dev/null > "$dst" 2>/dev/null
    got=$(wc -c < "$dst")
    [ "$got" = "$sz" ] || echo "SIZE MISMATCH $f: expected $sz, got $got" >> "$OUT/skipped.txt"
done < "$OUT/found.txt"

echo "copied $(ls "$OUT/files" | wc -l) file(s) to $OUT/files"
[ -s "$OUT/skipped.txt" ] && { echo "not copied / problems:"; cat "$OUT/skipped.txt"; }
exit 0
