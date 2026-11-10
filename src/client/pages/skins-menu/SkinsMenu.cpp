// The skin picker: the game's available ped models, using the same window and list style as the U animation menu.
#include "pages/skins-menu/SkinsMenu.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "common/lib/util/Util.h"
#include "shared/config/Config.h"
#include "features/skin/Skin.h"
#include "features/anims/Emotes.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::ui {

namespace {
using w::kSaBlue, w::kSaMuted, w::kSaRed, w::kSaLine;
constexpr uint32_t kText = 0xFFE6E9EF;

bool g_visible = false;
bool g_captured = false;
int g_selected = 0;
int g_scroll = 0;
float g_view = 0.0f;
uint64_t g_viewMs = 0;
std::string g_note;
bool g_noteError = false;
float g_dragX = -1.0f;
uint64_t g_noteUntil = 0;
int g_clickModel = -1;
uint64_t g_clickMs = 0;
const char* SkinName(int model) {
    // GTA San Andreas' stock ped IDs and the names used by its character roster.
    // IDs absent from this table are supplied by another game/mod and keep a clear fallback.
    static const std::pair<int, const char*> names[] = {
        {0, "Carl 'CJ' Johnson"}, {1, "The Truth"}, {2, "Maccer"}, {3, "Andre"},
        {4, "Barry 'Big Bear' Thorne (Thin)"}, {5, "Barry 'Big Bear' Thorne"}, {6, "Emmet"},
        {7, "Taxi / Train Driver"}, {8, "Janitor"}, {9, "Female Civilian"}, {10, "Old Woman"},
        {11, "Casino Croupier"}, {12, "Rich Woman"}, {13, "Street Girl"}, {14, "Male Civilian"},
        {15, "Mr. Whittaker"}, {16, "Airport Ground Worker"}, {17, "Businessman"}, {18, "Beach Visitor"},
        {19, "DJ"}, {20, "Madd Dogg's Manager"}, {21, "Male Civilian"}, {22, "Male Civilian"},
        {23, "BMX Rider"}, {24, "Madd Dogg's Bodyguard"}, {25, "Madd Dogg's Bodyguard"},
        {26, "Backpacker"}, {27, "Construction Worker"}, {28, "Los Santos Drug Dealer"},
        {29, "San Fierro Drug Dealer"}, {30, "Las Venturas Drug Dealer"}, {31, "Farm-Town Woman"},
        {32, "Farm-Town Man"}, {33, "Farm-Town Man"}, {34, "Farm-Town Man"}, {35, "Gardener"},
        {36, "Golfer"}, {37, "Golfer"}, {38, "Female Civilian"}, {39, "Female Civilian"},
        {40, "Female Civilian"}, {41, "Female Civilian"}, {43, "Male Civilian"}, {44, "Male Civilian"},
        {45, "Beach Visitor"}, {46, "Male Civilian"}, {47, "Male Civilian"}, {48, "Male Civilian"},
        {49, "Da Nang Snakehead"}, {50, "Mechanic"}, {51, "Mountain Biker"}, {52, "Mountain Biker"},
        {53, "Female Civilian"}, {54, "Female Civilian"}, {55, "Female Civilian"}, {56, "Female Civilian"},
        {57, "Oriental Man"}, {58, "Oriental Man"}, {59, "Oriental Man"}, {60, "Oriental Man"},
        {61, "Pilot"}, {62, "Colonel Fuhrberger"}, {63, "Prostitute"}, {64, "Prostitute"},
        {65, "Kendl Johnson"}, {66, "Pool Player"}, {67, "Pool Player"}, {68, "Priest / Preacher"},
        {69, "Female Civilian"}, {70, "Scientist"}, {71, "Security Guard"}, {72, "Hippy"}, {73, "Hippy"},
        {75, "Prostitute"}, {76, "Stewardess"}, {77, "Homeless Woman"}, {78, "Homeless Man"},
        {79, "Homeless Man"}, {80, "Boxer"}, {81, "Boxer"}, {82, "Black Elvis"}, {83, "White Elvis"},
        {84, "Blue Elvis"}, {85, "Prostitute"}, {86, "Ryder (Robbery Mask)"}, {87, "Stripper"},
        {88, "Female Civilian"}, {89, "Female Civilian"}, {90, "Female Jogger"}, {91, "Rich Woman"},
        {92, "Female Roller Skater"}, {93, "Female Civilian"}, {94, "Male Civilian"},
        {95, "Dillimore Gas Station Owner"}, {96, "Male Jogger"}, {97, "Lifeguard"},
        {98, "Male Civilian"}, {99, "Male Roller Skater"}, {100, "Biker"}, {101, "Male Civilian"},
        {102, "Ballas Member"}, {103, "Ballas Member"}, {104, "Ballas Member"},
        {105, "Grove Street Families Member"}, {106, "Grove Street Families Member"},
        {107, "Grove Street Families Member"}, {108, "Los Santos Vagos Member"},
        {109, "Los Santos Vagos Member"}, {110, "Los Santos Vagos Member"},
        {111, "Russian Mafia Member"}, {112, "Russian Mafia Member"}, {113, "Russian Mafia Boss"},
        {114, "Varios Los Aztecas Member"}, {115, "Varios Los Aztecas Member"},
        {116, "Varios Los Aztecas Member"}, {117, "San Fierro Triad Member"},
        {118, "San Fierro Triad Member"}, {119, "Johnny Sindacco"}, {120, "Triad Boss"},
        {121, "Da Nang Boy"}, {122, "Da Nang Boy"}, {123, "Da Nang Boy"},
        {124, "Las Venturas Mafia Member"}, {125, "Las Venturas Mafia Member"},
        {126, "Las Venturas Mafia Member"}, {127, "Las Venturas Mafia Member"},
        {128, "Farm-Town Man"}, {129, "Farm-Town Woman"}, {130, "Farm-Town Woman"},
        {131, "Farm-Town Woman"}, {132, "Farm-Town Man"}, {133, "Farm-Town Man"},
        {134, "Homeless Man"}, {135, "Homeless Man"}, {136, "Male Civilian"}, {137, "Homeless Man"},
        {138, "Beach Visitor"}, {139, "Beach Visitor"}, {140, "Beach Visitor"},
        {141, "Businesswoman"}, {142, "Taxi Driver"}, {143, "Crack Dealer"}, {144, "Crack Dealer"},
        {145, "Crack Dealer"}, {146, "Crack Dealer"}, {147, "Businessman"}, {148, "Businesswoman"},
        {150, "Businesswoman"}, {151, "Female Civilian"}, {152, "Prostitute"},
        {153, "Construction Worker"}, {154, "Beach Visitor"}, {155, "Well Stacked Pizza Worker"},
        {156, "Barber"}, {157, "Hillbilly"}, {158, "Farmer"}, {159, "Hillbilly"},
        {160, "Hillbilly"}, {161, "Farmer"}, {162, "Hillbilly"}, {163, "Black Bouncer"},
        {164, "White Bouncer"}, {165, "White MIB Agent"}, {166, "Black MIB Agent"},
        {167, "Cluckin' Bell Worker"}, {168, "Hotdog Vendor"}, {169, "Male Civilian"},
        {170, "Male Civilian"}, {171, "Blackjack Dealer"}, {172, "Casino Croupier"},
        {173, "San Fierro Rifa Member"}, {174, "San Fierro Rifa Member"},
        {175, "San Fierro Rifa Member"}, {176, "Barber"}, {177, "Barber"}, {178, "Woman"},
        {179, "Ammu-Nation Salesman"}, {180, "Tattoo Artist"}, {181, "Punk"}, {182, "Cab Driver"},
        {183, "Male Civilian"}, {184, "Male Civilian"}, {185, "Male Civilian"}, {186, "Male Civilian"},
        {187, "Businessman"}, {188, "Male Civilian"}, {189, "Valet"}, {190, "Barbara Schternvart"},
        {191, "Helena Wankstein"}, {192, "Michelle Cannes"}, {193, "Katie Zhan"},
        {194, "Millie Perkins"}, {195, "Denise Robinson"}, {196, "Farm-Town Inhabitant"},
        {197, "Hillbilly"}, {198, "Farm-Town Inhabitant"}, {199, "Farm-Town Inhabitant"},
        {200, "Hillbilly"}, {201, "Farmer"}, {202, "Farmer"}, {203, "Karate Teacher"},
        {204, "Karate Teacher"}, {205, "Burger Shot Cashier"}, {206, "Cab Driver"},
        {207, "Prostitute"}, {209, "Noodle Stand Vendor"}, {210, "Boating School Instructor"},
        {211, "Clothes Shop Staff"}, {212, "Homeless Person"}, {213, "Weird Old Man"},
        {214, "Maria Latore (Waitress)"}, {215, "Female Civilian"}, {216, "Female Civilian"},
        {217, "Clothes Shop Staff"}, {218, "Male Civilian"}, {219, "Rich Woman"},
        {220, "Cab Driver"}, {221, "Male Civilian"}, {222, "Male Civilian"}, {223, "Male Civilian"},
        {224, "Male Civilian"}, {225, "Male Civilian"}, {226, "Male Civilian"},
        {227, "Oriental Businessman"}, {228, "Oriental Man"}, {229, "Oriental Man"},
        {230, "Homeless Person"}, {231, "Male Civilian"}, {232, "Male Civilian"},
        {233, "Male Civilian"}, {234, "Cab Driver"}, {235, "Male Civilian"},
        {236, "Male Civilian"}, {237, "Prostitute"}, {238, "Prostitute"}, {239, "Homeless Person"},
        {240, "District Attorney"}, {241, "African-American Man"}, {242, "Mexican Man"},
        {243, "Prostitute"}, {244, "Stripper"}, {245, "Prostitute"}, {246, "Stripper"},
        {247, "Biker"}, {248, "Biker"}, {249, "Pimp"}, {250, "Male Civilian"},
        {251, "Lifeguard"}, {252, "Naked Valet"}, {253, "Bus Driver"}, {254, "Biker Drug Dealer"},
        {255, "Limousine Chauffeur"}, {256, "Stripper"}, {257, "Stripper"}, {258, "Heckler"},
        {259, "Heckler"}, {260, "Construction Worker"}, {261, "Cab Driver"}, {262, "Cab Driver"},
        {263, "Male Civilian"}, {264, "Clown / Ice-Cream Van Driver"},
        {265, "Officer Frank Tenpenny"}, {266, "Officer Eddie Pulaski"},
        {267, "Officer Jimmy Hernandez"}, {268, "Dwaine"}, {269, "Big Smoke (Mission)"},
        {270, "Sweet Johnson"}, {271, "Ryder"}, {272, "Forelli Mafia Boss"},
        {274, "Paramedic"}, {275, "Paramedic"}, {276, "Paramedic"}, {277, "Firefighter"},
        {278, "Firefighter"}, {279, "Firefighter"}, {280, "Los Santos Police Officer"},
        {281, "San Fierro Police Officer"}, {282, "Las Venturas Police Officer"},
        {283, "County Sheriff"}, {284, "Motorbike Police Officer"}, {285, "S.W.A.T. Officer"},
        {286, "Federal Agent"}, {287, "Army Soldier"}, {288, "Desert Sheriff"},
        {300, "Ryder (Alternate)"}, {301, "Ryder (Robbery Mask)"}, {302, "Emmet (Alternate)"},
        {303, "Andre (Alternate)"}, {304, "Kendl Johnson (Alternate)"}, {305, "Jethro"},
        {306, "Zero"}, {307, "T-Bone Mendez"}, {308, "Johnny Sindacco (Alternate)"},
        {309, "Janitor (Alternate)"}, {310, "Big Bear (Thin)"}, {311, "Big Smoke (Armored)"},
        {312, "Psycho"}
    };
    for (const auto& entry : names) if (entry.first == model) return entry.second;
    return "Custom / Modded Ped";
}

bool Hovered(float x, float y, float w, float h) {
    auto& st = In().State();
    return st.mouseX >= x && st.mouseX < x + w && st.mouseY >= y && st.mouseY < y + h;
}

std::string Fit(FontId f, std::string value, float width) {
    auto& g = Gfx();
    if (g.TextWidth(f, value) <= width) return value;
    std::wstring wide = Widen(value);
    while (!wide.empty() && g.TextWidthW(f, wide + L"…") > width) wide.pop_back();
    return Narrow(wide + L"…");
}

void Note(const std::string& value, bool error) {
    g_note = value;
    g_noteError = error;
    g_noteUntil = NowMs() + 6000;
}

void ApplySelected(int model) {
    std::string why;
    if (!skin::Choose(model, why)) {
        Note(why, true);
        return;
    }
    Note(model == 0 ? std::string(TR("Вы снова CJ.", "You are CJ again."))
                    : Format(TR("Скин %d выбран.", "Skin %d selected."), model), false);
    SetSkinsMenuVisible(false);
}
}  // namespace

void SetSkinsMenuVisible(bool visible) {
    if (visible == g_visible) return;
    g_visible = visible;
    g_noteUntil = 0;
    if (visible) {
        if (TheChat().IsOpen()) TheChat().Close();
        const auto& models = skin::Models();
        int current = skin::Current();
        auto it = std::find(models.begin(), models.end(), current);
        g_selected = it == models.end() ? 0 : (int)(it - models.begin());
        g_scroll = 0;
        g_view = 0.0f;
        return;
    }
    anims::PreviewHide();
    g_dragX = -1.0f;
    if (!std::exchange(g_captured, false) || core::ConnectMenuVisible()) return;
    In().SetKeyboardCapture(false);
    In().SetMouseCapture(false);
}

bool IsSkinsMenuVisible() { return g_visible; }

void ToggleSkinsMenu() {
    if (g_visible) return SetSkinsMenuVisible(false);
    if (!core::IsPlayerReady() || game::MenuActive() || core::ConnectMenuVisible() || Gfx().Failed()) return;
    SetSkinsMenuVisible(true);
}

void RenderSkinsMenu() {
    if (!g_visible) return;
    if (!core::IsPlayerReady() || game::MenuActive() || core::ConnectMenuVisible())
        return SetSkinsMenuVisible(false);

    bool first = !g_captured;
    if (first) {
        In().SetKeyboardCapture(true);
        In().SetMouseCapture(true);
        g_captured = true;
    }

    auto& g = Gfx();
    auto& st = In().State();
    float s = g.Scale();
    float lh = g.LineHeight(FONT_NORMAL), sh = g.LineHeight(FONT_SMALL);
    float mh = g.LineHeight(FONT_MENU), pad = 28 * s, room = mh * 1.6f;
    float W = std::min(900 * s, g.Width() - 32 * s), H = std::min(650 * s, g.Height() - 32 * s);
    float x = (g.Width() - W) * 0.5f, y = std::max(16 * s, (g.Height() - H) * 0.45f);
    float cx = x + pad, cy = y + 22 * s, cw = W - 2 * pad;
    const auto& models = skin::Models();
    int count = (int)models.size();
    g_selected = count ? std::clamp(g_selected, 0, count - 1) : 0;

    w::BeginFrame();
    g.Rect(x, y, W, H, 0xF0000000);
    g.RectOutline(x, y, W, H, kSaLine, std::max(1.0f, s));
    g.Text(FONT_MENU, cx, cy, TR("Скины", "Skins"), kSaBlue, TEXT_SHADOW);
    cy += mh + 18 * s;

    bool moved = false, apply = false, close = false;
    int pages = 0;
    if (!first) {
        for (int vk : st.keyDowns) {
            if (vk == VK_UP) --g_selected, moved = true;
            if (vk == VK_DOWN) ++g_selected, moved = true;
            if (vk == VK_PRIOR) --pages;
            if (vk == VK_NEXT) ++pages;
            if (vk == VK_HOME) g_selected = 0, moved = true;
            if (vk == VK_END) g_selected = count - 1, moved = true;
            if (vk == VK_RETURN) apply = true;
            if (vk == VK_ESCAPE) close = true;
        }
    }

    if (pages) moved = true;

    float rowH = lh + 12 * s;
    float footY = y + H - 18 * s - rowH;
    float bodyH = footY - sh - 24 * s - cy;
    float previewW = std::min(bodyH, cw * 0.43f);
    float listW = cw - previewW - 24 * s;
    int shown = std::max(1, (int)(bodyH / rowH));
    if (pages) g_selected += pages * std::max(1, shown - 1);
    g_selected = count ? std::clamp(g_selected, 0, count - 1) : 0;
    int listHRows = count;
    float listH = (float)shown * rowH - 3 * s;
    if (st.wheel && Hovered(cx, cy, listW, listH)) g_scroll -= st.wheel * 3;
    if (moved) g_scroll = std::clamp(g_selected - shown / 2, 0, std::max(0, listHRows - shown));
    g_scroll = std::clamp(g_scroll, 0, std::max(0, listHRows - shown));
    uint64_t now = NowMs();
    g_view += ((float)g_scroll - g_view) * (1.0f - std::exp(-(float)std::min<uint64_t>(now - g_viewMs, 100) / 60.0f));
    g_viewMs = now;
    if (std::fabs((float)g_scroll - g_view) < 0.01f) g_view = (float)g_scroll;

    g.Clip(cx, cy, listW, listH);
    for (int i = (int)g_view; i < std::min(count, (int)g_view + shown + 1); ++i) {
        float ry = cy + ((float)i - g_view) * rowH;
        bool hover = Hovered(cx, ry, listW, rowH) && Hovered(cx, cy, listW, listH);
        g.Rect(cx, ry, listW, rowH - 3 * s, i == g_selected ? 0xFF16202A : hover ? 0xFF10161D : 0xFF090C10);
        int model = models[i];
        std::string label = Format("%s  (ID %d)", SkinName(model), model);
        g.Text(FONT_NORMAL, cx + 16 * s, ry + (rowH - lh) * 0.5f, label,
               i == g_selected ? kSaBlue : kText, TEXT_SHADOW);
        if (hover && st.lmbPressed) {
            if (g_selected == i && g_clickModel == model && NowMs() - g_clickMs < 450) apply = true;
            g_selected = i;
            g_clickModel = model;
            g_clickMs = NowMs();
        }
    }
    g.ClipEnd();
    if (count > shown) {
        float bh = listH * (float)shown / (float)count, by = cy + listH * g_view / (float)count;
        g.Rect(cx + listW - 4 * s, by, 3 * s, bh, kSaMuted);
    }

    float px = cx + listW + 24 * s, py = cy;
    g.Rect(px, py, previewW, previewW, 0xFF0C1117);
    g.RectOutline(px, py, previewW, previewW, kSaLine, std::max(1.0f, s));
    int selectedModel = count ? models[g_selected] : 0;
    anims::SkinPreviewShow(selectedModel, previewW);
    if (void* texture = anims::PreviewTexture()) g.Image(texture, px, py, previewW, previewW);
    else {
        const char* loading = TR("Загрузка модели…", "Loading model…");
        float loadingW = g.TextWidth(FONT_SMALL, loading);
        g.Text(FONT_SMALL, px + (previewW - loadingW) * 0.5f, py + previewW * 0.5f, loading, kSaMuted, TEXT_SHADOW);
    }
    const game::CControllerState& controllerState = game::GetPad(0)->NewState;
    if (std::abs(controllerState.RightStickX) > 40)
        anims::PreviewTurn((float)controllerState.RightStickX / 128.0f * 0.055f);
    if (st.lmb && (g_dragX >= 0.0f || (st.lmbPressed && Hovered(px, py, previewW, previewW)))) {
        if (g_dragX >= 0.0f) anims::PreviewTurn((st.mouseX - g_dragX) * 0.01f);
        g_dragX = st.mouseX;
    } else {
        g_dragX = -1.0f;
    }
    std::string current = Format(TR("Текущий скин: %d", "Current skin: %d"), skin::Current());
    g.Text(FONT_SMALL, px, py + previewW + 14 * s, Fit(FONT_SMALL, current, previewW), kSaMuted, TEXT_SHADOW);
    g.Text(FONT_SMALL, px, py + previewW + 14 * s + sh + 5 * s,
           Fit(FONT_SMALL, TR("D-pad / левый стик: курсор", "D-pad / left stick: move cursor"), previewW), kSaMuted, TEXT_SHADOW);
    g.Text(FONT_SMALL, px, py + previewW + 14 * s + sh * 2 + 10 * s,
           Fit(FONT_SMALL, TR("Cross: выбрать  Circle: назад  L2/R2: прокрутка",
                              "Cross: select  Circle: back  L2/R2: scroll"), previewW), kSaMuted, TEXT_SHADOW);

    float noteY = footY - sh - 12 * s;
    if (NowMs() < g_noteUntil) g.Text(FONT_SMALL, cx, noteY, Fit(FONT_SMALL, g_note, cw), g_noteError ? kSaRed : kSaBlue, TEXT_SHADOW);
    else g.Text(FONT_SMALL, cx, noteY, TR("Выберите модель персонажа. CJ возвращает стандартный вид.",
                                          "Choose a character model. CJ restores the default appearance."), kSaMuted, TEXT_SHADOW);

    const char* applyText = TR("Выбрать", "Apply");
    const char* closeText = TR("Закрыть", "Close");
    float closeX = cx + cw - g.TextWidth(FONT_MENU, closeText);
    float applyX = closeX - room - 40 * s - g.TextWidth(FONT_MENU, applyText);
    bool applyClick = w::Button("skin-apply", applyX - room, footY, g.TextWidth(FONT_MENU, applyText) + 2 * room, rowH,
                                applyText, count > 0);
    bool closeClick = w::Button("skin-close", closeX - room, footY, g.TextWidth(FONT_MENU, closeText) + 2 * room, rowH, closeText);
    w::EndFrame();
    w::DrawCursor();
    if (apply || applyClick) ApplySelected(selectedModel);
    else if (close || closeClick) SetSkinsMenuVisible(false);
}

}  // namespace coop::ui
