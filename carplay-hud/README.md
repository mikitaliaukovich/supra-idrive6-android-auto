# CarPlay navigation on the BMW HUD / instrument cluster (NBT EVO ID6)

Goal: CarPlay navigation apps (Apple Maps, Google Maps, Waze) show their next turn on the BMW head-up display (and
on the 6WA/6WB cluster if possible), the same way BMW's own navigation does.

Status: **research + groundwork. Nothing here runs on the car yet.** This folder holds the protocol code that can
be built and tested on a PC, a read-only recon script for the car, and the plan.

## 1. Does the Threads video come from this repository?

**No. Neither this fork nor upstream (`Sq00ky/Supra-iDrive6-Android-Auto`, `main` at `6b8607b`) has this
feature.**

- Android Auto sends turn-by-turn data on its *navigation status* channel (channel 10: guidance on/off `0x8003`,
  next turn `0x8006`, turn distance `0x8007`). The daemon only **logs** those messages (the count, and whether
  guidance is on or off). It answers nothing and forwards nothing: `milestone-7/src/aa_session.cpp`, the
  `channel == 10` branch around line 908.
- Nothing in `milestone-7/src`, `rhmiapp/` or `plugin/` talks to the KOMBI or the HUD. The MOST client is
  observe-only and listens for the iDrive controller (FBlock 0xE5) and the voice button (0x15).
- The readme's *Known issues and next steps* says: "HUD navigation arrows: researched in a separate fork, on
  hold." That fork is not public. Upstream has one branch (`main`), and searching it for HUD / cluster work finds only
  that line.
- The Threads post (author `mi.bimmer`, text "Google maps on android auto 😌") gives no technical details. So the
  video comes from somewhere else: the author's private build, that unpublished fork, or another product. From the
  repository alone there is no way to tell which. Also, the cluster in the photo draws several side roads with real
  shapes. Android Auto's navigation status only carries a maneuver, a distance, a road name and an optional icon, so
  that picture was not built from Android Auto data alone.

So for Android Auto it is *possible* (the data reaches the head unit), but the code that turns it into HUD/KOMBI
output has not been published.

## 2. CarPlay can do it too, through a different route

CarPlay carries the next turn in its own protocol, iAP2, not in the video. An accessory that declares a
**RouteGuidanceDisplayComponent** when it identifies itself receives:

| iAP2 message | Direction | Content |
|---|---|---|
| `0x5200` StartRouteGuidanceUpdates | car → iPhone | "send me guidance for component N" |
| `0x5201` RouteGuidanceUpdate | iPhone → car | route state, current road, destination, ETA, distance to the next maneuver ("2.4" km), list of the current maneuver indices |
| `0x5202` RouteGuidanceManeuverUpdate | iPhone → car | one maneuver: type (54 kinds: left, slight right, roundabout exit 3, ...), road after the maneuver, driving side, junction and exit angles |
| `0x5203` StopRouteGuidanceUpdates | car → iPhone | stop |
| `0x5204` LaneGuidanceInformation | iPhone → car | lanes, when the component declares lane support |

Cars with iDrive 7 and later use exactly this to show Apple Maps on the HUD. iOS 17.4 opened the same HUD/cluster
metadata to third-party CarPlay apps. Apple Maps sends it. Google Maps is reported to on CarPlay HUDs. Waze support
is unconfirmed. This is metadata only: the HUD keeps drawing its own arrows, and no second video stream is needed.
(CarPlay's separate "instrument cluster video" feature needs a video-capable cluster and is not relevant here.)

NBT EVO's factory CarPlay is older than this. It never declares the component, so the iPhone never sends the data.
Someone has already solved the same problem for Audi/VW MIB2 (QNX 6.5, like NBT EVO):
[WaylandAce/mib2q-carplay-rgi](https://github.com/WaylandAce/mib2q-carplay-rgi). They inject a library
(`LD_PRELOAD`) into the CarPlay iAP2 process that:
1. rewrites the outgoing IdentificationInformation to add the component and the five message ids;
2. sends StartRouteGuidanceUpdates;
3. reads 0x5201/0x5202 from the link and hands them to the HMI, which drives the cluster/HUD in the car's own
   protocol (BAP on VW).

The plan for NBT EVO follows the same pattern. That project has no license, so none of its code is copied. Only the
protocol facts (message and parameter ids) are used, and they still have to be confirmed on this car.

```
iPhone ──iAP2 (Bluetooth / Wi-Fi)──▶ NBT EVO CarPlay iAP2 process ◀── LD_PRELOAD hook (to write)
                                         │  patch Identify, send 0x5200, copy 0x5201/0x5202
                                         ▼
                                   rgd::Tracker  (src/iap2_rgd.hpp, done)
                                         │  next arrow + distance + road
                                         ▼
                        BMW HUD / KOMBI output  (unknown: found with the recon below)
                        = the same path BMW's own navigation uses for its arrows
```

## 3. What is in this folder

| File | What |
|---|---|
| `src/iap2_rgd.hpp` | Header-only, C++98 (builds with the QNX GCC 4.4.2 toolchain). iAP2 link packets (`FF 5A`, both checksums), control-session messages (`40 40`), the **Identify patch**, Start/Stop builders, 0x5201/0x5202/0x5204 decoders, iAP2 maneuver type → neutral arrow (kind, angle, roundabout exit, driving side), and a `Tracker` that reduces the stream to what a HUD shows: arrow, "2.4 km", "3 Thang 2" |
| `tests/test_iap2_rgd.cpp` | Host test: hand-written bytes, Identify patch (order kept, idempotent, lanes optional), link-layer rewrap with new checksums, partial updates, maneuver list switching, arrival, lane decode |
| `tools/rgd_recon.sh` | **Read-only** script for the car shell (writes only to the USB stick). It finds the iAP2 stack, any existing route-guidance code, and KOMBI/HUD/maneuver binaries and configs, copies them for offline reverse engineering, and takes live snapshots (sockets, open files, system log) |

Run the test:

```
cd carplay-hud/tests
g++ -std=c++98 -Wall -Wextra -I../src -o test_iap2_rgd test_iap2_rgd.cpp && ./test_iap2_rgd
```

## 4. Plan

Each step depends on the one before it, and nothing that changes the car comes before step 3.

1. **Recon (needs you and the car).** Copy `tools/rgd_recon.sh` to the stick and run:
   - `sh /mnt/umass*/rgd_recon.sh` with the car simply on (static scan, a few minutes);
   - `sh /mnt/umass*/rgd_recon.sh live` with CarPlay connected and **Apple Maps guiding**;
   - `sh /mnt/umass*/rgd_recon.sh live` with **BMW's own navigation guiding** (arrows on the HUD).

   This answers:
   - Which process runs iAP2 for CarPlay. The best guess is `Connectivity`, which already writes `iap2.cfg`.
   - Whether iAP2 runs over `btspp` or over TCP on the hotspot (`uap0`).
   - Whether the firmware already contains RouteGuidance code. In that case a config change might be enough, as on
     MIB2.
   - Which binaries/services carry BMW's own arrows to the HUD and KOMBI.
2. **Passive tap.** A preload hook that only logs iAP2 control messages to the stick, so the framing and ids can be
   checked against a real Identify. Risk: none, it changes no bytes.
3. **Identify patch + StartRouteGuidanceUpdates.** Use `patchIdentification()` on the outgoing Identify. Sending
   `0x5200` is the delicate part. It is a new message on a link with sequence numbers, so it has to go through the
   stack's own send function (found in step 1's binaries), or the link sequence numbers have to be shifted for the
   rest of the session. Risk: if the iPhone rejects the identification (`0x1D03`), CarPlay won't connect until the
   hook is removed. The hook needs an off switch, like the daemon's `*.disable` files.
4. **BMW output.** Find how the native navigation feeds the HUD and KOMBI and feed the tracker's arrow into that same
   path. The repository already has a generic DSI client (`milestone-7/src/dsi_client.*`) and an observe-only MOST
   client, so comparing traffic with BMW navigation guiding against idle should reveal the right interface. Because
   the arrows then travel the same path as BMW's, they show up wherever BMW's do. Whether the 6WB shows them as well
   as the HUD is then down to the car's coding and HUD settings, not this code.
5. **Hand-off rules.** Native navigation guidance wins over CarPlay. CarPlay guidance stops cleanly when the phone
   disconnects or arrives (`StopRouteGuidanceUpdates`, clear the HUD).

## 5. Target car

| | |
|---|---|
| Car | 2018 BMW 440i Convertible (F33 LCI), built for the US, re-imported to Europe and coded EU (`4Z51` instead of `4Z53`) |
| Head unit | NBT EVO (ID6), software `NBTevo_R191441` (the Android Auto daemon was developed on `NBTevo_T21424I`) |
| Cluster | 6WB: shows BMW navigation **arrows** only, no video map (owner-confirmed) |
| HUD | Factory HUD, shows BMW navigation arrows |
| Access | SSH unlocked; ENET to a laptop for the recon |

What follows from this:
- **No CarPlay map on the cluster.** The 6WB can't show video, so mirroring the CarPlay map there (the iDrive 7/8
  feature) is ruled out on the car side. Getting the iPhone to send a second map stream would be a large job on its
  own anyway. The target is turn arrows, distance and street name on the HUD, and on the 6WB too wherever BMW's own
  arrows appear.
- **US car, EU coding.** The car is coded metric (km/h), so the HUD should show km/m, with right-hand traffic. The
  iPhone picks the units of its route guidance from its own region settings, so they may still arrive in miles, yards
  or feet. All units are decoded (`DU_KM`, `DU_M`, `DU_MILES`, `DU_YARDS`, `DU_FT`); the HUD output (step 4, not written
  yet) has to convert them to the car's units. The US-to-EU recoding may also change market-dependent behaviour on the head unit; the recon should show it.
- **Different software version.** `R191441` is not the version this repository was built and tested on, so the
  process names, libraries and paths from `milestone-7` are hints until the recon confirms them.

Still needed: the recon output from step 1. It includes `iap2.cfg` with your CarPlay hotspot password: fine to send
to me, don't post it publicly.

## 6. Unknowns, stated plainly

- Message and parameter ids are community-documented, not from Apple's MFi spec. The Identify patch must be checked
  against a real capture before it is enabled.
- How NBT EVO sends arrows to the HUD/KOMBI is not known yet. That is step 1's main job.
- Third-party app support for HUD metadata is up to Apple, Google and Waze. If an app doesn't send `0x5201/0x5202`,
  nothing on the car side can show its turns.
