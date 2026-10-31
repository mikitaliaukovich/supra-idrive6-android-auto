#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#
# CarPlay -> HUD recon for NBT EVO (ID6). READ-ONLY on the head unit: it only reads, and writes to the USB stick.
#
# Copy to the USB stick, then on the car shell:
#   sh /mnt/umass01200t7/rgd_recon.sh            static scan (processes, iAP2 / navigation / KOMBI / HUD binaries)
#   sh /mnt/umass01200t7/rgd_recon.sh live       live snapshot: run it once with CarPlay connected and Apple Maps
#                                                guiding, and once with BMW's own navigation guiding (HUD arrows on)
# Output: <stick>/rgd_recon_<time>/ . Zip the folder and send it. Note: config copies include the car's
# CarPlay hotspot password (iap2.cfg); remove it before posting the folder publicly.

MODE=${1:-static}
STICK=${0%/*}
[ "$STICK" = "$0" ] && STICK=.
STAMP=$(date +%Y%m%d_%H%M%S 2>/dev/null || echo now)
OUT=$STICK/rgd_recon_${MODE}_$STAMP
mkdir -p "$OUT" || { echo "cannot write to $OUT"; exit 1; }
echo "rgd_recon: mode $MODE, output $OUT"

run() {   # run <file> <command...>: never stops the script
    f=$1; shift
    echo "### $*" >> "$OUT/$f"
    "$@" >> "$OUT/$f" 2>&1
    echo >> "$OUT/$f"
}

# ------------------------------------------------------------------ always
run system.txt uname -a
run system.txt pidin info
run system.txt cat /etc/version
run system.txt ls -l /dev/btspp0 /dev/btspp1 /dev/btspp2 /dev/btspp3
run processes.txt pidin arg

if [ "$MODE" = "live" ]; then
    # Who talks to the phone while CarPlay runs: iAP2 over Bluetooth (btspp) and over Wi-Fi (TCP on uap0).
    run net.txt netstat -an
    run net.txt ifconfig -a
    run fds.txt pidin fds
    run mem.txt pidin mem
    run slog.txt sloginfo
    echo "rgd_recon: live snapshot done"
    exit 0
fi

# ------------------------------------------------------------------ static
SCAN=${RGD_SCAN:-"/opt /usr/lib /lib /proc/boot /usr/bin /usr/sbin /bin /sbin"}
find $SCAN -type f 2>/dev/null > "$OUT/all_files.txt"
echo "rgd_recon: $(wc -l < "$OUT/all_files.txt") files to scan (takes a few minutes)"

scan() {   # scan <name> <pattern...>: files whose bytes contain any of the patterns
    name=$1; shift
    pats=""
    for p in "$@"; do pats="$pats -e $p"; done
    if command -v xargs >/dev/null 2>&1; then
        xargs grep -l $pats < "$OUT/all_files.txt" 2>/dev/null
    else
        while read -r f; do grep -l $pats "$f" 2>/dev/null; done < "$OUT/all_files.txt"
    fi > "$OUT/hits_$name.txt"
    echo "rgd_recon: $name: $(wc -l < "$OUT/hits_$name.txt") file(s)"
}

scan iap2           iAP2 IAP2 iap2 Iap2
scan identification IdentificationInformation MessagesSentByAccessory StartIdentification
scan routeguidance  RouteGuidance routeGuidance ROUTE_GUIDANCE StartRouteGuidance
scan kombi_hud      Kombi KOMBI HeadUp HEADUP HudNavi HUDNavi
scan maneuver       Maneuver Manoeuvre MANEUVER Pfeil ArrowGuidance TurnArrow Abbiege
scan carplay        CarPlay Carplay carplay AirPlay airplay

# Readable strings around the interesting words, per file (no `strings` on the car: tr does the job).
words() {   # words <hits-file> <out-file> <egrep>
    : > "$OUT/$2"
    while read -r f; do
        echo "=== $f" >> "$OUT/$2"
        tr -c '\040-\176' '\n' < "$f" 2>/dev/null | grep -E "$3" | sort -u | head -400 >> "$OUT/$2"
    done < "$OUT/$1"
}
words hits_iap2.txt          strings_iap2.txt     'Identification|RouteGuidance|0x52|MessagesSent|MessagesReceived|Component|iAP2[A-Za-z]+'
words hits_routeguidance.txt strings_rg.txt       'RouteGuidance|Maneuver|Lane|Junction'
words hits_kombi_hud.txt     strings_kombi_hud.txt 'Kombi|KOMBI|HeadUp|HUD|Maneuver|Arrow|Pfeil|Guidance|DSI[A-Za-z]+'

# Config files that may drive the iAP2 identification (on MIB2 it is a JSON with the message-id lists).
find /opt /etc /var /HBpersistence /mnt/persistence \( -name '*iap*' -o -name '*IAP*' -o -name '*arplay*' \
    -o -name '*CarPlay*' -o -name '*kombi*' -o -name '*hud*' -o -name '*HUD*' \) 2>/dev/null > "$OUT/config_candidates.txt"
mkdir -p "$OUT/config"
while read -r f; do
    [ -f "$f" ] || continue
    sz=$(wc -c < "$f" 2>/dev/null)
    [ "${sz:-0}" -le 262144 ] && cp "$f" "$OUT/config/$(echo "$f" | tr '/' '_')" 2>/dev/null
done < "$OUT/config_candidates.txt"

# Binaries for offline reverse engineering: the iAP2 stack and whatever knows route guidance / KOMBI / HUD.
mkdir -p "$OUT/bin"
cat "$OUT/hits_iap2.txt" "$OUT/hits_identification.txt" "$OUT/hits_routeguidance.txt" "$OUT/hits_kombi_hud.txt" \
    2>/dev/null | sort -u > "$OUT/bin_list.txt"
while read -r f; do
    sz=$(wc -c < "$f" 2>/dev/null)
    if [ "${sz:-0}" -le 41943040 ]; then
        cp "$f" "$OUT/bin/$(echo "$f" | tr '/' '_')" 2>/dev/null
    else
        echo "$f $sz (too big, not copied)" >> "$OUT/bin_skipped.txt"
    fi
done < "$OUT/bin_list.txt"

echo "rgd_recon: done. Now run 'sh $0 live' with CarPlay + Apple Maps guiding, and again with BMW navigation guiding."
