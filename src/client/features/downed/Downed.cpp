// Down and revive. Dying with a partner alive in the same place does not end in the hospital: our player lies where he
// fell, the game is kept from its death (CPlayerInfo::KillPlayer is held, as Missions.cpp holds it for the host's
// mission, and CGameLogic's WASTED timer is set back every frame), and the partner kneels by the body and holds a key
// (or the pad's Triangle) to bring us up. Up in place with part of the health, the guns and the stars as they were.
// Nobody comes in time, or no partner is left alive to come (both down): the death goes on as the game has it.
//
// Not for the vehicle (the body stays in the seat: the game's death), nor in "Shared life" (a partner's death is
// the mission's there), nor with down_revive = 0 in coop.ini.
//
// The other players see the same as for a death: the owner's state says F_DEAD and its copy lies dead (Players). F2_DOWNED
// says the owner can still be brought up; once it stands the state is alive again and its copy is made anew there.
// The revive itself is a count in the downed player's game: the partner's G_REVIVE ticks, 5 a second while its key
// is held next to the body, fill a bar that falls back when they stop.
#include "features/downed/Downed.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/player/Players.h"
#include "entities/session/Flow.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::downed {

namespace {
using namespace game;
using game::script::Cmd;

constexpr float kReviveRange = 2.5f;       // the reviver stands this near the body (m)
constexpr float kReviveRangeSeen = 4.5f;   // the downed one's own check: the reviver's copy lags a little
constexpr float kHelpRange = 600.0f;       // a partner farther than this is no help in the time there is
constexpr float kReviveHealth = 0.3f;      // of the full health
constexpr uint64_t kTickEveryMs = 200;     // the reviver's G_REVIVE
constexpr uint64_t kTickFreshMs = 700;     // a tick counts this long
constexpr uint64_t kNoHelperMs = 3000;     // no partner who could come, for this long: the death goes on
enum : uint8_t { OP_TICK = 1 };

uint64_t BleedMs() { return (uint64_t)std::clamp(Cfg().downSeconds, 10, 600) * 1000; }
uint64_t ReviveMs() { return (uint64_t)std::clamp(Cfg().reviveSeconds, 1, 30) * 1000; }

struct Down {
    bool on = false;
    bool expired = false;       // bled out: the death goes on as the game has it, until we stand again
    uint64_t sinceMs = 0;
    uint64_t lastMs = 0;
    uint64_t progressMs = 0;    // how long a partner has been bringing us up
    uint64_t noHelperMs = 0;    // since when no partner could come
} g_down;

std::map<uint8_t, uint64_t> g_ticks;  // partner -> when its last G_REVIVE came
std::map<uint8_t, bool> g_announced;  // partner -> its being down was said in the chat

struct Reviving {
    uint8_t target = 0xFF;  // the downed partner we kneel by
    uint64_t heldMs = 0;
    uint64_t sentMs = 0;
    uint64_t lastMs = 0;
} g_rev;

void Reset() {
    g_down = {};
    g_rev = {};
    g_ticks.clear();
    g_announced.clear();
}

std::string KeyLabel(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    char name[64];
    if (sc && GetKeyNameTextA((LONG)(sc << 16), name, 64) > 0) return name;
    return Format("#%d", vk);
}

// the nearest partner who could bring us up: alive, with a copy here, in our place (area), within reach in the time
const RemotePlayer* HelperFor(CPlayerPed* me) {
    const RemotePlayer* best = nullptr;
    float bestD = kHelpRange;
    for (auto& [id, rp] : Players::Instance().All()) {
        if (!rp.ped || !rp.hasState || (rp.state.flags & OnFootState::F_DEAD)) continue;
        if (rp.state.area != me->AreaCode()) continue;
        float d = (rp.ped->GetPosition() - me->GetPosition()).Length();
        if (d < bestD) bestD = d, best = &rp;
    }
    return best;
}

bool CanGoDown(CPlayerPed* me) {
    return Cfg().downRevive && Net().Online() && !Net().SharedLife() && !g_down.expired && flow::InSharedWorld() &&
           !me->InVehicle() && HelperFor(me) != nullptr;
}

void Begin() {
    g_down = {};
    g_down.on = true;
    g_down.sinceMs = g_down.lastMs = NowMs();
    g_ticks.clear();
    ui::TheChat().AddSystem(TR("Вы упали. Напарник может вас поднять.", "You are down. A partner can bring you up."));
    LOGI("Down: a partner can bring us up for %u s", (unsigned)(BleedMs() / 1000));
}

void Expire(const char* why) {
    g_down.on = false;
    g_down.expired = true;
    g_down.progressMs = 0;
    ui::TheChat().AddSystem(TR("Помощь не пришла.", "No help came."));
    LOGI("Down over (%s): the death goes on as the game has it", why);
}

// the game's WASTED: CGameLogic marks it and starts its 3 s to the hospital as it finds the player dead; back to PLAYING
// (the same two words Missions.cpp's revive sets) before and after every process
void HoldGameLogic() {
    Ref<int32_t>(0x96A8B0) = 0;  // CGameLogic::GameState: GAMELOGIC_STATE_PLAYING
    Ref<int32_t>(0x96A8AC) = 0;  // CGameLogic::TimeOfLastEvent
}

// still lying down? false: stands by other means, or the time is out, or nobody can come (Expire says it)
bool StillDown(CPlayerPed* me) {
    if (!g_down.on) return false;
    if (me->Health() > 0.0f || !Net().Online() || !flow::InSharedWorld()) {  // up by other means (a script), or the game is going
        g_down = {};
        return false;
    }
    uint64_t now = NowMs();
    if (HelperFor(me)) {
        g_down.noHelperMs = 0;
    } else {
        if (!g_down.noHelperMs) g_down.noHelperMs = now;
        if (now - g_down.noHelperMs >= kNoHelperMs) return Expire("no partner can come"), false;
    }
    if (now - g_down.sinceMs >= BleedMs()) return Expire("bled out"), false;
    return true;
}

// alive again where we fell, as the game's respawn makes CJ (Missions.cpp Revive: SetInitialState and the rest) with what
// he had: his guns, his stars, the clock. The ground under the body: the lying one's origin is not the standing one's.
void ReviveInPlace(CPlayerPed* me) {
    float full = (float)PlayerInfos()[0].maxHealth;
    if (full < 1.0f) full = 100.0f;
    me->Health() = std::max(1.0f, full * kReviveHealth);
    Field<int32_t>(me, 0x750) = 0;                   // CPed::m_nDeathTimeMS
    me->Flags() = (me->Flags() | 0x80u) & ~0x800u;  // bIsVisible, not bRemoveFromWorld
    if (me->PedFlags() & 0x8000u) {                  // a katana took the head
        me->PedFlags() &= ~0x8000u;
        Field<int8_t>(me, 0x754) = -1;
    }
    CallMethod<0x60CD20>(me, false);  // CPlayerPed::SetInitialState: the death's tasks and state gone, on foot
    CallMethod<0x5DED50>(me);         // CPed::RestartNonPartialAnims
    CVector p = me->GetPosition();
    bool found = false;
    float z = Call<0x5696C0, float>(p.x, p.y, p.z + 1.0f, &found, (CEntity**)nullptr);  // CWorld::FindGroundZFor3DCoord
    Cmd(0x00A1).I(GetPedRef(me)).F(p.x).F(p.y).F(found ? z : p.z - 1.0f).Run();       // SET_CHAR_COORDINATES: feet
    Cmd(0x02EB).Run();  // RESTORE_CAMERA_JUMPCUT
    Cmd(0x0373).Run();  // SET_CAMERA_BEHIND_PLAYER
    Call<0x589070>();   // CHud::ResetWastedText
    HoldGameLogic();
    g_down = {};
    g_ticks.clear();
    ui::TheChat().AddSystem(TR("Вас подняли.", "You are back on your feet."));
    LOGI("Up again in place, health %.0f", me->Health());
}

bool KeyHeld() {
    if (!game::IsForeground() || game::MenuActive() || ui::TheChat().IsOpen() || ui::In().KeyboardCaptured()) return false;
    if (GetAsyncKeyState(Cfg().reviveKey) & 0x8000) return true;
    return GetPad(0)->NewState.ButtonTriangle != 0;
}

// the partner lies by us: kneel (stand by) and hold the key; its game counts the ticks
void ReviverTick(CPlayerPed* me, uint64_t now) {
    uint64_t dt = g_rev.lastMs ? std::min<uint64_t>(now - g_rev.lastMs, 100) : 0;
    g_rev.lastMs = now;
    const RemotePlayer* target = nullptr;
    if (me->Health() > 0.0f && !me->InVehicle() && flow::InSharedWorld()) {
        for (auto& [id, rp] : Players::Instance().All()) {
            if (!rp.ped || !rp.hasState || !(rp.state.flags & OnFootState::F_DEAD) ||
                !(rp.state.flags2 & OnFootState::F2_DOWNED) || rp.state.area != me->AreaCode())
                continue;
            if ((rp.ped->GetPosition() - me->GetPosition()).Length() <= kReviveRange) {
                target = &rp;
                break;
            }
        }
    }
    if (!target) {
        g_rev.target = 0xFF;
        g_rev.heldMs = 0;
        return;
    }
    if (g_rev.target != target->id) g_rev = {target->id, 0, 0, now};
    if (KeyHeld()) {
        g_rev.heldMs += dt;
        if (now - g_rev.sentMs >= kTickEveryMs) {
            g_rev.sentMs = now;
            Writer w(proto::G_REVIVE);
            w.U8(OP_TICK);
            w.U8(target->id);
            Net().Send(proto::CH_RELIABLE, w);
        }
    } else {
        g_rev.heldMs -= std::min(g_rev.heldMs, dt * 2);
    }
}

// "X is down" in the chat, once for each time
void AnnounceDowned() {
    for (auto& [id, rp] : Players::Instance().All()) {
        bool down = rp.hasState && (rp.state.flags & OnFootState::F_DEAD) && (rp.state.flags2 & OnFootState::F2_DOWNED);
        bool& said = g_announced[id];
        if (down && !said)
            ui::TheChat().AddSystem(Format(TR("%s упал. Подойдите и удерживайте %s, чтобы поднять.",
                                              "%s is down. Get next to them and hold %s to bring them up."),
                                           Net().PeerName(id).c_str(), KeyLabel(Cfg().reviveKey).c_str()));
        said = down;
    }
}

void Tick() {
    CPlayerPed* me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
    if (!me || !Net().Online()) {
        Reset();
        return;
    }
    uint64_t now = NowMs();
    AnnounceDowned();
    if (g_down.on) {
        if (!StillDown(me)) return;
        HoldGameLogic();
        uint64_t dt = std::min<uint64_t>(now - g_down.lastMs, 100);
        g_down.lastMs = now;
        bool helped = false;
        for (auto it = g_ticks.begin(); it != g_ticks.end();) {
            if (now - it->second > kTickFreshMs) {
                it = g_ticks.erase(it);
                continue;
            }
            const RemotePlayer* rp = Players::Instance().Get(it->first);
            if (rp && rp->ped && (rp->ped->GetPosition() - me->GetPosition()).Length() <= kReviveRangeSeen) helped = true;
            ++it;
        }
        if (helped) g_down.progressMs += dt;
        else g_down.progressMs -= std::min(g_down.progressMs, dt * 2);  // loses ground when they stop
        if (g_down.progressMs >= ReviveMs()) ReviveInPlace(me);
        return;
    }
    if (g_down.expired && me->Health() > 0.0f) g_down.expired = false;
    ReviverTick(me, now);
}

void Handle(uint8_t from, Reader& r) {
    uint8_t op = r.U8(), to = r.U8();
    if (!r.Ok() || from == Net().MyId() || to != Net().MyId() || op != OP_TICK || !g_down.on) return;
    g_ticks[from] = NowMs();
}

void Render() {
    auto& g = ui::Gfx();
    float s = g.Scale();
    float cx = g.Width() * 0.5f;
    auto bar = [&](float y, float frac, uint32_t colour) {
        float bw = 320 * s, bh = 8 * s, bx = cx - bw * 0.5f;
        g.Rect(bx, y, bw, bh, 0xC8000000);
        g.Rect(bx, y, bw * std::clamp(frac, 0.0f, 1.0f), bh, colour);
        g.RectOutline(bx, y, bw, bh, ui::w::kSaLine, std::max(1.0f, s));
    };
    if (g_down.on) {
        float y = g.Height() * 0.58f;
        g.Text(ui::FONT_BIG, cx, y, TR("ВЫ УПАЛИ", "YOU ARE DOWN"), ui::w::kSaRed, ui::TEXT_CENTER | ui::TEXT_OUTLINE);
        y += g.LineHeight(ui::FONT_BIG) + 6 * s;
        uint64_t left = BleedMs() - std::min(BleedMs(), NowMs() - g_down.sinceMs);
        g.Text(ui::FONT_NORMAL, cx, y,
               Format(TR("Напарник может вас поднять · осталось %u с", "A partner can bring you up · %u s left"),
                      (unsigned)((left + 999) / 1000)),
               0xFFFFFFFF, ui::TEXT_CENTER | ui::TEXT_SHADOW);
        y += g.LineHeight(ui::FONT_NORMAL) + 8 * s;
        bar(y, (float)left / (float)BleedMs(), ui::w::kSaRed);
        if (g_down.progressMs) {
            y += 18 * s;
            bar(y, (float)g_down.progressMs / (float)ReviveMs(), ui::w::kSaGreen);
        }
    } else if (g_rev.target != 0xFF) {
        float y = g.Height() * 0.68f;
        g.Text(ui::FONT_NORMAL, cx, y,
               Format(TR("Удерживайте %s (или Треугольник), чтобы поднять игрока %s",
                         "Hold %s (or Triangle) to bring %s up"),
                      KeyLabel(Cfg().reviveKey).c_str(), Net().PeerName(g_rev.target).c_str()),
               0xFFFFFFFF, ui::TEXT_CENTER | ui::TEXT_OUTLINE);
        y += g.LineHeight(ui::FONT_NORMAL) + 8 * s;
        bar(y, (float)g_rev.heldMs / (float)ReviveMs(), ui::w::kSaGreen);
    }
}

class Downed : public Module {
public:
    const char* Name() const override { return "Downed"; }
    void OnInstallHooks() override { Net().SetHandler(proto::G_REVIVE, &Handle); }
    void OnSessionEnd() override { Reset(); }
    void OnGameShutdown() override { Reset(); }
    void OnPeerLeave(uint8_t id) override {
        g_ticks.erase(id);
        g_announced.erase(id);
        if (g_rev.target == id) g_rev = {};
    }
    void OnPreProcess() override {
        if (g_down.on) HoldGameLogic();
    }
    void OnPostProcess() override { Tick(); }
    void OnRender() override { Render(); }

    static Downed& Instance() {
        static Downed m;
        return m;
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Downed::Instance()); }
} g_reg;
}  // namespace

bool OnKillPlayer() {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return false;
    if (g_down.on) return StillDown(me);
    if (!CanGoDown(me)) return false;
    Begin();
    return true;
}

bool IsDown() { return g_down.on; }

}  // namespace coop::downed
