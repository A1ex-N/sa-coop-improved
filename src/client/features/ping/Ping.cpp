#include "features/ping/Ping.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/player/Players.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/renderer/Renderer.h"

namespace coop::ping {
namespace {
using namespace game;

constexpr float kRange = 150.0f;
constexpr float kOpenSpaceRange = 45.0f;
constexpr uint64_t kDurationMs = 6500;
constexpr uint64_t kCooldownMs = 750;

struct Ping {
    CVector at;
    uint8_t area;
    uint64_t placedAt;
    uint32_t rgb = 0x50DCFF;
};

std::map<uint8_t, Ping> g_pings;
uint64_t g_nextTry = 0;

bool ValidPosition(const CVector& p) {
    // Bound remote coordinates before giving them to the renderer. The limits leave room for custom maps.
    return std::abs(p.x) <= 20000.0f && std::abs(p.y) <= 20000.0f && std::abs(p.z) <= 20000.0f;
}

void AimRay(CVector& start, CVector& direction) {
    constexpr uint16_t kAimWeapon = 53;  // MODE_AIMWEAPON, the HUD's offset third-person reticle
    if (cam::Mode() != kAimWeapon) return;
    const CMatrix& view = Ref<CMatrix>(addr::TheCamera + 0xA04);  // CCamera::m_mViewMatrix
    const CVector rowX = view.forward.Cross(view.up);
    const CVector rowY = view.up.Cross(view.right);
    const CVector rowZ = view.right.Cross(view.forward);
    const float det = view.right.Dot(rowX);
    if (!std::isfinite(det) || std::abs(det) < 0.00001f) return;
    auto inverseDirection = [&](const CVector& v) {
        return CVector{v.Dot(rowX), v.Dot(rowY), v.Dot(rowZ)} * (1.0f / det);
    };
    // Invert the game's screen projection: normalized screen coordinates are view X and Y divided by view Z.
    const CVector eye = inverseDirection(view.pos * -1.0f);
    const CVector ray = inverseDirection({cam::CrosshairMultX(), cam::CrosshairMultY(), 1.0f});
    if (!ValidPosition(eye) || !ValidPosition(ray) || ray.Length() < 0.001f) return;
    start = eye;
    direction = ray.Normalized();
}

float Fade(uint64_t age) {
    return std::min(1.0f, (kDurationMs - age) / 700.0f);
}

void DrawBeaconLabel(float x, float y, int metres, uint32_t color, float fade) {
    auto& g = ui::Gfx();
    const float s = g.Scale();
    std::string text = Format(TR("МЕТКА · %d м", "PING · %d m"), metres);
    float width = g.TextWidth(ui::FONT_NORMAL, text) + 24 * s;
    float height = g.LineHeight(ui::FONT_NORMAL) + 10 * s;
    x = std::clamp(x, width * 0.5f + 6 * s, std::max(width * 0.5f + 6 * s, g.Width() - width * 0.5f - 6 * s));
    y = std::clamp(y, 18 * s, g.Height() - height - 18 * s);
    g.Rect(x - width * 0.5f, y, width, height, ui::WithAlpha(0xED101218, fade));
    g.RectOutline(x - width * 0.5f, y, width, height, ui::WithAlpha(0xFFF7FBFF, fade), std::max(1.0f, s));
    g.Rect(x - width * 0.5f + 2 * s, y + 2 * s, 4 * s, height - 4 * s, color);
    g.Text(ui::FONT_NORMAL, x, y + 5 * s, text, ui::WithAlpha(0xFFFFFFFF, fade), ui::TEXT_CENTER);
}

bool DrawBeacon(const Ping& ping, float distance, uint64_t age) {
    auto& g = ui::Gfx();
    float x, y, depth;
    if (!WorldToScreen(ping.at, x, y, depth) || !std::isfinite(x) || !std::isfinite(y) ||
        x < 0 || x > g.Width() || y < 0 || y > g.Height()) return false;
    const float s = g.Scale(), fade = Fade(age);
    const uint32_t color = ui::WithAlpha(0xFF000000 | ping.rgb, fade);
    float tipX = x, tipY = y - 80 * s, tipDepth;
    float projectedX, projectedY;
    if (WorldToScreen(ping.at + CVector{0, 0, 22.0f}, projectedX, projectedY, tipDepth) &&
        std::isfinite(projectedX) && std::isfinite(projectedY)) {
        tipX = std::clamp(projectedX, 0.0f, g.Width());
        tipY = std::clamp(projectedY, 0.0f, std::max(0.0f, y - 54 * s));
    }
    float pulse = 0.5f + 0.5f * std::cos(age * 0.008f);
    g.Line(x, y, tipX, tipY, (22 + 8 * pulse) * s, ui::WithAlpha(0x60000000 | ping.rgb, fade));
    g.Line(x, y, tipX, tipY, 12 * s, ui::WithAlpha(0xE8000000, fade));
    g.Line(x, y, tipX, tipY, 8 * s, color);
    g.Line(x, y, tipX, tipY, 3 * s, ui::WithAlpha(0xFFFFFFFF, fade));
    // A high-contrast crosshair ends at the exact point that was pinged.
    for (float sign : {-1.0f, 1.0f}) {
        const uint32_t white = ui::WithAlpha(0xFFFFFFFF, fade);
        g.Line(x + sign * 5 * s, y, x + sign * 14 * s, y, 3 * s, white);
        g.Line(x, y + sign * 5 * s, x, y + sign * 14 * s, 3 * s, white);
    }
    DrawBeaconLabel(tipX, tipY - g.LineHeight(ui::FONT_NORMAL) - 14 * s, (int)std::lround(distance), color, fade);
    return true;
}

void DrawOffscreen(const Ping& ping, float distance, uint64_t age) {
    auto& g = ui::Gfx();
    const float s = g.Scale();
    const CVector delta = ping.at - cam::Source();
    const float cx = g.Width() * 0.5f, cy = g.Height() * 0.5f;
    float sx, sy, depth;
    if (WorldToScreen(ping.at, sx, sy, depth) && std::isfinite(sx) && std::isfinite(sy)) {
        sx -= cx;
        sy -= cy;
    } else {
        sx = delta.Dot(cam::Front().Cross(cam::Up()));
        sy = -delta.Dot(cam::Up());
        if (std::hypot(sx, sy) < 0.1f) sy = 1.0f;
    }
    float length = std::hypot(sx, sy);
    if (!std::isfinite(length) || length < 0.1f) return;
    float dx = sx / length, dy = sy / length;
    float factor = 1.0f / std::max(std::abs(dx) / (cx * 0.78f), std::abs(dy) / (cy * 0.75f));
    float x = cx + dx * factor, y = cy + dy * factor;
    float fade = Fade(age);
    uint32_t color = ui::WithAlpha(0xFF000000 | ping.rgb, fade);
    for (float side : {-1.0f, 1.0f}) {
        float ex = x - dx * 15 * s - side * dy * 9 * s;
        float ey = y - dy * 15 * s + side * dx * 9 * s;
        g.Line(x, y, ex, ey, 9 * s, ui::WithAlpha(0xFFFFFFFF, fade));
        g.Line(x, y, ex, ey, 4 * s, color);
    }
    float labelHeight = g.LineHeight(ui::FONT_NORMAL) + 10 * s;
    DrawBeaconLabel(x - dx * 18 * s, y - labelHeight * 0.5f - dy * (labelHeight + 14 * s),
                    (int)std::lround(distance), color, fade);
}

class Pings : public Module {
public:
    const char* Name() const override { return "Pings"; }

    void OnInstallHooks() override {
        Net().SetHandler(proto::G_PING, [](uint8_t from, Reader& r) {
            uint8_t area = r.U8();
            auto pos = r.Vec();
            CVector at{pos.x, pos.y, pos.z};
            uint32_t rgb = r.Remaining() ? r.U32() : 0x50DCFF;
            if (rgb > 0xFFFFFF) return;
            if (!r.Ok() || r.Remaining() != 0 || !ValidPosition(at) || from == Net().MyId()) return;
            uint64_t now = NowMs();
            auto previous = g_pings.find(from);
            if (previous != g_pings.end() && now - previous->second.placedAt < kCooldownMs) return;
            g_pings.insert_or_assign(from, Ping{at, area, now, rgb});
            LOGD("Ping received from player %u in area %u at %.1f, %.1f, %.1f", from, area, at.x, at.y, at.z);
        });
    }

    void OnGameStarted() override { Clear(); }
    void OnSessionStart() override { Clear(); }
    void OnSessionEnd() override { Clear(); }
    void OnPeerLeave(uint8_t id) override { g_pings.erase(id); }

    void OnRender() override {
        auto* me = g_pings.empty() || game::MenuActive() || cam::WideScreenOn() ? nullptr : FindPlayerPed(0);
        if (!me) return;
        uint64_t now = NowMs();
        const Ping* offscreen = nullptr;
        float offscreenDistance = 0;
        for (const auto& [id, ping] : g_pings) {
            uint64_t age = now - ping.placedAt;
            if (age >= kDurationMs || ping.area != static_cast<uint8_t>(me->AreaCode())) continue;
            float distance = (ping.at - me->GetPosition()).Length();
            if (!DrawBeacon(ping, distance, age) && (!offscreen || ping.placedAt > offscreen->placedAt)) {
                offscreen = &ping;
                offscreenDistance = distance;
            }
        }
        if (offscreen) DrawOffscreen(*offscreen, offscreenDistance, now - offscreen->placedAt);
    }

    void OnPreProcess() override {
        if (g_pings.empty() || !core::IsPlayerReady()) return;
        auto* me = FindPlayerPed(0);
        if (!me) return;

        uint64_t now = NowMs();
        for (auto it = g_pings.begin(); it != g_pings.end();) {
            auto& [id, ping] = *it;
            uint64_t age = now - ping.placedAt;
            if (age >= kDurationMs) {
                it = g_pings.erase(it);
                continue;
            }
            if (ping.area == static_cast<uint8_t>(me->AreaCode())) {
                float pulse = 0.5f + 0.5f * std::cos(age * 0.012f);
                float fade = Fade(age);
                auto alpha = static_cast<uint8_t>((145.0f + 110.0f * pulse) * fade);
                uint32_t markerId = 0x50494E00u | id;
                CVector base = ping.at;
                auto red = static_cast<uint8_t>(ping.rgb >> 16);
                auto green = static_cast<uint8_t>(ping.rgb >> 8);
                auto blue = static_cast<uint8_t>(ping.rgb);
                // C3dMarkers::PlaceMarkerSet uses the same cylinder as mission markers.
                Call<0x725BA0>(markerId, (uint16_t)1, &base, 2.25f, red, green, blue,
                               alpha, (uint16_t)1024, 0.12f, (int16_t)0);

                // CCoronas::RegisterCorona adds a bright light above the cylinder.
                CVector light = ping.at + CVector{0.0f, 0.0f, 4.0f + 0.6f * pulse};
                Call<0x6FC580>(markerId, (CEntity*)nullptr, red, green, blue, alpha,
                               &light, 1.6f + 0.7f * pulse, 240.0f, (uint8_t)0, (uint8_t)0, (uint8_t)0,
                               (uint8_t)0, (uint8_t)0, 0.0f, false, 0.0f, true, 15.0f, false, true);
            }
            ++it;
        }
    }

private:
    static void Clear() {
        g_pings.clear();
        g_nextTry = 0;
    }
};

struct Reg {
    Reg() {
        static Pings module;
        core::RegisterModule(&module);
    }
} g_reg;
}  // namespace

bool Place() {
    if (!core::IsPlayerReady()) return false;
    auto* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || GetPad(0)->DisablePlayerControls || cam::WideScreenOn()) return false;

    uint64_t now = NowMs();
    if (now < g_nextTry) return true;
    g_nextTry = now + kCooldownMs;

    CVector start = cam::Source();
    CVector direction = cam::Front();
    AimRay(start, direction);
    CVector end = start + direction * kRange;
    if (!ValidPosition(start) || !ValidPosition(end)) return true;

    // CColPoint starts with the position. Ignore our own body or vehicle when casting from the camera.
    struct {
        CVector point;
        uint8_t rest[0x2C - sizeof(CVector)];
    } collision{};
    auto& ignore = Ref<CEntity*>(0xB7CD68);  // CWorld::pIgnoreEntity
    auto* previous = ignore;
    ignore = me->InVehicle() ? static_cast<CEntity*>(me->Vehicle()) : static_cast<CEntity*>(me);
    CEntity* hit = nullptr;
    bool found = Call<0x56BA00, bool>(&start, &end, &collision, &hit, true, true, true, true, true, false, false, false);
    ignore = previous;
    // Aiming into open space still places a beacon within view. A hit is offset to keep the glow clear of the surface.
    bool hitSurface = found && ValidPosition(collision.point);
    CVector at = hitSurface ? collision.point - direction * 0.2f : start + direction * kOpenSpaceRange;
    uint8_t area = static_cast<uint8_t>(me->AreaCode());
    uint32_t rgb = Players::LocalMarkRgb();
    const bool online = Net().Online();
    g_pings.insert_or_assign(online ? Net().MyId() : 0, Ping{at, area, now, rgb});
    LOGI("Ping placed %s in area %u at %.1f, %.1f, %.1f (%s)", online ? "online" : "locally", area,
         at.x, at.y, at.z, hitSurface ? "surface" : "open space");
    if (!online) return true;
    Writer w(proto::G_PING);
    w.U8(area);
    w.Vec({at.x, at.y, at.z});
    w.U32(rgb);
    Net().Send(proto::CH_RELIABLE, w);
    return true;
}

}  // namespace coop::ping
