// The skin picker: the game's available ped models, using the same window and list style as the U animation menu.
#include "pages/skins-menu/SkinsMenu.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "common/lib/util/Util.h"
#include "features/skin/Skin.h"
#include "features/anims/Emotes.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"
#include "shared/config/Config.h"

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

    bool moved = false, apply = false;
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
        std::string label = model == 0 ? TR("0    CJ (default)", "0    CJ (default)") : std::to_string(model);
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
    if (st.lmb && (g_dragX >= 0.0f || (st.lmbPressed && Hovered(px, py, previewW, previewW)))) {
        if (g_dragX >= 0.0f) anims::PreviewTurn((st.mouseX - g_dragX) * 0.01f);
        g_dragX = st.mouseX;
    } else {
        g_dragX = -1.0f;
    }
    std::string current = Format(TR("Текущий скин: %d", "Current skin: %d"), skin::Current());
    g.Text(FONT_SMALL, px, py + previewW + 14 * s, Fit(FONT_SMALL, current, previewW), kSaMuted, TEXT_SHADOW);
    g.Text(FONT_SMALL, px, py + previewW + 14 * s + sh + 5 * s,
           TR("Enter / двойной щелчок: выбрать", "Enter / double-click: apply"), kSaMuted, TEXT_SHADOW);

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
    bool close = w::Button("skin-close", closeX - room, footY, g.TextWidth(FONT_MENU, closeText) + 2 * room, rowH, closeText);
    w::EndFrame();
    w::DrawCursor();
    if (apply || applyClick) ApplySelected(selectedModel);
    else if (close) SetSkinsMenuVisible(false);
}

}  // namespace coop::ui
