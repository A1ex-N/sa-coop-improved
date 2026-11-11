#include "features/ping/Ping.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "common/api/protocol/Protocol.h"
#include "common/lib/util/Util.h"
#include "entities/player/Players.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop::ping {
namespace {
using namespace game;

constexpr float kRange = 150.0f;
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
        });
    }

    void OnGameStarted() override { Clear(); }
    void OnSessionStart() override { Clear(); }
    void OnSessionEnd() override { Clear(); }
    void OnPeerLeave(uint8_t id) override { g_pings.erase(id); }

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
                float fade = std::min(1.0f, (kDurationMs - age) / 700.0f);
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
    if (!Net().Online() || !core::IsPlayerReady()) return false;
    auto* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f || GetPad(0)->DisablePlayerControls || cam::WideScreenOn()) return false;

    uint64_t now = NowMs();
    if (now < g_nextTry) return true;
    g_nextTry = now + kCooldownMs;

    CVector start = cam::Source();
    CVector direction = cam::Front();
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
    if (!found || !ValidPosition(collision.point)) {
        ui::TheChat().AddSystem(TR("Наведите камеру на объект или землю.", "Point the camera at an object or the ground."));
        return true;
    }

    // Pull the glow slightly off the surface so the geometry does not hide it.
    CVector at = collision.point - direction * 0.2f;
    uint8_t area = static_cast<uint8_t>(me->AreaCode());
    uint32_t rgb = Players::LocalMarkRgb();
    g_pings.insert_or_assign(Net().MyId(), Ping{at, area, now, rgb});
    Writer w(proto::G_PING);
    w.U8(area);
    w.Vec({at.x, at.y, at.z});
    w.U32(rgb);
    Net().Send(proto::CH_RELIABLE, w);
    return true;
}

}  // namespace coop::ping
