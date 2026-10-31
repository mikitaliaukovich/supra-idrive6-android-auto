// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//
// iAP2 Route Guidance Display (RGD) for CarPlay -> BMW HUD / instrument cluster.
//
// Header-only, no vendor headers, C++03 subset (QNX GCC 4.4.2: no range-for, no nullptr, no atomics).
// Covers the protocol side only, which can be built and tested on a PC:
//   - iAP2 link-layer packet parse/build (0xFF 0x5A ... + checksums)
//   - iAP2 control-session message parse/build (0x40 0x40, BE16 length, BE16 id, params)
//   - IdentificationInformation (0x1D01) patch: declare a RouteGuidanceDisplayComponent (param 0x1E) and add
//     0x5200/0x5203 to MessagesSentByAccessory, 0x5201/0x5202/0x5204 to MessagesReceivedFromDevice. Without
//     this iOS never sends route guidance to the car.
//   - StartRouteGuidanceUpdates (0x5200) / StopRouteGuidanceUpdates (0x5203) builders
//   - RouteGuidanceUpdate (0x5201), RouteGuidanceManeuverUpdate (0x5202), LaneGuidanceInformation (0x5204) decode
//   - mapping of the iAP2 maneuver type to a neutral arrow description (direction + angle + roundabout exit) that
//     the BMW-side output (HUD / KOMBI) is then mapped from.
//
// Message and parameter ids are not from Apple's (MFi-confidential) spec: they are community-documented
// (Wireshark iAP2 dissector; github.com/WaylandAce/mib2q-carplay-rgi for the route-guidance ids) and must be confirmed against a capture
// from the car before anything is written into the iAP2 link. Integers are big-endian, strings UTF-8 + NUL.
#ifndef IAP2_RGD_HPP
#define IAP2_RGD_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

namespace rgd {

// ---------------------------------------------------------------------------------------------------------------
// ids
// ---------------------------------------------------------------------------------------------------------------
enum {
    MSG_START_IDENTIFICATION       = 0x1D00,
    MSG_IDENTIFICATION_INFORMATION = 0x1D01,
    MSG_IDENTIFICATION_ACCEPTED    = 0x1D02,
    MSG_IDENTIFICATION_REJECTED    = 0x1D03,

    MSG_START_ROUTE_GUIDANCE       = 0x5200,   // accessory -> device
    MSG_ROUTE_GUIDANCE_UPDATE      = 0x5201,   // device -> accessory
    MSG_ROUTE_GUIDANCE_MANEUVER    = 0x5202,   // device -> accessory
    MSG_STOP_ROUTE_GUIDANCE        = 0x5203,   // accessory -> device
    MSG_LANE_GUIDANCE_INFORMATION  = 0x5204    // device -> accessory (only when the component declares lanes)
};

// IdentificationInformation parameters used here.
enum {
    IDP_MESSAGES_SENT_BY_ACCESSORY      = 6,
    IDP_MESSAGES_RECEIVED_FROM_DEVICE   = 7,
    IDP_ROUTE_GUIDANCE_DISPLAY_COMPONENT = 0x1E
};

// RouteGuidanceDisplayComponent sub-parameters.
enum {
    RGC_ID = 0, RGC_NAME = 1, RGC_MAX_CURRENT_ROAD_NAME = 2, RGC_MAX_DESTINATION_NAME = 3,
    RGC_MAX_AFTER_MANEUVER_ROAD_NAME = 4, RGC_MAX_MANEUVER_DESCRIPTION = 5, RGC_MAX_MANEUVER_CAPACITY = 6,
    RGC_MAX_LANE_DESCRIPTION = 7, RGC_MAX_LANE_CAPACITY = 8
};

// StartRouteGuidanceUpdates parameters.
enum { RGS_COMPONENT_ID = 0, RGS_SOURCE_NAME = 1, RGS_SOURCE_SUPPORTS_RG = 2, RGS_SUPPORTS_EXIT_INFO = 3 };

// RouteGuidanceUpdate parameters.
enum {
    RGU_COMPONENT_ID = 0, RGU_STATE = 1, RGU_MANEUVER_STATE = 2, RGU_CURRENT_ROAD = 3, RGU_DESTINATION = 4,
    RGU_ETA = 5, RGU_TIME_REMAINING = 6, RGU_DISTANCE_REMAINING = 7, RGU_DISTANCE_STRING = 8,
    RGU_DISTANCE_UNITS = 9, RGU_DIST_TO_MANEUVER = 10, RGU_DIST_TO_MANEUVER_STRING = 11,
    RGU_DIST_TO_MANEUVER_UNITS = 12, RGU_MANEUVER_LIST = 13, RGU_MANEUVER_COUNT = 14, RGU_VISIBLE_IN_APP = 15,
    RGU_LANE_INDEX = 16, RGU_LANE_TOTAL = 17, RGU_LANE_SHOWING = 18, RGU_SOURCE_NAME = 19,
    RGU_SOURCE_SUPPORTS_RG = 20
};

// RouteGuidanceManeuverUpdate parameters.
enum {
    RGM_COMPONENT_ID = 0, RGM_INDEX = 1, RGM_DESCRIPTION = 2, RGM_TYPE = 3, RGM_AFTER_ROAD = 4,
    RGM_DISTANCE_BETWEEN = 5, RGM_DISTANCE_STRING = 6, RGM_DISTANCE_UNITS = 7, RGM_DRIVING_SIDE = 8,
    RGM_JUNCTION_TYPE = 9, RGM_JUNCTION_ANGLE = 10, RGM_EXIT_ANGLE = 11, RGM_LINKED_LANE = 12, RGM_EXIT_INFO = 13
};

// LaneGuidanceInformation parameters / lane sub-parameters.
enum { RGL_COMPONENT_ID = 0, RGL_INDEX = 1, RGL_LANES = 2, RGL_DESCRIPTION = 3 };
enum { LANE_POSITION = 0, LANE_STATUS = 1, LANE_ANGLE = 2, LANE_HIGHLIGHT_ANGLE = 3 };

enum RouteState { RS_NO_ROUTE = 0, RS_ROUTE_SET = 1, RS_ARRIVED = 2, RS_LOADING = 3, RS_LOCATING = 4,
                  RS_REROUTING = 5, RS_PROCEED_TO_ROUTE = 6 };
enum DistUnit { DU_KM = 0, DU_MILES = 1, DU_M = 2, DU_YARDS = 3, DU_FT = 4 };

// ---------------------------------------------------------------------------------------------------------------
// byte helpers
// ---------------------------------------------------------------------------------------------------------------
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint64_t be64(const uint8_t* p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
inline void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
inline void set16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = (uint8_t)(x >> 8); v[at + 1] = (uint8_t)x; }

// ---------------------------------------------------------------------------------------------------------------
// control-session message: 0x40 0x40 | BE16 total length | BE16 message id | params
// param: BE16 length (incl. its 4-byte header) | BE16 id | data      (groups nest params in data)
// ---------------------------------------------------------------------------------------------------------------
struct Param {
    uint16_t id;
    const uint8_t* data;
    size_t len;
};

// Splits a parameter block. Returns false on a malformed block (params before the error are kept).
inline bool parseParams(const uint8_t* p, size_t len, std::vector<Param>& out) {
    size_t off = 0;
    while (off + 4 <= len) {
        uint16_t plen = be16(p + off);
        if (plen < 4 || off + plen > len) return false;
        Param x;
        x.id = be16(p + off + 2);
        x.data = p + off + 4;
        x.len = plen - 4;
        out.push_back(x);
        off += plen;
    }
    return off == len;
}

// The params point into the buffer given to parseMessage(): keep that buffer alive while the Message is used.
struct Message {
    uint16_t id;
    std::vector<Param> params;
};

// Parses one control-session message at p. Returns its total length, or 0 if p doesn't hold a whole message.
inline size_t parseMessage(const uint8_t* p, size_t len, Message& m) {
    if (len < 6 || p[0] != 0x40 || p[1] != 0x40) return 0;
    uint16_t mlen = be16(p + 2);
    if (mlen < 6 || mlen > len) return 0;
    m.id = be16(p + 4);
    m.params.clear();
    if (!parseParams(p + 6, mlen - 6, m.params)) return 0;
    return mlen;
}

class MessageBuilder {
public:
    explicit MessageBuilder(uint16_t id) { buf_.push_back(0x40); buf_.push_back(0x40); put16(buf_, 0); put16(buf_, id); }
    MessageBuilder& raw(uint16_t id, const uint8_t* d, size_t n) {
        put16(buf_, (uint16_t)(n + 4)); put16(buf_, id); buf_.insert(buf_.end(), d, d + n); return *this;
    }
    MessageBuilder& u8(uint16_t id, uint8_t x) { return raw(id, &x, 1); }
    MessageBuilder& u16(uint16_t id, uint16_t x) { uint8_t b[2] = { (uint8_t)(x >> 8), (uint8_t)x }; return raw(id, b, 2); }
    MessageBuilder& u32(uint16_t id, uint32_t x) {
        uint8_t b[4] = { (uint8_t)(x >> 24), (uint8_t)(x >> 16), (uint8_t)(x >> 8), (uint8_t)x }; return raw(id, b, 4);
    }
    MessageBuilder& u64(uint16_t id, uint64_t x) {
        uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(x >> (56 - 8 * i));
        return raw(id, b, 8);
    }
    MessageBuilder& str(uint16_t id, const char* s) { return raw(id, (const uint8_t*)s, strlen(s) + 1); }
    MessageBuilder& none(uint16_t id) { return raw(id, NULL, 0); }
    MessageBuilder& group(uint16_t id, const std::vector<uint8_t>& params) {
        return raw(id, params.empty() ? NULL : &params[0], params.size());
    }
    std::vector<uint8_t> build() const { std::vector<uint8_t> v = buf_; set16(v, 2, (uint16_t)v.size()); return v; }
    // Parameter bytes only (for nesting into a group).
    std::vector<uint8_t> paramsOnly() const { return std::vector<uint8_t>(buf_.begin() + 6, buf_.end()); }
private:
    std::vector<uint8_t> buf_;
};

inline std::string pStr(const Param& p) {
    size_t n = 0;
    while (n < p.len && p.data[n]) ++n;
    return std::string((const char*)p.data, n);
}
inline bool pU8(const Param& p, uint8_t& x) { if (p.len < 1) return false; x = p.data[0]; return true; }
inline bool pU16(const Param& p, uint16_t& x) { if (p.len < 2) return false; x = be16(p.data); return true; }
inline bool pU32(const Param& p, uint32_t& x) { if (p.len < 4) return false; x = be32(p.data); return true; }
inline bool pU64(const Param& p, uint64_t& x) { if (p.len < 8) return false; x = be64(p.data); return true; }
// A list of BE16 values: both a packed array in one param and one value per repeated param are seen.
inline void pAppend16(const Param& p, std::vector<uint16_t>& out) {
    for (size_t i = 0; i + 1 < p.len; i += 2) out.push_back(be16(p.data + i));
}

// ---------------------------------------------------------------------------------------------------------------
// iAP2 link layer: FF 5A | BE16 packet length | control | seq | ack | session | header checksum | payload | payload cs
// Checksums are the two's complement of the byte sum (all covered bytes + checksum == 0 mod 256).
// ---------------------------------------------------------------------------------------------------------------
inline uint8_t iap2Checksum(const uint8_t* p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; ++i) s = (uint8_t)(s + p[i]);
    return (uint8_t)(0x100 - s);
}

struct LinkPacket {
    uint8_t control, seq, ack, session;
    std::vector<uint8_t> payload;
};

// Parses one link packet at p. Returns its length (0 = incomplete or not a packet; checksums are verified).
inline size_t parseLinkPacket(const uint8_t* p, size_t len, LinkPacket& lp) {
    if (len < 9 || p[0] != 0xFF || p[1] != 0x5A) return 0;
    uint16_t plen = be16(p + 2);
    if (plen < 9 || plen > len || plen == 10) return 0;
    if (iap2Checksum(p, 8) != p[8]) return 0;
    lp.control = p[4]; lp.seq = p[5]; lp.ack = p[6]; lp.session = p[7];
    lp.payload.clear();
    if (plen > 9) {
        size_t n = plen - 10;
        if (iap2Checksum(p + 9, n) != p[9 + n]) return 0;
        lp.payload.assign(p + 9, p + 9 + n);
    }
    return plen;
}

inline std::vector<uint8_t> buildLinkPacket(const LinkPacket& lp) {
    std::vector<uint8_t> v;
    v.push_back(0xFF); v.push_back(0x5A);
    put16(v, (uint16_t)(9 + (lp.payload.empty() ? 0 : lp.payload.size() + 1)));
    v.push_back(lp.control); v.push_back(lp.seq); v.push_back(lp.ack); v.push_back(lp.session);
    v.push_back(iap2Checksum(&v[0], 8));
    if (!lp.payload.empty()) {
        v.insert(v.end(), lp.payload.begin(), lp.payload.end());
        v.push_back(iap2Checksum(&lp.payload[0], lp.payload.size()));
    }
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// Identification patch
// ---------------------------------------------------------------------------------------------------------------
struct ComponentConfig {
    uint16_t componentId;
    const char* name;
    uint16_t maxRoadName, maxDestinationName, maxAfterManeuverRoadName, maxManeuverDescription;
    uint16_t maxManeuverCapacity;
    uint16_t maxLaneDescription, maxLaneCapacity;   // 0 = no lane guidance (no 0x5204)
    ComponentConfig()
        : componentId(0x5242 /* 'RB' */), name("BMW HUD"), maxRoadName(64), maxDestinationName(64),
          maxAfterManeuverRoadName(64), maxManeuverDescription(128), maxManeuverCapacity(8),
          maxLaneDescription(0), maxLaneCapacity(0) {}
};

inline std::vector<uint8_t> buildRouteGuidanceComponent(const ComponentConfig& c) {
    MessageBuilder g(0);
    g.u16(RGC_ID, c.componentId).str(RGC_NAME, c.name).u16(RGC_MAX_CURRENT_ROAD_NAME, c.maxRoadName)
     .u16(RGC_MAX_DESTINATION_NAME, c.maxDestinationName).u16(RGC_MAX_AFTER_MANEUVER_ROAD_NAME, c.maxAfterManeuverRoadName)
     .u16(RGC_MAX_MANEUVER_DESCRIPTION, c.maxManeuverDescription).u16(RGC_MAX_MANEUVER_CAPACITY, c.maxManeuverCapacity);
    if (c.maxLaneCapacity) g.u16(RGC_MAX_LANE_DESCRIPTION, c.maxLaneDescription).u16(RGC_MAX_LANE_CAPACITY, c.maxLaneCapacity);
    return g.paramsOnly();
}

enum PatchResult { PATCH_NOT_IDENTIFICATION = 0, PATCH_ALREADY = 1, PATCH_DONE = 2, PATCH_MALFORMED = -1 };

// Rewrites an IdentificationInformation message (one whole control-session message) so that it declares route
// guidance. Parameters are kept in their original order; the message-id lists are extended in place and the
// component is appended at the end. An identification that already declares a component is left alone.
inline PatchResult patchIdentification(const uint8_t* msg, size_t len, const ComponentConfig& cfg,
                                       std::vector<uint8_t>& out) {
    Message m;
    size_t mlen = parseMessage(msg, len, m);
    if (!mlen) return PATCH_MALFORMED;
    if (m.id != MSG_IDENTIFICATION_INFORMATION) return PATCH_NOT_IDENTIFICATION;
    bool haveSent = false, haveRecv = false;
    for (size_t i = 0; i < m.params.size(); ++i) {
        if (m.params[i].id == IDP_ROUTE_GUIDANCE_DISPLAY_COMPONENT) return PATCH_ALREADY;
        if (m.params[i].id == IDP_MESSAGES_SENT_BY_ACCESSORY) haveSent = true;
        if (m.params[i].id == IDP_MESSAGES_RECEIVED_FROM_DEVICE) haveRecv = true;
    }
    if (!haveSent || !haveRecv) return PATCH_MALFORMED;   // both are mandatory in a real identification

    static const uint16_t kSent[] = { MSG_START_ROUTE_GUIDANCE, MSG_STOP_ROUTE_GUIDANCE };
    static const uint16_t kRecv[] = { MSG_ROUTE_GUIDANCE_UPDATE, MSG_ROUTE_GUIDANCE_MANEUVER, MSG_LANE_GUIDANCE_INFORMATION };
    MessageBuilder b(MSG_IDENTIFICATION_INFORMATION);
    for (size_t i = 0; i < m.params.size(); ++i) {
        const Param& p = m.params[i];
        if (p.id == IDP_MESSAGES_SENT_BY_ACCESSORY || p.id == IDP_MESSAGES_RECEIVED_FROM_DEVICE) {
            std::vector<uint16_t> ids;
            pAppend16(p, ids);
            const uint16_t* add = p.id == IDP_MESSAGES_SENT_BY_ACCESSORY ? kSent : kRecv;
            size_t nadd = p.id == IDP_MESSAGES_SENT_BY_ACCESSORY ? 2 : (cfg.maxLaneCapacity ? 3 : 2);
            for (size_t k = 0; k < nadd; ++k) {
                bool dup = false;
                for (size_t j = 0; j < ids.size(); ++j) dup = dup || ids[j] == add[k];
                if (!dup) ids.push_back(add[k]);
            }
            std::vector<uint8_t> d;
            for (size_t j = 0; j < ids.size(); ++j) put16(d, ids[j]);
            b.raw(p.id, d.empty() ? NULL : &d[0], d.size());
        } else {
            b.raw(p.id, p.data, p.len);
        }
    }
    b.group(IDP_ROUTE_GUIDANCE_DISPLAY_COMPONENT, buildRouteGuidanceComponent(cfg));
    out = b.build();
    if (out.size() > 0xFFFF) return PATCH_MALFORMED;
    return PATCH_DONE;
}

inline std::vector<uint8_t> buildStartRouteGuidance(uint16_t componentId, bool sourceName, bool sourceSupportsRg) {
    MessageBuilder b(MSG_START_ROUTE_GUIDANCE);
    b.u16(RGS_COMPONENT_ID, componentId);
    if (sourceName) b.none(RGS_SOURCE_NAME);
    if (sourceSupportsRg) b.none(RGS_SOURCE_SUPPORTS_RG);
    return b.build();
}

inline std::vector<uint8_t> buildStopRouteGuidance(uint16_t componentId) {
    return MessageBuilder(MSG_STOP_ROUTE_GUIDANCE).u16(RGS_COMPONENT_ID, componentId).build();
}

// ---------------------------------------------------------------------------------------------------------------
// decoded route guidance
// ---------------------------------------------------------------------------------------------------------------
struct RouteUpdate {
    uint32_t present;                         // bit n = parameter n was in the message (updates are partial)
    std::vector<uint16_t> componentIds;
    uint8_t state, maneuverState;
    std::string currentRoad, destination;
    uint64_t eta, timeRemaining;
    uint32_t distanceRemaining;
    std::string distanceString;
    uint8_t distanceUnits;
    uint32_t distToManeuver;
    std::string distToManeuverString;
    uint8_t distToManeuverUnits;
    std::vector<uint16_t> maneuverList;       // indices of the current maneuver(s), first = next
    uint16_t maneuverCount;
    uint8_t visibleInApp;
    std::string sourceName;
    RouteUpdate() : present(0), state(0), maneuverState(0), eta(0), timeRemaining(0), distanceRemaining(0),
                    distanceUnits(0), distToManeuver(0), distToManeuverUnits(0), maneuverCount(0), visibleInApp(0) {}
    bool has(int param) const { return (present >> param) & 1u; }
};

struct Maneuver {
    uint32_t present;
    std::vector<uint16_t> componentIds;
    uint16_t index;
    std::string description, afterRoad, distanceString;
    uint8_t type;
    uint32_t distanceBetween;
    uint8_t distanceUnits;
    uint8_t drivingSide;                      // 0 right-hand traffic, 1 left-hand traffic
    uint8_t junctionType;
    std::vector<int16_t> junctionAngles;      // other roads at the junction, degrees
    int16_t exitAngle;                        // the road to take, degrees
    uint16_t linkedLane;
    std::string exitInfo;
    Maneuver() : present(0), index(0), type(0xFF), distanceBetween(0), distanceUnits(0), drivingSide(0),
                 junctionType(0), exitAngle(0), linkedLane(0xFFFF) {}
    bool has(int param) const { return (present >> param) & 1u; }
};

struct Lane {
    uint16_t position;
    uint8_t status;                           // 0 not good, 1 good, 2 preferred
    std::vector<int16_t> angles;
    int16_t highlight;
    Lane() : position(0), status(0), highlight(1000) {}
};

struct LaneGuidance {
    uint16_t index;
    std::vector<Lane> lanes;
    std::string description;
    LaneGuidance() : index(0) {}
};

inline bool decodeRouteUpdate(const Message& m, RouteUpdate& u) {
    if (m.id != MSG_ROUTE_GUIDANCE_UPDATE) return false;
    u = RouteUpdate();
    for (size_t i = 0; i < m.params.size(); ++i) {
        const Param& p = m.params[i];
        bool ok = true;
        switch (p.id) {
            case RGU_COMPONENT_ID:            pAppend16(p, u.componentIds); break;
            case RGU_STATE:                   ok = pU8(p, u.state); break;
            case RGU_MANEUVER_STATE:          ok = pU8(p, u.maneuverState); break;
            case RGU_CURRENT_ROAD:            u.currentRoad = pStr(p); break;
            case RGU_DESTINATION:             u.destination = pStr(p); break;
            case RGU_ETA:                     ok = pU64(p, u.eta); break;
            case RGU_TIME_REMAINING:          ok = pU64(p, u.timeRemaining); break;
            case RGU_DISTANCE_REMAINING:      ok = pU32(p, u.distanceRemaining); break;
            case RGU_DISTANCE_STRING:         u.distanceString = pStr(p); break;
            case RGU_DISTANCE_UNITS:          ok = pU8(p, u.distanceUnits); break;
            case RGU_DIST_TO_MANEUVER:        ok = pU32(p, u.distToManeuver); break;
            case RGU_DIST_TO_MANEUVER_STRING: u.distToManeuverString = pStr(p); break;
            case RGU_DIST_TO_MANEUVER_UNITS:  ok = pU8(p, u.distToManeuverUnits); break;
            case RGU_MANEUVER_LIST:           pAppend16(p, u.maneuverList); break;
            case RGU_MANEUVER_COUNT:          ok = pU16(p, u.maneuverCount); break;
            case RGU_VISIBLE_IN_APP:          ok = pU8(p, u.visibleInApp); break;
            case RGU_SOURCE_NAME:             u.sourceName = pStr(p); break;
            default:                          ok = p.id < 32; break;
        }
        if (ok && p.id < 32) u.present |= 1u << p.id;
    }
    return true;
}

inline bool decodeManeuver(const Message& m, Maneuver& x) {
    if (m.id != MSG_ROUTE_GUIDANCE_MANEUVER) return false;
    x = Maneuver();
    for (size_t i = 0; i < m.params.size(); ++i) {
        const Param& p = m.params[i];
        bool ok = true;
        uint16_t v = 0;
        switch (p.id) {
            case RGM_COMPONENT_ID:      pAppend16(p, x.componentIds); break;
            case RGM_INDEX:             ok = pU16(p, x.index); break;
            case RGM_DESCRIPTION:       x.description = pStr(p); break;
            case RGM_TYPE:              ok = pU8(p, x.type); break;
            case RGM_AFTER_ROAD:        x.afterRoad = pStr(p); break;
            case RGM_DISTANCE_BETWEEN:  ok = pU32(p, x.distanceBetween); break;
            case RGM_DISTANCE_STRING:   x.distanceString = pStr(p); break;
            case RGM_DISTANCE_UNITS:    ok = pU8(p, x.distanceUnits); break;
            case RGM_DRIVING_SIDE:      ok = pU8(p, x.drivingSide); break;
            case RGM_JUNCTION_TYPE:     ok = pU8(p, x.junctionType); break;
            case RGM_JUNCTION_ANGLE: {
                std::vector<uint16_t> a;
                pAppend16(p, a);
                for (size_t k = 0; k < a.size(); ++k) x.junctionAngles.push_back((int16_t)a[k]);
                break;
            }
            case RGM_EXIT_ANGLE:        ok = pU16(p, v); x.exitAngle = (int16_t)v; break;
            case RGM_LINKED_LANE:       ok = pU16(p, x.linkedLane); break;
            case RGM_EXIT_INFO:         x.exitInfo = pStr(p); break;
            default:                    break;
        }
        if (ok && p.id < 32) x.present |= 1u << p.id;
    }
    return true;
}

inline bool decodeLaneGuidance(const Message& m, LaneGuidance& g) {
    if (m.id != MSG_LANE_GUIDANCE_INFORMATION) return false;
    g = LaneGuidance();
    for (size_t i = 0; i < m.params.size(); ++i) {
        const Param& p = m.params[i];
        if (p.id == RGL_INDEX) pU16(p, g.index);
        else if (p.id == RGL_DESCRIPTION) g.description = pStr(p);
        else if (p.id == RGL_LANES) {
            std::vector<Param> sub;
            parseParams(p.data, p.len, sub);
            Lane l;
            for (size_t k = 0; k < sub.size(); ++k) {
                uint16_t v = 0;
                if (sub[k].id == LANE_POSITION) pU16(sub[k], l.position);
                else if (sub[k].id == LANE_STATUS) pU8(sub[k], l.status);
                else if (sub[k].id == LANE_ANGLE) {
                    std::vector<uint16_t> a;
                    pAppend16(sub[k], a);
                    for (size_t j = 0; j < a.size(); ++j) l.angles.push_back((int16_t)a[j]);
                } else if (sub[k].id == LANE_HIGHLIGHT_ANGLE && pU16(sub[k], v)) l.highlight = (int16_t)v;
            }
            g.lanes.push_back(l);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// neutral arrow (what the HUD / cluster has to draw), independent of the BMW-side encoding
// ---------------------------------------------------------------------------------------------------------------
enum ArrowKind {
    AK_NONE = 0, AK_STRAIGHT, AK_TURN, AK_KEEP, AK_U_TURN, AK_ROUNDABOUT, AK_EXIT_RAMP, AK_ENTER_RAMP,
    AK_FERRY, AK_DESTINATION, AK_START, AK_FOLLOW_ROAD
};

struct Arrow {
    ArrowKind kind;
    int angle;            // signed degrees, negative = left, positive = right, 0 straight, +-180 U-turn
    int roundaboutExit;   // 1.. when known, 0 otherwise
    bool leftHandTraffic;
    Arrow() : kind(AK_NONE), angle(0), roundaboutExit(0), leftHandTraffic(false) {}
};

// iAP2 maneuver type (0..53) -> neutral arrow. exitAngle from the maneuver, when present, refines the angle.
inline Arrow arrowFor(const Maneuver& m) {
    Arrow a;
    a.leftHandTraffic = m.has(RGM_DRIVING_SIDE) && m.drivingSide == 1;
    int t = m.type;
    switch (t) {
        case 0: case 3:   a.kind = AK_STRAIGHT; break;
        case 1: case 20:  a.kind = AK_TURN; a.angle = -90; break;
        case 2: case 21:  a.kind = AK_TURN; a.angle = 90; break;
        case 47:          a.kind = AK_TURN; a.angle = -135; break;
        case 48:          a.kind = AK_TURN; a.angle = 135; break;
        case 49:          a.kind = AK_TURN; a.angle = -45; break;
        case 50:          a.kind = AK_TURN; a.angle = 45; break;
        case 4: case 18: case 26:
            a.kind = AK_U_TURN; a.angle = a.leftHandTraffic ? 180 : -180; break;
        case 5:           a.kind = AK_FOLLOW_ROAD; break;
        case 6: case 7: case 19:
            a.kind = AK_ROUNDABOUT; if (t == 19) a.angle = a.leftHandTraffic ? 180 : -180; break;
        case 8:           a.kind = AK_EXIT_RAMP; a.angle = a.leftHandTraffic ? -30 : 30; break;
        case 22:          a.kind = AK_EXIT_RAMP; a.angle = -30; break;
        case 23:          a.kind = AK_EXIT_RAMP; a.angle = 30; break;
        case 9:           a.kind = AK_ENTER_RAMP; break;
        case 10: case 12: case 27:
            a.kind = AK_DESTINATION; break;
        case 24:          a.kind = AK_DESTINATION; a.angle = -90; break;
        case 25:          a.kind = AK_DESTINATION; a.angle = 90; break;
        case 11:          a.kind = AK_START; break;
        case 13: case 52: a.kind = AK_KEEP; a.angle = -20; break;
        case 14: case 53: a.kind = AK_KEEP; a.angle = 20; break;
        case 51:          a.kind = AK_KEEP; break;
        case 15: case 16: case 17:
            a.kind = AK_FERRY; break;
        default:
            if (t >= 28 && t <= 46) { a.kind = AK_ROUNDABOUT; a.roundaboutExit = t - 27; }
            break;
    }
    // Junction geometry beats the generic angle for turns, roundabouts and ramps (iOS: 0 = straight on,
    // clockwise positive is the common convention; confirm on the car before relying on the sign).
    if (m.has(RGM_EXIT_ANGLE) && (a.kind == AK_TURN || a.kind == AK_ROUNDABOUT || a.kind == AK_EXIT_RAMP)) {
        int e = m.exitAngle;
        while (e > 180) e -= 360;
        while (e < -180) e += 360;
        a.angle = e;
    }
    return a;
}

// ---------------------------------------------------------------------------------------------------------------
// stateful tracker: the next maneuver + distance, i.e. exactly what a BMW HUD shows
// ---------------------------------------------------------------------------------------------------------------
struct HudState {
    bool active;
    Arrow arrow;
    std::string distance;      // e.g. "2.4"
    uint8_t distanceUnits;     // DistUnit
    uint32_t distanceMeters;   // raw iAP2 distance to the maneuver
    std::string road;          // road after the maneuver ("3 Thang 2"), else the current road
    std::string destination;
    uint64_t timeRemaining;
    HudState() : active(false), distanceUnits(DU_M), distanceMeters(0), timeRemaining(0) {}
};

class Tracker {
public:
    Tracker() : state_(RS_NO_ROUTE), next_(0xFFFF), haveNext_(false) {}

    // Feeds one control-session message. Returns true when the HUD state changed.
    bool feed(const Message& m) {
        const HudState before = hud_;
        if (m.id == MSG_ROUTE_GUIDANCE_UPDATE) {
            RouteUpdate u;
            decodeRouteUpdate(m, u);
            if (u.has(RGU_STATE)) state_ = u.state;
            if (u.has(RGU_MANEUVER_LIST) && !u.maneuverList.empty()) { next_ = u.maneuverList[0]; haveNext_ = true; }
            if (u.has(RGU_DIST_TO_MANEUVER)) hud_.distanceMeters = u.distToManeuver;
            if (u.has(RGU_DIST_TO_MANEUVER_STRING)) hud_.distance = u.distToManeuverString;
            if (u.has(RGU_DIST_TO_MANEUVER_UNITS)) hud_.distanceUnits = u.distToManeuverUnits;
            if (u.has(RGU_CURRENT_ROAD)) currentRoad_ = u.currentRoad;
            if (u.has(RGU_DESTINATION)) hud_.destination = u.destination;
            if (u.has(RGU_TIME_REMAINING)) hud_.timeRemaining = u.timeRemaining;
            if (state_ == RS_NO_ROUTE || state_ == RS_ARRIVED) { maneuvers_.clear(); haveNext_ = false; }
            return refresh(before);
        }
        if (m.id == MSG_ROUTE_GUIDANCE_MANEUVER) {
            Maneuver x;
            decodeManeuver(m, x);
            if (!x.has(RGM_INDEX)) return false;
            bool found = false;
            for (size_t i = 0; i < maneuvers_.size(); ++i)
                if (maneuvers_[i].index == x.index) { maneuvers_[i] = x; found = true; }
            if (!found) {
                if (maneuvers_.size() >= 32) maneuvers_.erase(maneuvers_.begin());
                maneuvers_.push_back(x);
            }
            return refresh(before);
        }
        return false;
    }

    const HudState& hud() const { return hud_; }

private:
    // Recomputes the arrow / road from the cached maneuvers and reports whether anything differs from `before`.
    bool refresh(const HudState& before) {
        HudState h = hud_;
        h.active = state_ == RS_ROUTE_SET || state_ == RS_REROUTING || state_ == RS_PROCEED_TO_ROUTE;
        h.arrow = Arrow();
        h.road = currentRoad_;
        if (h.active && haveNext_) {
            for (size_t i = 0; i < maneuvers_.size(); ++i) {
                if (maneuvers_[i].index != next_) continue;
                h.arrow = arrowFor(maneuvers_[i]);
                if (!maneuvers_[i].afterRoad.empty()) h.road = maneuvers_[i].afterRoad;
            }
        }
        bool changed = h.active != before.active || h.arrow.kind != before.arrow.kind ||
                       h.arrow.angle != before.arrow.angle || h.arrow.roundaboutExit != before.arrow.roundaboutExit ||
                       h.distance != before.distance || h.distanceUnits != before.distanceUnits ||
                       h.road != before.road || h.distanceMeters != before.distanceMeters ||
                       h.destination != before.destination || h.timeRemaining != before.timeRemaining;
        hud_ = h;
        return changed;
    }

    uint8_t state_;
    uint16_t next_;
    bool haveNext_;
    std::string currentRoad_;
    std::vector<Maneuver> maneuvers_;
    HudState hud_;
};

} // namespace rgd

#endif
