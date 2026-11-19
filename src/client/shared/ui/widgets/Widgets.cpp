#include "shared/ui/widgets/Widgets.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

#include "common/lib/util/Util.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"

namespace coop::ui::w {

namespace {
std::string g_focus;          // id of the focused text box
std::string g_firstTextBox;   // for Tab cycling
std::vector<std::string> g_textBoxes;
bool g_tabPressed = false, g_shiftTab = false;
size_t g_caret = std::string::npos;
uint64_t g_caretTime = 0;
std::wstring* g_focusedValue = nullptr;
size_t g_focusedMaxLen = 0;
bool g_focusedDigitsOnly = false;


bool Hover(float x, float y, float w, float h) {
    auto& st = In().State();
    return st.mouseX >= x && st.mouseX < x + w && st.mouseY >= y && st.mouseY < y + h;
}

void Bevel(float x, float y, float width, float height, bool pressed = false) {
    const auto& theme = Theme();
    if (theme.style != UiStyle::Beveled) return;
    auto& g = Gfx();
    const float edge = std::max(1.0f, g.Scale());
    const uint32_t light = pressed ? theme.bevelDark : theme.bevelLight;
    const uint32_t dark = pressed ? theme.bevelLight : theme.bevelDark;
    g.Rect(x, y, width, edge, light);
    g.Rect(x, y, edge, height, light);
    g.Rect(x, y + height - edge, width, edge, dark);
    g.Rect(x + width - edge, y, edge, height, dark);
}

void CutCorners(float x, float y, float w, float h, float radius, uint32_t color, bool round) {
    auto& g = Gfx();
    radius = std::min(radius, std::min(w, h) * 0.5f);
    if (radius <= 0.0f) return;
    const int bands = std::max(1, (int)std::ceil(radius));
    const float step = radius / bands;
    g.Rect(x, y + radius, w, h - 2 * radius, color);
    for (int i = 0; i < bands; ++i) {
        float dy = radius - (i + 0.5f) * step;
        float inset = round ? radius - std::sqrt(radius * radius - dy * dy) : dy;
        g.Rect(x + inset, y + i * step, w - 2 * inset, step, color);
        g.Rect(x + inset, y + h - (i + 1) * step, w - 2 * inset, step, color);
    }
}

void Frame(float x, float y, float w, float h, uint32_t fill, uint32_t edge, float radius = 7.0f) {
    auto& g = Gfx();
    auto style = Theme().style;
    const float stroke = std::max(1.0f, g.Scale());
    if (style == UiStyle::Glass || style == UiStyle::Arcade) {
        bool round = style == UiStyle::Glass;
        float corner = radius * g.Scale();
        // Inset borders keep translucent fills from stacking over a solid frame.
        CutCorners(x, y, w, h, corner, WithAlpha(edge, 0.55f), round);
        CutCorners(x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, corner - stroke, fill, round);
        return;
    }
    g.Rect(x, y, w, h, fill);
    g.RectOutline(x, y, w, h, edge, stroke);
}

bool OnScreenKeyboardHit(float x, float y) {
    if (!In().ControllerCursorActive()) return false;
    auto& g = Gfx();
    float s = g.Scale();
    float w = std::min(650.0f * s, g.Width() - 24.0f * s);
    float h = 166.0f * s;
    float left = (g.Width() - w) * 0.5f;
    float top = g.Height() - h - 12.0f * s;
    return x >= left && x < left + w && y >= top && y < top + h;
}
}  // namespace

void BeginFrame() {
    g_textBoxes.clear();
    auto& st = In().State();
    g_tabPressed = false;
    for (int vk : st.keyDowns)
        if (vk == VK_TAB) {
            g_tabPressed = true;
            g_shiftTab = st.shift;
        }
    g_focusedValue = nullptr;
    // Clicking outside text boxes removes focus, except when the click is on the
    // controller on-screen keyboard rendered at the end of the previous frame.
    if (st.lmbPressed && !OnScreenKeyboardHit(st.mouseX, st.mouseY)) g_focus.clear();
}

void EndFrame() {
    if (g_tabPressed && !g_textBoxes.empty()) {
        size_t idx = 0;
        for (size_t i = 0; i < g_textBoxes.size(); ++i)
            if (g_textBoxes[i] == g_focus) idx = i;
        if (g_focus.empty()) idx = g_shiftTab ? g_textBoxes.size() - 1 : 0;
        else idx = g_shiftTab ? (idx + g_textBoxes.size() - 1) % g_textBoxes.size() : (idx + 1) % g_textBoxes.size();
        g_focus = g_textBoxes[idx];
        g_caret = std::string::npos;
    }

    if (!g_focus.empty() && g_focusedValue && In().ControllerCursorActive()) {
        auto& st = In().State();
        auto& g = Gfx();
        auto& value = *g_focusedValue;
        float s = g.Scale();
        float w = std::min(650.0f * s, g.Width() - 24.0f * s), h = 166.0f * s;
        float x = (g.Width() - w) * 0.5f, y = g.Height() - h - 12.0f * s;
        g.Rect(x, y, w, h, Theme().dialog);
        g.RectOutline(x, y, w, h, Theme().accent, std::max(1.0f, s));
        g.Text(FONT_SMALL, x + 10 * s, y + 5 * s, "Controller keyboard  -  Cross: select", Theme().muted, TEXT_SHADOW);

        auto key = [&](float kx, float ky, float kw, const std::wstring& label, wchar_t ch, bool special = false) {
            bool hover = Hover(kx, ky, kw, 27 * s);
            g.Rect(kx, ky, kw, 27 * s, hover ? Theme().hover : Theme().preview);
            g.RectOutline(kx, ky, kw, 27 * s, hover ? Theme().accent : Theme().border, std::max(1.0f, s));
            float tw = g.TextWidthW(FONT_SMALL, label);
            g.TextW(FONT_SMALL, kx + (kw - tw) * 0.5f, ky + 6 * s, label, hover ? Theme().accent : Theme().text, TEXT_SHADOW);
            if (!hover || !st.lmbPressed) return;
            if (special && ch == L'\b') {
                if (g_caret == std::string::npos || g_caret > value.size()) g_caret = value.size();
                if (g_caret > 0) value.erase(--g_caret, 1);
            } else if (special && ch == L'\n') {
                g_focus.clear();
                g_focusedValue = nullptr;
            } else if (special && ch == L' ') {
                if (!g_focusedDigitsOnly && value.size() < g_focusedMaxLen) {
                    if (g_caret == std::string::npos || g_caret > value.size()) g_caret = value.size();
                    value.insert(value.begin() + (ptrdiff_t)g_caret++, L' ');
                }
            } else if (value.size() < g_focusedMaxLen && ch) {
                if (g_caret == std::string::npos || g_caret > value.size()) g_caret = value.size();
                value.insert(value.begin() + (ptrdiff_t)g_caret++, ch);
            }
        };

        const float gap = 4 * s, kh = 27 * s;
        float rowY = y + 27 * s;
        if (g_focusedDigitsOnly) {
            const wchar_t* digits = L"1234567890";
            float kw = (w - 11 * gap) / 10.0f;
            for (int i = 0; i < 10; ++i) key(x + gap + i * (kw + gap), rowY, kw, std::wstring(1, digits[i]), digits[i]);
            rowY += kh + gap;
            const wchar_t* extra = L"-.";
            float ew = 70 * s;
            for (int i = 0; i < 2; ++i) key(x + (w - (2 * ew + gap)) * 0.5f + i * (ew + gap), rowY, ew,
                                               std::wstring(1, extra[i]), extra[i]);
            rowY += kh + gap;
        } else {
            const wchar_t* rows[] = {L"qwertyuiop", L"asdfghjkl", L"zxcvbnm_- ."};
            for (int r = 0; r < 3; ++r) {
                int n = (int)std::wcslen(rows[r]);
                float kw = (w - (n + 1) * gap) / n;
                float left = x + (w - (n * kw + (n - 1) * gap)) * 0.5f;
                for (int i = 0; i < n; ++i) {
                    wchar_t c = rows[r][i];
                    if (c == L' ') continue;
                    key(left + i * (kw + gap), rowY, kw, std::wstring(1, c), c);
                }
                rowY += kh + gap;
            }
        }
        float bottomY = y + h - kh - 5 * s;
        float backW = 90 * s, spaceW = 150 * s, doneW = 90 * s;
        key(x + 8 * s, bottomY, backW, L"Backspace", L'\b', true);
        if (!g_focusedDigitsOnly) key(x + (w - spaceW) * 0.5f, bottomY, spaceW, L"Space", L' ', true);
        key(x + w - doneW - 8 * s, bottomY, doneW, L"Done", L'\n', true);
    }
}

void SetFocus(const char* id) {
    g_focus = id;
    g_caret = std::string::npos;
}

bool AnyTextFocused() { return !g_focus.empty(); }

float Panel(float x, float y, float w, float h, const char* asciiTitle) {
    auto& g = Gfx();
    float s = g.Scale();
    MenuSurface(x, y, w, h, Theme().panel);
    // the title in the game's gothic font, like "Main Menu" on the game's own pages
    float th = 58 * s;
    if (Theme().style != UiStyle::Classic) {
        MenuTitle(x + 35 * s, y + 16 * s + (th - g.LineHeight(FONT_MENU)) * 0.5f, asciiTitle);
    } else {
        g.GameText(GAME_FONT_GOTHIC, x + 52 * s, y + 16 * s, asciiTitle, th, Theme().titleText);
    }
    return y + 16 * s + th;
}

void MenuSurface(float x, float y, float w, float h, uint32_t fill) {
    auto& g = Gfx();
    const auto& theme = Theme();
    Frame(x, y, w, h, fill, theme.border, 13);
    Bevel(x, y, w, h);
    if (theme.style == UiStyle::Atlas)
        g.Rect(x, y, w, 4 * g.Scale(), theme.accent);
    if (theme.style == UiStyle::Terminal) {
        float inset = 4 * g.Scale();
        g.RectOutline(x + inset, y + inset, w - 2 * inset, h - 2 * inset, theme.border);
    }
}

void MenuTitle(float x, float y, const std::string& title) {
    auto& g = Gfx();
    const auto& theme = Theme();
    float s = g.Scale();
    FontId font = theme.style == UiStyle::Terminal ? FONT_TERMINAL : FONT_MENU;
    float width = g.TextWidth(font, title), height = g.LineHeight(FONT_MENU);
    if (theme.style == UiStyle::Beveled || theme.style == UiStyle::Arcade) {
        Frame(x - 8 * s, y - 2 * s, width + 16 * s, height + 4 * s, theme.titleFill, theme.border);
    } else if (theme.style != UiStyle::Classic) {
        float length = theme.style == UiStyle::Atlas ? width : std::min(width, 44 * s);
        g.Rect(x, y + height, length, 2 * s, theme.accent);
    }
    g.Text(font, x, y + (height - g.LineHeight(font)) * 0.5f, title, theme.titleText,
           theme.style == UiStyle::Classic ? TEXT_SHADOW : 0);
}

void MenuRow(float x, float y, float w, float h, bool selected, bool hover) {
    auto& g = Gfx();
    const auto& theme = Theme();
    uint32_t fill = selected ? theme.selection : hover ? theme.hover : theme.field;
    if ((selected || hover) && (theme.style == UiStyle::Glass || theme.style == UiStyle::Arcade))
        Frame(x, y, w, h, fill, theme.accent);
    else
        g.Rect(x, y, w, h, fill);
    if (selected && theme.style == UiStyle::Atlas) g.Rect(x, y, 4 * g.Scale(), h, theme.strongText);
}

// A menu item: text centred in (x, y, w, h), light blue when selected or under the mouse. The game's star beside it came
// over the next word on the tight tabs of the U window (the user's word, 04.11).
static bool MenuItem(float x, float y, float w, float h, FontId font, const std::string& label, bool enabled, bool selected) {
    auto& g = Gfx();
    bool hover = enabled && Hover(x, y, w, h);
    bool lit = enabled && (selected || hover);
    const auto& theme = Theme();
    if (font != FONT_MENU_SMALL) {
        if (theme.style == UiStyle::Terminal) font = FONT_TERMINAL;
        if (theme.style == UiStyle::Glass) font = FONT_NORMAL;
    }
    float tx = x + (w - g.TextWidth(font, label)) * 0.5f, ty = y + (h - g.LineHeight(font)) * 0.5f;
    const float s = g.Scale();
    const float ix = x + 2 * s, iy = y + 2 * s, iw = w - 4 * s, ih = h - 4 * s;
    switch (theme.style) {
        case UiStyle::Classic: break;
        case UiStyle::Beveled:
            g.Rect(ix, iy, iw, ih, lit ? theme.selection : theme.field);
            Bevel(ix, iy, iw, ih, selected);
            break;
        case UiStyle::Glass:
            if (lit) Frame(ix, iy, iw, ih, theme.selection, theme.accent, 12);
            break;
        case UiStyle::Atlas:
            g.Rect(ix, iy, iw, ih, lit ? theme.selection : theme.field);
            if (lit) g.Rect(ix, iy, 4 * s, ih, theme.strongText);
            break;
        case UiStyle::Arcade:
            Frame(ix, iy, iw, ih, lit ? theme.selection : theme.field, theme.border, 9);
            break;
        case UiStyle::Terminal:
            if (lit) g.Rect(ix, iy, iw, ih, theme.selection);
            g.Rect(ix, iy + 2 * s, 2 * s, ih - 4 * s, theme.border);
            g.Rect(ix + iw - 2 * s, iy + 2 * s, 2 * s, ih - 4 * s, theme.border);
            for (float by : {iy + 2 * s, iy + ih - 4 * s}) {
                g.Rect(ix, by, 5 * s, 2 * s, theme.border);
                g.Rect(ix + iw - 5 * s, by, 5 * s, 2 * s, theme.border);
            }
            break;
    }
    g.Text(font, tx, ty, label, !enabled ? theme.disabled : lit ? theme.selectedText : theme.idle,
           theme.style == UiStyle::Classic ? TEXT_SHADOW : 0);
    return hover && In().State().lmbPressed;
}

bool Button(const char* id, float x, float y, float w, float h, const std::string& label, bool enabled, bool lit,
            bool big) {
    return MenuItem(x, y, w, h, big ? FONT_BIG : FONT_MENU, label, enabled, lit);
}

bool Tab(const char* id, float x, float y, float w, float h, const std::string& label, bool active, bool small) {
    return MenuItem(x, y, w, h, small ? FONT_MENU_SMALL : FONT_MENU, label, true, active) && !active;
}

std::wstring ClipboardText() {
    std::wstring clip;
    if (!OpenClipboard(nullptr)) return clip;
    if (HANDLE hc = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = (const wchar_t*)GlobalLock(hc)) {
            clip = p;
            GlobalUnlock(hc);
        }
    }
    CloseClipboard();
    return clip;
}

bool TextBox(const char* id, float x, float y, float w, float h, std::wstring& value, size_t maxLen, bool password,
             bool digitsOnly, const char* placeholder) {
    auto& g = Gfx();
    float s = g.Scale();
    auto& st = In().State();
    g_textBoxes.push_back(id);
    bool hover = Hover(x, y, w, h);
    if (hover && st.lmbPressed) {
        g_focus = id;
        g_caret = std::string::npos;
    }
    bool focused = g_focus == id;
    if (focused) {
        g_focusedValue = &value;
        g_focusedMaxLen = maxLen;
        g_focusedDigitsOnly = digitsOnly;
    }
    bool enter = false;
    // the caret belongs to the focused box only (an empty box after it used to reset it every frame)
    if (focused && (g_caret == std::string::npos || g_caret > value.size())) g_caret = value.size();
    if (focused) {
        for (int vk : st.keyDowns) {
            switch (vk) {
                case VK_BACK:
                    if (g_caret > 0) value.erase(--g_caret, 1);
                    break;
                case VK_DELETE:
                    if (g_caret < value.size()) value.erase(g_caret, 1);
                    break;
                case VK_LEFT:
                    if (g_caret > 0) --g_caret;
                    break;
                case VK_RIGHT:
                    if (g_caret < value.size()) ++g_caret;
                    break;
                case VK_HOME:
                    g_caret = 0;
                    break;
                case VK_END:
                    g_caret = value.size();
                    break;
                case VK_RETURN:
                    enter = true;
                    break;
                case 'V':
                    if (st.ctrl)
                        for (wchar_t c : ClipboardText()) {
                            if (value.size() >= maxLen) break;
                            if (c < 0x20 || (digitsOnly && (c < L'0' || c > L'9'))) continue;
                            value.insert(value.begin() + (ptrdiff_t)g_caret++, c);
                        }
                    break;
                default:
                    break;
            }
        }
        for (wchar_t c : st.chars) {
            if (st.ctrl || value.size() >= maxLen) continue;
            if (digitsOnly && (c < L'0' || c > L'9')) continue;
            value.insert(value.begin() + (ptrdiff_t)g_caret++, c);
            g_caretTime = NowMs();
        }
    }
    Frame(x, y, w, h, Theme().field, focused ? Theme().accent : (hover ? Theme().idle : Theme().border));
    Bevel(x, y, w, h, true);
    std::wstring shown = password ? std::wstring(value.size(), L'•') : value;
    float ty = y + (h - g.LineHeight(FONT_NORMAL)) * 0.5f;
    float pad = 8 * s;
    float caretX = g.TextWidthW(FONT_NORMAL, std::wstring_view(shown).substr(0, focused ? g_caret : 0));
    float offset = std::max(0.0f, caretX - (w - 2 * pad));
    // crude clipping: drop characters that scrolled out on the left
    std::wstring visible = shown;
    float cut = 0;
    while (!visible.empty() && cut < offset) {
        cut += g.TextWidthW(FONT_NORMAL, visible.substr(0, 1));
        visible.erase(0, 1);
    }
    if (value.empty() && !focused && placeholder) g.Text(FONT_NORMAL, x + pad, ty, placeholder, Theme().idle, 0);
    else g.TextW(FONT_NORMAL, x + pad, ty, visible, Theme().strongText, 0);
    if (focused && ((NowMs() - g_caretTime) / 500) % 2 == 0)
        g.Rect(x + pad + caretX - cut, ty + 2 * s, 2 * s, g.LineHeight(FONT_NORMAL) - 4 * s, Theme().accent);
    return enter;
}

int Selector(const char* id, float x, float y, float w, float h, const std::string& text) {
    auto& g = Gfx();
    float bw = h;
    int r = 0;
    Frame(x, y, w, h, Theme().field, Theme().border);
    auto arrow = [&](float ax, const char* glyph) {
        bool hover = Hover(ax, y, bw, h);
        g.Text(FONT_MENU, ax + bw * 0.5f, y + (h - g.LineHeight(FONT_MENU)) * 0.5f, glyph, hover ? Theme().accent : Theme().idle,
               TEXT_CENTER);
        return hover && In().State().lmbPressed;
    };
    if (arrow(x, "<")) r = -1;
    if (arrow(x + w - bw, ">")) r = +1;
    float ty = y + (h - g.LineHeight(FONT_NORMAL)) * 0.5f;
    if (w > 2 * bw) {
        g.Clip(x + bw, y, w - 2 * bw, h);
        g.Text(FONT_NORMAL, x + w * 0.5f, ty, text, Theme().strongText, TEXT_CENTER | TEXT_SHADOW);
        g.ClipEnd();
    }
    return r;
}

bool Checkbox(const char* id, float x, float y, const std::string& label, bool& value) {
    auto& g = Gfx();
    float s = g.Scale();
    float box = g.LineHeight(FONT_NORMAL) * 0.8f;
    float tw = g.TextWidth(FONT_NORMAL, label);
    bool hover = Hover(x, y, box + 10 * s + tw, box);
    g.Rect(x, y, box, box, Theme().field);
    g.RectOutline(x, y, box, box, hover ? Theme().accent : Theme().idle, std::max(1.0f, 1.5f * s));
    if (value) g.Rect(x + box * 0.22f, y + box * 0.22f, box * 0.56f, box * 0.56f, Theme().accent);
    g.Text(FONT_NORMAL, x + box + 10 * s, y - box * 0.1f, label, Theme().text, TEXT_SHADOW);
    if (hover && In().State().lmbPressed) {
        value = !value;
        return true;
    }
    return false;
}

void DrawCursor() {
    auto& g = Gfx();
    auto& st = In().State();
    float s = std::max(1.0f, g.Scale());
    float x = st.mouseX, y = st.mouseY;
    // simple arrow made of rectangles
    for (int i = 0; i < 14; ++i) {
        float len = (float)(i < 10 ? i + 1 : 14 - i) * s;
        g.Rect(x, y + i * s, len + 2 * s, s, 0xFF000000);
        g.Rect(x + s, y + i * s, std::max(0.0f, len - s), s, 0xFFFFFFFF);
    }
}

}  // namespace coop::ui::w
