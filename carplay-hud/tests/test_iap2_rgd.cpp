// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//
// Host test for carplay-hud/src/iap2_rgd.hpp:
//   g++ -std=c++98 -Wall -Wextra -I../src -o test_iap2_rgd test_iap2_rgd.cpp && ./test_iap2_rgd
#include "iap2_rgd.hpp"
#include <stdio.h>
#include <stdlib.h>

using namespace rgd;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static Message parse(const std::vector<uint8_t>& v) {
    Message m;
    size_t n = parseMessage(&v[0], v.size(), m);
    CHECK(n == v.size());
    return m;
}

static void testHandAssembledManeuver() {
    // RouteGuidanceManeuverUpdate written out byte by byte (not via MessageBuilder):
    //   index 3, type 2 (right turn), after-road "3 Thang 2", driving side 0, exit angle 90
    const uint8_t raw[] = {
        0x40, 0x40, 0x00, 0x2A, 0x52, 0x02,
        0x00, 0x06, 0x00, 0x01, 0x00, 0x03,                                      // index = 3
        0x00, 0x05, 0x00, 0x03, 0x02,                                            // type = 2
        0x00, 0x0E, 0x00, 0x04, '3', ' ', 'T', 'h', 'a', 'n', 'g', ' ', '2', 0x00, // after-road
        0x00, 0x05, 0x00, 0x08, 0x00,                                            // driving side = right-hand
        0x00, 0x06, 0x00, 0x0B, 0x00, 0x5A                                       // exit angle = 90
    };
    Message m;
    CHECK(parseMessage(raw, sizeof(raw), m) == sizeof(raw));
    CHECK(m.id == MSG_ROUTE_GUIDANCE_MANEUVER && m.params.size() == 5);
    Maneuver x;
    CHECK(decodeManeuver(m, x));
    CHECK(x.index == 3 && x.type == 2 && x.afterRoad == "3 Thang 2" && x.exitAngle == 90);
    CHECK(x.has(RGM_DRIVING_SIDE) && !x.has(RGM_DESCRIPTION));
    Arrow a = arrowFor(x);
    CHECK(a.kind == AK_TURN && a.angle == 90 && !a.leftHandTraffic);

    // truncated / corrupted inputs are refused
    CHECK(parseMessage(raw, sizeof(raw) - 1, m) == 0);
    uint8_t bad[sizeof(raw)];
    memcpy(bad, raw, sizeof(raw));
    bad[6] = 0x00; bad[7] = 0x02;   // first param claims 2 bytes (< 4-byte header)
    CHECK(parseMessage(bad, sizeof(bad), m) == 0);
}

static std::vector<uint8_t> sampleIdentification() {
    const uint8_t sent[] = { 0x68, 0x00, 0x68, 0x01, 0xAE, 0x00 };     // a few existing ids
    const uint8_t recv[] = { 0x68, 0x02, 0xAE, 0x01 };
    MessageBuilder b(MSG_IDENTIFICATION_INFORMATION);
    b.str(0, "NBT EVO").str(1, "NBTevo").str(2, "BMW").str(3, "123").str(4, "1.0").str(5, "1.0")
     .raw(IDP_MESSAGES_SENT_BY_ACCESSORY, sent, sizeof(sent))
     .raw(IDP_MESSAGES_RECEIVED_FROM_DEVICE, recv, sizeof(recv))
     .u8(8, 0).u16(9, 0).str(12, "en");
    return b.build();
}

static void testIdentificationPatch() {
    std::vector<uint8_t> id = sampleIdentification();
    ComponentConfig cfg;
    std::vector<uint8_t> out;
    CHECK(patchIdentification(&id[0], id.size(), cfg, out) == PATCH_DONE);
    Message m = parse(out);
    CHECK(m.id == MSG_IDENTIFICATION_INFORMATION);
    Message orig = parse(id);
    CHECK(m.params.size() == orig.params.size() + 1);
    for (size_t i = 0; i < orig.params.size(); ++i) CHECK(m.params[i].id == orig.params[i].id);   // order kept

    std::vector<uint16_t> sent, recv;
    const Param* comp = NULL;
    for (size_t i = 0; i < m.params.size(); ++i) {
        if (m.params[i].id == IDP_MESSAGES_SENT_BY_ACCESSORY) pAppend16(m.params[i], sent);
        if (m.params[i].id == IDP_MESSAGES_RECEIVED_FROM_DEVICE) pAppend16(m.params[i], recv);
        if (m.params[i].id == IDP_ROUTE_GUIDANCE_DISPLAY_COMPONENT) comp = &m.params[i];
    }
    CHECK(sent.size() == 5 && sent[3] == 0x5200 && sent[4] == 0x5203 && sent[0] == 0x6800);
    CHECK(recv.size() == 4 && recv[2] == 0x5201 && recv[3] == 0x5202);   // no lanes configured -> no 0x5204
    CHECK(comp != NULL);
    if (comp) {
        std::vector<Param> sub;
        CHECK(parseParams(comp->data, comp->len, sub));
        CHECK(sub.size() == 7 && sub[0].id == RGC_ID && be16(sub[0].data) == cfg.componentId);
        CHECK(sub[1].id == RGC_NAME && pStr(sub[1]) == "BMW HUD");
    }

    // patching twice is a no-op; other messages are not touched
    std::vector<uint8_t> again;
    CHECK(patchIdentification(&out[0], out.size(), cfg, again) == PATCH_ALREADY);
    std::vector<uint8_t> other = MessageBuilder(0x5000).u16(0, 1).build();
    CHECK(patchIdentification(&other[0], other.size(), cfg, again) == PATCH_NOT_IDENTIFICATION);

    // lanes configured -> 0x5204 requested and lane limits declared
    cfg.maxLaneCapacity = 4; cfg.maxLaneDescription = 64;
    CHECK(patchIdentification(&id[0], id.size(), cfg, out) == PATCH_DONE);
    m = parse(out);
    recv.clear();
    for (size_t i = 0; i < m.params.size(); ++i)
        if (m.params[i].id == IDP_MESSAGES_RECEIVED_FROM_DEVICE) pAppend16(m.params[i], recv);
    CHECK(recv.size() == 5 && recv[4] == 0x5204);
}

static void testLinkPacket() {
    LinkPacket lp;
    lp.control = 0x40; lp.seq = 0x2B; lp.ack = 0x11; lp.session = 0x0A;
    lp.payload = sampleIdentification();
    std::vector<uint8_t> w = buildLinkPacket(lp);
    CHECK(w[0] == 0xFF && w[1] == 0x5A && be16(&w[2]) == w.size());
    uint8_t s = 0;
    for (size_t i = 0; i < 9; ++i) s = (uint8_t)(s + w[i]);
    CHECK(s == 0);                                   // header checksum
    s = 0;
    for (size_t i = 9; i < w.size(); ++i) s = (uint8_t)(s + w[i]);
    CHECK(s == 0);                                   // payload checksum
    LinkPacket back;
    CHECK(parseLinkPacket(&w[0], w.size(), back) == w.size());
    CHECK(back.seq == 0x2B && back.ack == 0x11 && back.session == 0x0A && back.payload == lp.payload);
    w[20] ^= 1;
    CHECK(parseLinkPacket(&w[0], w.size(), back) == 0);   // payload checksum catches corruption

    // ACK-only packet (no payload)
    LinkPacket ack;
    ack.control = 0x40; ack.seq = 1; ack.ack = 2; ack.session = 0;
    std::vector<uint8_t> a = buildLinkPacket(ack);
    CHECK(a.size() == 9 && parseLinkPacket(&a[0], a.size(), back) == 9 && back.payload.empty());

    // Identify patch through the link layer: same seq/ack/session, new length + checksums
    LinkPacket in;
    CHECK(parseLinkPacket(&buildLinkPacket(lp)[0], buildLinkPacket(lp).size(), in) > 0);
    std::vector<uint8_t> patched;
    ComponentConfig cfg;
    CHECK(patchIdentification(&in.payload[0], in.payload.size(), cfg, patched) == PATCH_DONE);
    in.payload = patched;
    std::vector<uint8_t> w2 = buildLinkPacket(in);
    CHECK(parseLinkPacket(&w2[0], w2.size(), back) == w2.size() && back.seq == 0x2B && back.payload == patched);
}

static void testStartStop() {
    std::vector<uint8_t> s = buildStartRouteGuidance(0x5242, true, true);
    Message m = parse(s);
    CHECK(m.id == MSG_START_ROUTE_GUIDANCE && m.params.size() == 3);
    CHECK(m.params[0].id == RGS_COMPONENT_ID && be16(m.params[0].data) == 0x5242);
    CHECK(m.params[1].len == 0 && m.params[2].len == 0);
    m = parse(buildStopRouteGuidance(0x5242));
    CHECK(m.id == MSG_STOP_ROUTE_GUIDANCE && m.params.size() == 1);
}

static std::vector<uint8_t> maneuverMsg(uint16_t idx, uint8_t type, const char* after, bool lht = false) {
    MessageBuilder b(MSG_ROUTE_GUIDANCE_MANEUVER);
    b.u16(RGM_COMPONENT_ID, 0x5242).u16(RGM_INDEX, idx).u8(RGM_TYPE, type).str(RGM_AFTER_ROAD, after)
     .u8(RGM_DRIVING_SIDE, lht ? 1 : 0);
    return b.build();
}

static void testTracker() {
    Tracker t;
    CHECK(!t.hud().active);

    // maneuvers arrive before the route update that points at them
    // (no route state yet, so they are cached but the HUD stays inactive)
    t.feed(parse(maneuverMsg(0, 11, "Nguyen Trai")));
    t.feed(parse(maneuverMsg(1, 1, "3 Thang 2")));
    t.feed(parse(maneuverMsg(2, 30, "Ly Thuong Kiet")));
    CHECK(!t.hud().active && t.hud().arrow.kind == AK_NONE);

    MessageBuilder u(MSG_ROUTE_GUIDANCE_UPDATE);
    const uint8_t list[] = { 0x00, 0x01, 0x00, 0x02 };
    u.u16(RGU_COMPONENT_ID, 0x5242).u8(RGU_STATE, RS_ROUTE_SET).u8(RGU_MANEUVER_STATE, 1)
     .str(RGU_CURRENT_ROAD, "Nguyen Trai").str(RGU_DESTINATION, "Home").u64(RGU_ETA, 1798000000000ull)
     .u64(RGU_TIME_REMAINING, 840).u32(RGU_DISTANCE_REMAINING, 7300).str(RGU_DISTANCE_STRING, "7.3")
     .u8(RGU_DISTANCE_UNITS, DU_KM).u32(RGU_DIST_TO_MANEUVER, 2400).str(RGU_DIST_TO_MANEUVER_STRING, "2.4")
     .u8(RGU_DIST_TO_MANEUVER_UNITS, DU_KM).raw(RGU_MANEUVER_LIST, list, sizeof(list)).u16(RGU_MANEUVER_COUNT, 3)
     .u8(RGU_VISIBLE_IN_APP, 1).str(RGU_SOURCE_NAME, "Google Maps");
    std::vector<uint8_t> ubytes = u.build();   // Message points into it: keep it alive
    Message um = parse(ubytes);
    RouteUpdate ru;
    CHECK(decodeRouteUpdate(um, ru));
    CHECK(ru.eta == 1798000000000ull && ru.timeRemaining == 840 && ru.distanceRemaining == 7300);
    CHECK(ru.maneuverList.size() == 2 && ru.maneuverList[0] == 1 && ru.sourceName == "Google Maps");
    CHECK(ru.has(RGU_ETA) && !ru.has(RGU_LANE_INDEX));

    CHECK(t.feed(um));
    HudState h = t.hud();
    CHECK(h.active && h.arrow.kind == AK_TURN && h.arrow.angle == -90);
    CHECK(h.distance == "2.4" && h.distanceUnits == DU_KM && h.distanceMeters == 2400);
    CHECK(h.road == "3 Thang 2" && h.destination == "Home");

    // partial update: only the distance changes
    MessageBuilder d(MSG_ROUTE_GUIDANCE_UPDATE);
    d.u32(RGU_DIST_TO_MANEUVER, 150).str(RGU_DIST_TO_MANEUVER_STRING, "150").u8(RGU_DIST_TO_MANEUVER_UNITS, DU_M);
    CHECK(t.feed(parse(d.build())));
    CHECK(t.hud().distance == "150" && t.hud().distanceUnits == DU_M && t.hud().arrow.kind == AK_TURN);
    CHECK(!t.feed(parse(d.build())));   // identical update: no change reported

    // next maneuver: 2nd roundabout... exit 3 (type 30)
    MessageBuilder n(MSG_ROUTE_GUIDANCE_UPDATE);
    const uint8_t list2[] = { 0x00, 0x02 };
    n.raw(RGU_MANEUVER_LIST, list2, sizeof(list2));
    CHECK(t.feed(parse(n.build())));
    CHECK(t.hud().arrow.kind == AK_ROUNDABOUT && t.hud().arrow.roundaboutExit == 3 && t.hud().road == "Ly Thuong Kiet");

    // arrived -> inactive, arrow cleared
    CHECK(t.feed(parse(MessageBuilder(MSG_ROUTE_GUIDANCE_UPDATE).u8(RGU_STATE, RS_ARRIVED).build())));
    CHECK(!t.hud().active && t.hud().arrow.kind == AK_NONE);
}

static void testArrowTable() {
    Maneuver m;
    m.present = 1u << RGM_TYPE;
    m.type = 4;   CHECK(arrowFor(m).kind == AK_U_TURN && arrowFor(m).angle == -180);
    m.type = 49;  CHECK(arrowFor(m).kind == AK_TURN && arrowFor(m).angle == -45);
    m.type = 48;  CHECK(arrowFor(m).kind == AK_TURN && arrowFor(m).angle == 135);
    m.type = 23;  CHECK(arrowFor(m).kind == AK_EXIT_RAMP && arrowFor(m).angle == 30);
    m.type = 13;  CHECK(arrowFor(m).kind == AK_KEEP && arrowFor(m).angle < 0);
    m.type = 28;  CHECK(arrowFor(m).kind == AK_ROUNDABOUT && arrowFor(m).roundaboutExit == 1);
    m.type = 46;  CHECK(arrowFor(m).roundaboutExit == 19);
    m.type = 12;  CHECK(arrowFor(m).kind == AK_DESTINATION);
    m.type = 200; CHECK(arrowFor(m).kind == AK_NONE);
    // left-hand traffic U-turn goes the other way
    m.type = 4; m.drivingSide = 1; m.present |= 1u << RGM_DRIVING_SIDE;
    CHECK(arrowFor(m).angle == 180 && arrowFor(m).leftHandTraffic);
    // exit angle refines a turn, normalised to -180..180
    m.type = 2; m.exitAngle = 300; m.present |= 1u << RGM_EXIT_ANGLE;
    CHECK(arrowFor(m).angle == -60);
}

static void testLaneGuidance() {
    MessageBuilder lane(0);
    const uint8_t angles[] = { 0x00, 0x00, 0x00, 0x2D };   // 0 and 45 degrees
    lane.u16(LANE_POSITION, 1).u8(LANE_STATUS, 2).raw(LANE_ANGLE, angles, sizeof(angles)).u16(LANE_HIGHLIGHT_ANGLE, 45);
    MessageBuilder b(MSG_LANE_GUIDANCE_INFORMATION);
    b.u16(RGL_COMPONENT_ID, 0x5242).u16(RGL_INDEX, 7).group(RGL_LANES, lane.paramsOnly())
     .group(RGL_LANES, MessageBuilder(0).u16(LANE_POSITION, 2).u8(LANE_STATUS, 0).paramsOnly())
     .str(RGL_DESCRIPTION, "Use the right lane");
    LaneGuidance g;
    CHECK(decodeLaneGuidance(parse(b.build()), g));
    CHECK(g.index == 7 && g.lanes.size() == 2 && g.description == "Use the right lane");
    CHECK(g.lanes[0].status == 2 && g.lanes[0].angles.size() == 2 && g.lanes[0].angles[1] == 45 && g.lanes[0].highlight == 45);
    CHECK(g.lanes[1].position == 2 && g.lanes[1].highlight == 1000);
}

int main() {
    testHandAssembledManeuver();
    testIdentificationPatch();
    testLinkPacket();
    testStartStop();
    testTracker();
    testArrowTable();
    testLaneGuidance();
    if (g_fail) { printf("%d check(s) FAILED\n", g_fail); return 1; }
    printf("test_iap2_rgd: all checks passed\n");
    return 0;
}
