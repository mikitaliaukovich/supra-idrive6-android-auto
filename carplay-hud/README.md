# CarPlay navigation on the BMW HUD / instrument cluster (NBT EVO ID6)

Goal: CarPlay navigation apps (Apple Maps, Google Maps, Waze) show their next turn on the BMW head-up display (and
on the 6WA/6WB cluster if possible), the same way BMW's own navigation does.

Status: **research + groundwork. Nothing here runs on the car yet.** The read-only recon on the car is done
(2026-11-01, results in section 7). This folder holds the protocol code that can be built and tested on a PC, a
read-only recon script for the car, and the plan.

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
iPhone ──iAP2 over TCP on the CarPlay Wi-Fi (uap0)──▶ Connectivity ──/dev/carplayiap──▶ mm-ipod (ipod-drvr-iap2.so)
                                                                                         ▲
                                                               LD_PRELOAD hook (to write)┘
                                         │  patch Identify, send 0x5200, copy 0x5201/0x5202
                                         ▼
                                   rgd::Tracker  (src/iap2_rgd.hpp, done)
                                         │  next arrow + distance + road
                                         ▼
                        BMW HUD / KOMBI output: the path BMW's own navigation uses for its arrows,
                        NavNBTEvoController → "ICHUD" viewer → MOST "HUDTerminal" drawing commands (section 7)
```

## 3. What is in this folder

| File | What |
|---|---|
| `src/iap2_rgd.hpp` | Header-only, C++98 (builds with the QNX GCC 4.4.2 toolchain). iAP2 link packets (`FF 5A`, both checksums), control-session messages (`40 40`), the **Identify patch**, Start/Stop builders, 0x5201/0x5202/0x5204 decoders, iAP2 maneuver type → neutral arrow (kind, angle, roundabout exit, driving side), and a `Tracker` that reduces the stream to what a HUD shows: arrow, "2.4 km", "3 Thang 2" |
| `tests/test_iap2_rgd.cpp` | Host test: hand-written bytes, Identify patch (order kept, idempotent, lanes optional), the patch run on the car's real identification (rebuilt from the recon, checked against the logged sizes and link header), link-layer rewrap with new checksums, partial updates, maneuver list switching, arrival, lane decode |
| `tools/rgd_recon.sh` | **Read-only** script for the car shell (writes only to the USB stick). It finds the iAP2 stack, any existing route-guidance code, and KOMBI/HUD/maneuver binaries and configs, copies them for offline reverse engineering, and takes live snapshots (sockets, open files, system log). Not usable as-is on `hu-omap` (no `tr`, `xargs` or `strings` there, see section 7); the recon ran the same steps over SSH instead |
| `tools/fetch_capture_deps.sh` | Laptop side, **read-only** on the head unit (`find`, `ls`, `cat` over the shared SSH login): copies what the capture plan in section 9 needs into `car/recon/deps_<time>/` and checks each file's size |

Run the test:

```
cd carplay-hud/tests
g++ -std=c++98 -Wall -Wextra -I../src -o test_iap2_rgd test_iap2_rgd.cpp && ./test_iap2_rgd
```

## 4. Plan

Each step depends on the one before it, and nothing that changes the car comes before step 3.

1. **Recon. Done 2026-11-01** (static, CarPlay with Waze guiding, BMW navigation guiding, and the boot log with the
   car's identification). Results in section 7. In short: iAP2 for CarPlay runs in `mm-ipod`, over TCP on `uap0`;
   the firmware has no route-guidance code, so no config change can turn it on; BMW's arrows reach the HUD and
   cluster as MOST "HUDTerminal" drawing commands from `NavNBTEvoController`.
2. **Passive tap.** Partly covered already: `mm-ipod` runs with full verbosity and writes every iAP2 message, decoded,
   to the system log (`sloginfo`), and the real Identify from that log is now a unit test. What is left for this step
   is a hook that only observes, to prove that preloading into `mm-ipod` works (how it is started, whether its
   restart is clean) before any byte is changed. Risk: none, it changes no bytes.
3. **Identify patch + StartRouteGuidanceUpdates.** The hook sits on `mm-ipod`'s reads and writes on
   `/dev/carplayiap`, where the raw link packets (`FF 5A`, control session 1) pass. Use `patchIdentification()` on
   the outgoing Identify. Sending `0x5200` is the delicate part. It is a new message on a link with sequence numbers,
   so it has to go through the stack's own send function (in `ipod-drvr-iap2.so`), or the link sequence numbers have
   to be shifted for the rest of the session. Risk: if the iPhone rejects the identification (`0x1D03`), CarPlay
   won't connect until the hook is removed. The hook needs an off switch, like the daemon's `*.disable` files.
4. **BMW output.** BMW's arrows are not icon codes: `NavNBTEvoController` draws them on the HUD and cluster through
   MOST "HUDTerminal" commands (polygons, circles, bitmaps, text, a distance bar). So the output has to draw an arrow
   from the tracker's data the same way, and Apple's junction and exit angles become useful for drawing side roads.
   The interface is mapped from the copied binary (section 8): logical FBlock `0x82`, 29 named commands with their
   function ids and parameter types. Still to learn: the byte layout of the parameters (number widths, the polygon
   point stream), the instance and session handshake, and which commands BMW sends for one arrow. A capture on the
   car answers that; section 9 has the plan. Because the arrows then travel the same path as BMW's, they show up wherever
   BMW's do. Whether the 6WB shows them as well as the HUD is then down to the car's coding and HUD settings, not
   this code.
5. **Hand-off rules.** Native navigation guidance wins over CarPlay. CarPlay guidance stops cleanly when the phone
   disconnects or arrives (`StopRouteGuidanceUpdates`, clear the HUD).

## 5. Target car

| | |
|---|---|
| Car | 2018 BMW 440i Convertible (F33 LCI), built for the US, re-imported to Europe and coded EU (`4Z51` instead of `4Z53`) |
| Head unit | NBT EVO (ID6), software `NBTevo_R19144I` (the Android Auto daemon was developed on `NBTevo_T21424I`); identifies to the iPhone as `F33-NBTEvo-0716` |
| Cluster | 6WB: shows BMW navigation **arrows** only, no video map (owner-confirmed) |
| HUD | Factory HUD, shows BMW navigation arrows |
| Access | SSH unlocked, root over ENET at `169.254.199.99` (confirmed). SSH lands on node `hu-jacinto`; the main node is `hu-omap`, reached over Qnet with `on -f /net/hu-omap <command>` |

What follows from this:
- **No CarPlay map on the cluster.** The 6WB can't show video, so mirroring the CarPlay map there (the iDrive 7/8
  feature) is ruled out on the car side. Getting the iPhone to send a second map stream would be a large job on its
  own anyway. The target is turn arrows, distance and street name on the HUD, and on the 6WB too wherever BMW's own
  arrows appear.
- **US car, EU coding.** The car is coded metric (km/h), so the HUD should show km/m, with right-hand traffic. The
  iPhone picks the units of its route guidance from its own region settings, so they may still arrive in miles, yards
  or feet. All units are decoded (`DU_KM`, `DU_M`, `DU_MILES`, `DU_YARDS`, `DU_FT`); the HUD output (step 4, not written
  yet) has to convert them to the car's units. The recon saw nothing market-specific in the CarPlay or HUD path
  (the navigation is the combined `eceusa` build).
- **Different software version.** `R19144I` is not the version this repository was built and tested on, so the
  process names, libraries and paths from `milestone-7` are hints only. Those that matter here are confirmed by the
  recon (section 7).

The recon output (binaries, configs, logs) stays on the owner's laptop and is not in the repository: it holds BMW
firmware and `iap2.cfg` with the CarPlay hotspot password.

## 6. Unknowns, stated plainly

- Message and parameter ids are community-documented, not from Apple's MFi spec. The Identify patch now matches the
  real identification's structure (unit test), but only the iPhone can say whether it accepts the result.
- How NBT EVO sends arrows to the HUD/KOMBI: the HUDTerminal commands, ids and parameter types are known (section
  8). Not known yet: the exact bytes of each parameter, whether FBlock `0x82` is remapped at run time, and the
  sequence BMW sends to draw one arrow. A MOST capture on the car answers all three.
- Whether the 6WB gets its arrows over MOST like the HUD, or over the SOME/IP guidance service seen in the recon, is
  open.
- Third-party app support for HUD metadata is up to Apple, Google and Waze. If an app doesn't send `0x5201/0x5202`,
  nothing on the car side can show its turns.

## 7. Recon results (2026-11-01)

Read-only, over SSH (ENET). Nothing was written on the head unit. Runs: static scan, CarPlay connected with no
route, CarPlay with Waze guiding, BMW navigation guiding with and without arrows, and the system log right after a
head-unit restart (it holds the CarPlay identification).

**CarPlay iAP2.**
- The iPhone connects over the CarPlay Wi-Fi (`uap0`, head unit `172.16.224.1`), with TCP sessions on ports 7000,
  5001 and 6030 held by `Connectivity`. No `/dev/btspp*` exists on either node. Bluetooth iAP2 is used only to set up
  wireless CarPlay: the first identification of a session declares a BluetoothTransportComponent, the CarPlay one a
  WiFiTransportComponent.
- `Connectivity` serves the iAP2 stream as `/dev/carplayiap`. QNX's iPod/iAP2 driver `mm-ipod`
  (`/opt/conn/bin/carplay/mm-ipod`, loading `/opt/conn/lib/carplay/ipod-drvr-iap2.so`) opens it, runs the link layer
  and the control session, and publishes the results under `/pps/services/multimedia/iap2_wifi/`. Its configuration
  is `/var/opt/conn/carplay/iap2_Wifi.cfg`, one section per feature.
- `mm-ipod` runs with `-vvvvvvvv -l slog`, so `sloginfo` shows every link packet header and every control message
  decoded, with ids and parameters.
- The CarPlay IdentificationInformation is 870 bytes on control session 1: accessory strings, 24 sent and 16
  received message ids (none in `0x52xx`), 17 External Accessory protocols (`com.bmw.carplay`, `com.bmw.a4a`…), two
  languages, and VehicleInformation (20), LocationInformation (22) and WiFiTransport (24) components. The iPhone
  accepts it (`0x1D02`).

**Route guidance in the firmware.** None. `ipod-drvr-iap2.so` knows identification, now playing, location,
communications, destination, vehicle status, power, HID, media library, app launch, USB audio, External Accessory
and the Wi-Fi/Bluetooth status messages, but nothing about route guidance, and no other CarPlay library mentions it either. So the hook is required; unlike
MIB2, no configuration change can enable it.

**BMW's arrows.**
- `NavigationFully` computes the maneuvers. `NavNBTEvoController` attaches the guidance layers (TDA, RDA, HG/JV,
  lane guidance) to a viewer called `ICHUD`, with separate IC and HUD switches, and its `CIcHudComm` class sends them
  out.
- The output is a remote drawing interface on MOST, "HUDTerminal" (functions `0x200`–`0x22B`, `0x300`,
  `0xC03`–`0xC09`): create instance, add polygon / circle / bitmap, set position and rotation, set text, set a
  "potentiometer" (most likely the distance bar), set the guiding step. Full list in section 8. There are two
  variants, `TF1_3` and `TF11_13`, and `CKombiFBlock00/01` for the cluster.
- The MOST network controller runs on `hu-jacinto` (`dev-InicServer`); the `most` process on `hu-omap` is its
  client. These MOST messages do not appear in the system log.
- The controller also has a SOME/IP client for a guidance service (`RICServiceGuidance`, lane guiding data), and
  `hu-omap` holds SOME/IP sessions to a device at `160.48.199.97`. That may be the cluster; not confirmed.

**Cautions for working on the head unit.**
- `hu-omap` has no `head`, `tr`, `xargs`, `strings`, `which` or `printf`; `grep`, `find`, `sed`, `sort`, `wc`,
  `dd` and `gzip` are there. Longer command sequences go over stdin:
  `ssh … 'on -f /net/hu-omap /bin/sh -s' < script.sh`.
- Never run a system-wide `pidin fds`: it hangs on `io-pkt`. During the recon the HUD and cluster arrows stopped
  appearing until the head unit was restarted, and that hang is the likely cause. `pidin -p <pid> fds` works for
  `mm-ipod` and `Connectivity`, but timed out on `NavNBTEvoController`.

## 8. The HUDTerminal interface (from `NavNBTEvoController`, offline)

Worked out from the binary copied during the recon, without running anything on the car.

- `CHUDTerminalLogDev` is a MOST logical device built on BMW's MOST library (`libimp_sys_most_base_nbt`). It asks
  for **logical FBlock `0x82`** (`CHBComponentInfo::getMappedLogDev(0x82)`), so the FBlock id on the bus is `0x82`
  unless the head unit's component configuration maps it to another one.
- Every command is a MOST method (`CHBLogFuncMethodUnclassified`) whose parameters are typed objects (number,
  bool, string, stream). The table lists the parameter types in the order the method declares them; the method's
  result values are in that list too (`ImageGetSize` returns width and height, `IsVisible` a bool), and the error
  code and error info that nearly every method ends with are left out.
- Names come from the `HUDTerminal.<name>` call objects. Each one is bound to exactly one method in a single setup
  function, and the parameter types fit the names (a bool for `SetVisibility`, a string for `TextareaSetText`, a
  point stream for `ImageAddPolygon`, a 60 KB stream for `ImageAddBitmap`).
- Number types: `n` is unsigned (or of unknown width), `s8` / `s16` / `s32` are signed with the minimum −128 /
  −32768 / −2³¹ declared, `n(a..b)` has a declared range, `stream(≤N)` is a byte stream of at most N bytes.

| Function | Name | Parameters |
|---|---|---|
| `0x200` | CreateInstance | n, n, n, string, n |
| `0x201` | GetInstanceId | n, string, n |
| `0x203` | ReleaseInstance | n |
| `0x204` | SetVisibility | n, bool |
| `0x205` | IsVisible | n, bool |
| `0x20E` | SwitchVisibility | n, n |
| `0x210` | TextareaSetText | n, string, n |
| `0x211` | KOMBITextareaSetFormat | n, n, n, n, n, n |
| `0x215` | ImageSetRotation | n, s16, s16, s16, n |
| `0x218` | ImageSetLineColor | n, n |
| `0x219` | ImageSetLineWidth | n, n |
| `0x21B` | ImageSetFillColor | n, n |
| `0x21C` | ImageSetFillMode | n, n |
| `0x21D` | ImageAddPolygon | n, n, n, n (≥ 1), stream(≤400) |
| `0x21E` | ImageAddCircle | n, n, n, s16, s16, n, s32, s32 |
| `0x21F` | ImageRemoveItem | n, n, n |
| `0x220` | ImageRemoveAllItems | n |
| `0x226` | PotentiometerSetType | n, n, bool |
| `0x227` | PotentiometerSetValue | n, s32, s8 |
| `0x229` | PotentiometerSetRange | n, n, s32, s8, s32, s8, s32, s8 |
| `0x22B` | FrameAddItem | n, n, n, n |
| `0x300` | TerminalInterface | stream(≤10200) |
| `0xC03` | ImageSetTextareaFormat | n, n, n, n, n, n, n |
| `0xC04` | ImageGetSize | n, n, n |
| `0xC05` | ImageAddBitmap | n, n, n, n, n, n, n, stream(≤60000) |
| `0xC06` | ImageSetPositionRotation | n, s16, s16, s16, s16, s16, n |
| `0xC07` | AnimationSetGuidingStep | n, n, n |
| `0xC08` | CurrentGuidingStep | bool, n |
| `0xC09` | ImageAddCircleDiameter | n, n, n, s16, s16, n, s32, s32 |

Three more members of the logical device (`0x247`, `0xC01` and one without a function id) are not used by any
`HUDTerminal.<name>` call.

What this means for step 4: an arrow is a set of images (polygons, circles, bitmaps) on a terminal instance, plus
text areas for the distance and road name and a potentiometer, most likely the distance bar. Not known from the
binary alone: the byte width of each `n`, the layout of the polygon point stream, what the leading numbers are
(instance, image and item handles are the obvious guess), and the order of calls BMW makes for one arrow. A capture
of FBlock `0x82` traffic while BMW navigation guides answers that, and the replay can then be checked against it.

## 9. Capture plan for the HUDTerminal traffic

The capture has to see what `NavNBTEvoController` *sends* to FBlock `0x82`. Three ways were looked at, offline:

| Way | What it sees | What it needs on the car |
|---|---|---|
| Repository MOST client (`milestone-7`, `registerDevCtrls` on `0x82xx`) | Only messages coming in from the HUD/cluster (results, status): it gets copies of incoming messages, not of what another client sends. Good to confirm FBlock `0x82` and its instance | Installing and running a program (a write) |
| BMW's own MOST spy, in the `most` process | Every MOST message passing through the server, both directions: it has `MOSTSpy` / `MOSTBinarySpy` trace channels that dump messages in binary (`trcDumpMostMessageBinary`) | Switching the channel on through BMW's trace framework: per-process trace settings in `/var/opt/sys/persistTrace<process>.bin`, or the HBDebug DSI interface at run time. Output goes to the trace buffers and `/mnt/quota/sys/trace/`. How to switch it on and decode it is not known yet |
| Logging library preloaded into `NavNBTEvoController` | Every HUDTerminal command it sends: they all go through `CHBLogFuncMethodUnclassified::sendAck` in BMW's MOST library (19 of its 28 call sites are in the HUDTerminal code; the function id sits at offset 18 of the object, so other FBlocks can be filtered out) | QNX toolchain, installing the library, restarting the nav controller with it (started from `/opt/sys/etc/starter.cfg`), an off switch |

Steps:
1. **Read-only car visit** (`tools/fetch_capture_deps.sh`, over SSH, nothing written on the head unit): BMW's MOST
   library `libimp_sys_most_base_nbt*.so` (how parameters are packed), the trace libraries and
   `trace-recorder-app`, `starter.cfg`, the FBlock mapping configuration, and listings of `/var/opt/sys` and
   `/mnt/quota/sys/trace/`.
2. **Offline:** parameter packing from the MOST library; how the spy channel is switched on and what its output
   looks like.
3. **On the car, each write approved by the owner:** BMW's spy first (a small, reversible settings change); the
   preloaded logger if the spy can't be used; optionally the repository's MOST client on `0x82` as a check.
4. **Capture** while BMW navigation guides through a few turns, then decode it against the table in section 8.

The preloaded logger and the MOST client both need the QNX 6.5 build setup (`build.sh`: the QNX SDP 6.5 Docker
image and a QNX license file).

## 10. How BMW draws one arrow (offline, from `NavNBTEvoController`'s log strings)

The drawing is split over two processes:

- **`NavigationFully` computes the picture.** Its graphics engine ("NGE") builds the arrow as vector data: polylines
  with the arrow head appended, polygons and circles with their vertices, plus small bitmaps (road icon, junction
  view, RLE32-compressed). That is the "TDA / RDA / HG" guidance data from section 7.
- **`NavNBTEvoController`'s `CIcHudComm` only forwards it.** Per maneuver it logs "triggering command data
  transmission … polylines: N (with V vertices), polygons: N, bitmaps …" and turns each piece into a HUDTerminal
  command: polygon or polyline → `ImageAddPolygon`, circle → `ImageAddCircle` / `ImageAddCircleDiameter`, road
  icon and junction view → `ImageAddBitmap`, street name → `TextareaSetText`, distance and unit → text instances
  (it skips the send when neither changed), distance bar ("Bargraph") → the potentiometer, range 0–255.

The terminal is set up once, by name:

| Layout | Top frames | Instances |
|---|---|---|
| `TF1_3` (older terminal) | `TopFrame`, `TopFrame2`, `TopFrame3` | `Bargraph`, `Bargraph3`, `Distance2/3`, `Unit2/3` |
| `TF11_13` (newer terminal) | 11, 12, 13, 14 | `Guiding_Arrow_11_1/_2`, `Guiding_Arrow_13_1/_2`, `Guiding_Icon_11_x/13_x`, `Distance_1x_1`, `Unit_1x_1`, `Bargraph_1x_1`, `CCP_1x_1/_2` (lane info) |

Set-up order, from the log strings: `CreateInstance` for every named element, `FrameAddItem` to put them into the
top frames, then line colour and width, fill colour and mode, potentiometer type and its range 0–255. While
guiding it switches the visible top frame with the guiding step ("Switching TopFrame … GuidingStep"), turns the
guide arrow and the junction view on and off with `SetVisibility`, and ends every update with a flush to MOST's
packetizer. `navigation.ini` only adds that the distance bar is shown in the maneuver's "main" phase.

What follows for the CarPlay output:
- The HUD and cluster draw vector shapes, so the output must build the arrow itself: a polyline with an arrow
  head for the route, the side roads from Apple's junction angles, a polygon or circle for roundabouts, plus the
  street text, the distance and unit, and the bar.
- Which layout the car's HUD and 6WB use (`TF1_3` or `TF11_13`), the coordinate space (`ImageGetSize` asks for it
  at run time), and the colours are decided at run time. The capture in section 9 shows them.
- An alternative to drawing the shapes ourselves would be to feed CarPlay's maneuver into BMW's own engine in
  `NavigationFully` and let it draw. That boundary (`CRicPiP…ICHUD…` jobs between the two processes) has not been
  looked at yet.
- The controller has a bitmap dump switch (`DumpBitmapsFpk` / `DumpBitmapsOldIc`, to `/var/log/bitmap.rle`) that
  could help the capture; turning it on would be a write on the head unit.
