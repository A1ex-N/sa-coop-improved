// Skins: the model our player wears instead of CJ. /skin <number> puts that model on, /skin next and /skin prev go through
// the game's people one by one, /skin 0 is CJ again.
//
// The other games need nothing of their own for it: Progress already sends the model of our player (CPed::m_nModelIndex,
// G_PLAYER_LOOK) and puts it on our copy there (Players::SetModel), as it does for a skin mod's. So all that is left is
// to put the model on our own player, which is done here the way the game's SET_PLAYER_MODEL does it.
//
// The choice is kept in coop.ini ([player] skin) and put on again when the game gives CJ back: a loaded game, a
// respawn, the end of a cutscene. That never happens at a bad moment (see WhyNot), never over a model another mod put
// on, and gives up for a minute if something keeps taking the skin off.
//
// A skin's number is the game's model ID. Only models the game has as people are taken (CModelInfo type 7), and not the
// special characters (290-299): each game fills those by name and no number says which one is meant.
#include "features/skin/Skin.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/mission/Mission.h"
#include "entities/player/Players.h"
#include "entities/profile/Profile.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop::skin {

namespace {
using namespace game;
using game::script::Cmd;

constexpr int kModelLimit = 20000;  // the game's model table is read below this (IsPedModel)
constexpr uint64_t kSettleMs = 4000;   // after the game is ready: the world and the partner's profile come first
constexpr uint64_t kRetryMs = 500;     // between two tries to put the skin on again
constexpr uint64_t kWindowMs = 30000;  // more than kMaxTries put on again within this: something takes it off, let it be
constexpr int kMaxTries = 3;
constexpr uint64_t kPauseMs = 60000;

int g_wanted = 0;  // the skin the player chose (0: CJ), from coop.ini and /skin
bool g_loaded = false;
uint64_t g_readySince = 0, g_nextTryMs = 0, g_windowMs = 0, g_pausedUntilMs = 0;
int g_tries = 0;
std::set<int> g_ours;  // models our choices asked the streaming for: let go of when the player moves on to another

bool IsSpecial(int model) { return model >= 290 && model <= 299; }

bool Selectable(int model) { return model == 0 || (model > 0 && model < kModelLimit && !IsSpecial(model) && IsPedModel(model)); }

void RebuildPlayer(CPlayerPed* ped, bool keepDescFatMuscle) { Call<0x5A82C0>(ped, keepDescFatMuscle); }  // CClothes::RebuildPlayer

// the same two tests the Animations feature makes before it takes CJ over (Emotes.cpp)
bool CutsceneOn() { return Ref<uint8_t>(0xB5F851) || Ref<int32_t>(0xB5F84C); }  // CCutsceneMgr::ms_running, ms_cutsceneLoadStatus
bool Scripted() {
    return GetPad(0)->DisablePlayerControls || cam::WideScreenOn() || Ref<uint8_t>(0xA444A8) /* CTheScripts::bMiniGameInProgress */ ||
           ContestActivity() || SceneWatched();
}

// why our player cannot change its model now, empty: it can. The model's change drops the player's tasks and its body (as
// the game's SET_PLAYER_MODEL does), a thing for a player who stands free.
std::string WhyNot(CPlayerPed*& me) {
    me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
    if (!me) return TR("Игра ещё не началась.", "The game has not started yet.");
    if (me->Health() <= 0.0f) return TR("Персонаж мёртв.", "Your character is dead.");
    if (CutsceneOn()) return TR("Сейчас идёт катсцена.", "A cutscene is on.");
    if (Scripted()) return TR("Сейчас персонажем управляет игра.", "The game controls your character right now.");
    if (me->InVehicle()) return TR("Сначала выйдите из машины.", "Get out of the vehicle first.");
    if (!(me->PedFlags() & 1) || me->IsInTheAir())  // CPed::bIsStanding: swimming and falling are out too
        return TR("Встаньте на землю.", "Stand on the ground first.");
    if (PlayerInfos()[0].remoteVehicle) return TR("Сейчас вы управляете игрушкой.", "You are driving a remote-controlled toy.");
    return "";
}

// the next model of a person after `from` in that direction, round the whole table
int Step(int from, int dir) {
    const int n = kModelLimit;
    for (int i = 1; i <= n; ++i) {
        int m = ((from + dir * i) % n + n) % n;
        if (Selectable(m)) return m;
    }
    return from;
}

// the choice goes to the player's own coop.ini: next time the game starts with it
void Remember(int model) {
    g_wanted = model;
    g_tries = 0;
    g_pausedUntilMs = 0;
    Cfg().skin = model;
    Ini ini;
    ini.Load(Cfg().iniPath);
    ini.SetInt("player", "skin", model);
    ini.Save(Cfg().iniPath);
}

void Usage(int current) {
    ui::TheChat().AddSystem(Format(TR("Ваш скин: %d. /skin <номер> выбирает скин, /skin next и /skin prev листают их, /skin 0 возвращает CJ.",
                                      "Your skin: %d. /skin <number> picks one, /skin next and /skin prev go through them, /skin 0 is CJ again."),
                                   current));
}

// the skin the player chose, back on when the game gave CJ back; quiet: no word in the chat when it cannot be now
void AutoWear(CPlayerPed* me, uint64_t now) {
    if (g_wanted <= 0 || now < g_nextTryMs || now < g_pausedUntilMs) return;
    // the partner's profile goes over the player's body once, a little after the host's world came (Progress)
    if (now - g_readySince < kSettleMs || !ProfileSettled()) return;
    if (me->ModelIndex() != 0 || core::InPrologue()) return;  // it wears one already: ours, or a skin mod's
    g_nextTryMs = now + kRetryMs;
    CPlayerPed* same = nullptr;
    if (!WhyNot(same).empty()) return;
    if (!Selectable(g_wanted)) {
        LOGW("Skin %d from coop.ini is not a person of this game: CJ stays", g_wanted);
        g_wanted = 0;
        return;
    }
    if (now - g_windowMs > kWindowMs) g_windowMs = now, g_tries = 0;
    if (++g_tries > kMaxTries) {
        g_pausedUntilMs = now + kPauseMs;
        LOGW("Something took skin %d off %d times in %u s: not put on for a minute", g_wanted, kMaxTries, (unsigned)(kWindowMs / 1000));
        ui::TheChat().AddError(TR("Игра снова и снова возвращает CJ, скин пока не надевается. Выберите его заново: /skin <номер>.",
                                  "The game keeps putting CJ back, so the skin is left off for a minute. Pick it again with /skin <number>."));
        return;
    }
    std::string why;
    if (!Apply(g_wanted, why)) LOGW("Skin %d not put on: %s", g_wanted, why.c_str());
}

class Skin : public Module {
public:
    const char* Name() const override { return "Skin"; }
    void OnGameStarted() override {  // a world made anew: our player is CJ again until the skin goes on
        g_readySince = g_nextTryMs = g_windowMs = g_pausedUntilMs = 0;
        g_tries = 0;
        g_ours.clear();  // the streaming was reset with the world
    }
    void OnPostProcess() override {
        if (!g_loaded) {
            g_loaded = true;
            g_wanted = std::max(0, Cfg().skin);
        }
        CPlayerPed* me = core::IsPlayerReady() ? FindPlayerPed(0) : nullptr;
        if (!me) {
            g_readySince = 0;
            return;
        }
        uint64_t now = NowMs();
        if (!g_readySince) g_readySince = now;
        AutoWear(me, now);
    }
    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override { return Command(cmd, args); }

    static Skin& Instance() {
        static Skin s;
        return s;
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Skin::Instance()); }
} g_reg;
}  // namespace

bool Apply(int model, std::string& why) {
    CPlayerPed* me = nullptr;
    why = WhyNot(me);
    if (!why.empty()) return false;
    if (!Selectable(model)) {
        why = IsSpecial(model) ? std::string(TR("Особых персонажей (290-299) выбрать нельзя.", "The special characters (290-299) cannot be picked."))
                               : Format(TR("Скина с номером %d нет.", "There is no skin number %d."), model);
        return false;
    }
    int prev = me->ModelIndex();
    if (prev == model) return true;
    int weapon = me->ActiveWeapon().type;  // the gun in the hand goes with the old body
    if (!Players::SetModel(me, model)) {
        why = Format(TR("Скин %d не загрузился.", "Skin %d did not load."), model);
        return false;
    }
    if (model == 0) RebuildPlayer(me, false);  // CJ's clothes, fat and muscle on the new body
    if (model != 0) g_ours.insert(model);
    if (prev != model && g_ours.erase(prev)) MarkModelNotNeeded(prev);  // a model our choice asked for: free to go when unused
    if (weapon > 0) {
        int h = GetPedRef(me);
        Cmd(0x01B9).I(h).I(0).Run();       // SET_CURRENT_CHAR_WEAPON: put away and taken again, the gun is in the hand anew
        Cmd(0x01B9).I(h).I(weapon).Run();
    }
    LOGI("Skin %d put on (was %d)", model, prev);
    return true;
}

bool Command(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd != "skin") return false;
    int current = g_wanted;
    if (core::IsPlayerReady())
        if (CPlayerPed* me = FindPlayerPed(0)) current = me->ModelIndex();
    std::string a = args.empty() ? "" : ToLower(args[0]);
    int target = -1;
    if (a == "next" || a == "n" || a == "+") target = Step(current, +1);
    else if (a == "prev" || a == "previous" || a == "p" || a == "-") target = Step(current, -1);
    else if (a == "cj" || a == "reset" || a == "off") target = 0;
    else if (!a.empty() && a.size() <= 5 && a.find_first_not_of("0123456789") == std::string::npos) target = atoi(a.c_str());
    if (target < 0) {
        Usage(current);
        return true;
    }
    std::string why;
    if (!Apply(target, why)) {
        ui::TheChat().AddError(why);
        return true;
    }
    Remember(target);
    ui::TheChat().AddSystem(target == 0 ? std::string(TR("Вы снова CJ.", "You are CJ again.")) : Format(TR("Скин %d.", "Skin %d."), target));
    return true;
}

}  // namespace coop::skin
