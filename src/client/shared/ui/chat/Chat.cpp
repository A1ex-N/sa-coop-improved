#include "shared/ui/chat/Chat.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::ui {

Chat& TheChat() {
    static Chat c;
    return c;
}

static constexpr size_t kMaxLines = 200;
static constexpr int kVisibleLines = 10;
static constexpr size_t kMaxInput = 160;
static constexpr uint64_t kFadeDurationMs = 2000;

enum HelpTab { HelpChat, HelpPlayer, HelpTravel, HelpSession, HelpHost, HelpTabCount };

static int VisibleHelpTabs() { return Net().IsHost() ? HelpTabCount : HelpHost; }

void Chat::Add(const std::string& text, uint32_t argb) {
    LOGI("[chat] %s", text.c_str());
    m_lines.push_back({Widen(text), argb, NowMs(), {}});
    while (m_lines.size() > kMaxLines) m_lines.pop_front();
}

void Chat::AddPlayer(const std::string& name, const std::string& text, uint32_t nameColor) {
    LOGI("[chat] %s: %s", name.c_str(), text.c_str());
    m_lines.push_back({Widen(text), 0xFFF4F1EE, NowMs(), Widen(name), nameColor});
    while (m_lines.size() > kMaxLines) m_lines.pop_front();
}

void Chat::Open() {
    if (IsOpen()) return;
    m_open = true;
    m_openTime = NowMs();
    m_input.clear();
    m_cursor = 0;
    m_histPos = -1;
    m_scroll = 0;
    In().SetKeyboardCapture(true);
}

void Chat::OpenHelp() {
    m_open = false;
    m_helpOpen = true;
    m_helpTab = HelpChat;
    m_scroll = 0;
    In().SetKeyboardCapture(true);
    In().SetMouseCapture(true);
}

void Chat::Close() {
    if (!IsOpen()) return;
    if (m_helpOpen) In().SetMouseCapture(false);
    m_helpOpen = false;
    m_open = false;
    m_scroll = 0;
    In().SetKeyboardCapture(false);
}

static std::wstring ReadClipboard() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = (const wchar_t*)GlobalLock(h)) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    for (auto& c : out)
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return out;
}

void Chat::ProcessInput() {
    if (m_helpOpen) {
        int tabs = VisibleHelpTabs();
        m_helpTab = std::min(m_helpTab, tabs - 1);
        for (int vk : In().State().keyDowns) {
            if (vk == VK_ESCAPE) {
                Close();
                return;
            }
            if (vk == VK_TAB || vk == VK_LEFT || vk == VK_RIGHT) {
                bool previous = vk == VK_LEFT || (vk == VK_TAB && In().State().shift);
                m_helpTab = (m_helpTab + (previous ? tabs - 1 : 1)) % tabs;
            }
        }
        return;
    }
    if (!m_open) return;
    auto& st = In().State();
    for (int vk : st.keyDowns) {
        switch (vk) {
            case VK_RETURN: {
                std::wstring text = m_input;
                Close();
                // trim
                while (!text.empty() && text.back() == L' ') text.pop_back();
                size_t start = text.find_first_not_of(L' ');
                if (start == std::wstring::npos) return;
                text = text.substr(start);
                if (m_history.empty() || m_history.back() != text) m_history.push_back(text);
                if (m_history.size() > 50) m_history.erase(m_history.begin());
                if (onSubmit) onSubmit(Narrow(text));
                return;
            }
            case VK_ESCAPE:
                Close();
                return;
            case VK_BACK:
                if (m_cursor > 0) {
                    m_input.erase(m_cursor - 1, 1);
                    --m_cursor;
                }
                break;
            case VK_DELETE:
                if (m_cursor < m_input.size()) m_input.erase(m_cursor, 1);
                break;
            case VK_LEFT:
                if (m_cursor > 0) --m_cursor;
                break;
            case VK_RIGHT:
                if (m_cursor < m_input.size()) ++m_cursor;
                break;
            case VK_HOME:
                m_cursor = 0;
                break;
            case VK_END:
                m_cursor = m_input.size();
                break;
            case VK_UP:
                if (!m_history.empty()) {
                    if (m_histPos < 0) m_histPos = (int)m_history.size();
                    if (m_histPos > 0) --m_histPos;
                    m_input = m_history[m_histPos];
                    m_cursor = m_input.size();
                }
                break;
            case VK_DOWN:
                if (m_histPos >= 0) {
                    ++m_histPos;
                    if (m_histPos >= (int)m_history.size()) {
                        m_histPos = -1;
                        m_input.clear();
                    } else {
                        m_input = m_history[m_histPos];
                    }
                    m_cursor = m_input.size();
                }
                break;
            case VK_PRIOR:
                m_scroll = std::min<int>(m_scroll + 5, std::max<int>(0, (int)m_lines.size() - kVisibleLines));
                break;
            case VK_NEXT:
                m_scroll = std::max(0, m_scroll - 5);
                break;
            case 'V':
                if (st.ctrl) {
                    std::wstring clip = ReadClipboard();
                    size_t room = kMaxInput > m_input.size() ? kMaxInput - m_input.size() : 0;
                    if (clip.size() > room) clip.resize(room);
                    m_input.insert(m_cursor, clip);
                    m_cursor += clip.size();
                }
                break;
            default:
                break;
        }
    }
    for (wchar_t c : st.chars) {
        if (st.ctrl) continue;  // Ctrl+V etc. produce control chars; already filtered
        if (m_input.size() >= kMaxInput) break;
        m_input.insert(m_input.begin() + (ptrdiff_t)m_cursor, c);
        ++m_cursor;
    }
    if (st.wheel) {
        m_scroll = std::clamp(m_scroll + st.wheel, 0, std::max<int>(0, (int)m_lines.size() - kVisibleLines));
    }
}

void Chat::RenderHelp(float x, float y, float width) {
    auto& g = Gfx();
    const auto& input = In().State();
    const float s = g.Scale();
    const float padding = 22 * s;
    const float lh = g.LineHeight(FONT_NORMAL);
    const float small = g.LineHeight(FONT_SMALL);
    const float left = x + padding;
    const float right = x + width - padding;
    const float tabY = y + padding + lh + 14 * s;
    const float tabHeight = small + 18 * s;
    const float dividerY = tabY + tabHeight;
    const float closeRight = right;
    const wchar_t* closeLabel = TR(L"Esc Закрыть", L"Esc Close");
    const float closeWidth = g.TextWidthW(FONT_SMALL, closeLabel) + 20 * s;
    auto hover = [&](float px, float py, float pw, float ph) {
        return input.mouseX >= px && input.mouseX < px + pw && input.mouseY >= py && input.mouseY < py + ph;
    };
    bool closeHovered = hover(closeRight - closeWidth, y + padding, closeWidth, lh);
    if (closeHovered && input.lmbPressed) {
        Close();
        return;
    }

    const int tabs = VisibleHelpTabs();
    const std::array<const wchar_t*, HelpTabCount> labels{
        TR(L"Чат", L"Chat"), TR(L"Игрок", L"Player"), TR(L"Поездки", L"Travel"),
        TR(L"Сессия", L"Session"), TR(L"Хост", L"Host"),
    };
    m_helpTab = std::min(m_helpTab, tabs - 1);
    const float tabWidth = (right - left) / tabs;
    for (int i = 0; i < tabs; ++i)
        if (input.lmbPressed && hover(left + i * tabWidth, tabY, tabWidth, tabHeight)) m_helpTab = i;

    struct Command {
        const wchar_t* syntax;
        const wchar_t* description;
    };
    std::vector<Command> commands;
    std::wstring note;
    if (m_helpTab == HelpChat) {
        commands = {
            {L"/players, /status", TR(L"Список игроков и их статус", L"List players and their status")},
            {TR(L"/pm <id> <текст>", L"/pm <id> <text>"), TR(L"Личное сообщение", L"Send a private message")},
            {TR(L"/me <действие>", L"/me <action>"), TR(L"Описать действие", L"Show an action")},
        };
        note = Widen(Format(TR("%s — игроки и переводы денег.", "%s opens the player list and money transfers."),
                            KeyName(Cfg().playerListKey).c_str()));
    } else if (m_helpTab == HelpPlayer) {
        commands = {
            {L"/skin menu", TR(L"Открыть выбор скина", L"Open the skin picker")},
            {TR(L"/skin <номер>", L"/skin <id>"), TR(L"Выбрать скин по номеру. 0 — вернуть CJ.", L"Choose a skin by ID. Use 0 to return to CJ.")},
            {L"/skin next, /skin prev", TR(L"Следующий или предыдущий скин", L"Cycle through skins")},
            {TR(L"/anims [номер]", L"/anims [id]"), TR(L"Открыть меню анимаций или запустить анимацию", L"Open the animation menu or play an animation")},
            {TR(L"/blip [цвет]", L"/blip [colour]"), TR(L"Цвет метки на радаре. Имя цвета, RRGGBB или default.", L"Set your radar marker colour. Use a colour name, RRGGBB, or default.")},
        };
    } else if (m_helpTab == HelpTravel) {
        if (!Net().Online()) {
            note = TR(L"Для поездок подключитесь к серверу.", L"Connect to a server to use travel commands.");
        } else if (!Net().IsHost() && !Net().PartnerTeleport()) {
            note = TR(L"Хост отключил поездки для напарников.", L"The host disabled travel commands for partners.");
        } else {
            bool many = Net().Peers().size() > 1;
            commands = {
                {many ? L"/tp <id>" : L"/tp", TR(L"Попросить переместиться к игроку", L"Ask to go to your partner")},
                {many ? L"/tphere <id>" : L"/tphere", TR(L"Позвать игрока к себе", L"Invite your partner over")},
            };
            note = TR(L"Напарник отвечает Y или N.", L"Your partner answers Y or N.");
            if (many)
                note += L" " + Widen(Format(TR("Номера игроков — в меню %s.", "%s shows player IDs."), KeyName(Cfg().playerListKey).c_str()));
        }
    } else if (m_helpTab == HelpSession) {
        commands = {
            {L"/coop, /menu", TR(L"Открыть меню кооператива", L"Open the co-op menu")},
            {L"/disconnect, /quitcoop", TR(L"Выйти из сессии", L"Leave the session")},
            {L"/unstuck", TR(L"Снять блокировку управления", L"Clear a stuck control lock")},
            {L"/debug", TR(L"Показать или скрыть отладочную информацию", L"Toggle the debug overlay")},
            {L"/help, /?", TR(L"Открыть список команд", L"Open command help")},
        };
    } else {
        commands = {
            {TR(L"/kick <id или ник> [причина]", L"/kick <id or name> [reason]"), TR(L"Исключить игрока", L"Remove a player")},
            {TR(L"/say <текст>", L"/say <text>"), TR(L"Объявление сервера", L"Send a server announcement")},
        };
    }

    constexpr uint32_t text = 0xFFF4F1EE, muted = 0xFFBCB7B1, line = 0xFF55504A, accent = 0xFFFFBA55;
    const float split = left + (right - left) * 0.40f;
    const float rowPad = 14 * s;
    const float firstRowY = dividerY + 16 * s;
    struct Row {
        std::vector<std::wstring> syntax, description;
        float height;
    };
    std::vector<Row> rows;
    float bottom = firstRowY;
    for (const auto& command : commands) {
        auto syntax = g.Wrap(FONT_NORMAL, command.syntax, split - left - 2 * rowPad);
        auto description = g.Wrap(FONT_SMALL, command.description, right - split - 2 * rowPad);
        float height = std::max(syntax.size() * lh, description.size() * small) + 2 * rowPad;
        rows.push_back({std::move(syntax), std::move(description), height});
        bottom += height + 7 * s;
    }
    auto notes = g.Wrap(FONT_SMALL, note, right - left);
    if (!note.empty()) bottom += notes.size() * small + 5 * s;
    g.Rect(x, y, width, bottom + padding - y, 0xEF24211F);
    g.RectOutline(x, y, width, bottom + padding - y, 0xFF433E39);
    g.TextW(FONT_NORMAL, left, y + padding, TR(L"Справка по командам", L"Command help"), text, 0);
    if (closeHovered) g.Rect(closeRight - closeWidth, y + padding, closeWidth, lh, 0xFF3B3631);
    g.TextW(FONT_SMALL, closeRight - 5 * s, y + padding + (lh - small) * 0.5f, closeLabel, text, TEXT_RIGHT);
    g.Rect(left, dividerY, right - left, s, line);
    for (int i = 0; i < tabs; ++i) {
        float tabX = left + i * tabWidth;
        if (hover(tabX, tabY, tabWidth, tabHeight)) g.Rect(tabX, tabY, tabWidth, tabHeight, 0xFF34302C);
        g.TextW(FONT_SMALL, tabX + tabWidth * 0.5f, tabY + (tabHeight - small) * 0.5f, labels[i],
                m_helpTab == i ? text : muted, TEXT_CENTER);
        if (m_helpTab == i) g.Rect(tabX + 10 * s, dividerY - 3 * s, tabWidth - 20 * s, 3 * s, accent);
    }

    float rowY = firstRowY;
    for (const auto& row : rows) {
        g.Rect(left, rowY, right - left, row.height, 0xFF322E2B);
        g.Rect(split, rowY + rowPad, s, row.height - 2 * rowPad, line);
        float textY = rowY + (row.height - row.syntax.size() * lh) * 0.5f;
        for (const auto& part : row.syntax) {
            g.TextW(FONT_NORMAL, left + rowPad, textY, part, text, 0);
            textY += lh;
        }
        textY = rowY + (row.height - row.description.size() * small) * 0.5f;
        for (const auto& part : row.description) {
            g.TextW(FONT_SMALL, split + rowPad, textY, part, text, 0);
            textY += small;
        }
        rowY += row.height + 7 * s;
    }
    if (!note.empty()) {
        rowY += 5 * s;
        for (const auto& part : notes) {
            g.TextW(FONT_SMALL, left, rowY, part, muted, 0);
            rowY += small;
        }
    }
    w::DrawCursor();
}

static void DrawChatBackdrop(Renderer& g, float x, float y, float width, float height, uint32_t color) {
    const float radius = 8 * g.Scale();
    const float band = radius / 4;
    g.Rect(x, y + radius, width, height - 2 * radius, color);
    for (int i = 0; i < 4; ++i) {
        float dy = radius - (i + 0.5f) * band;
        float inset = radius - std::sqrt(radius * radius - dy * dy);
        g.Rect(x + inset, y + i * band, width - 2 * inset, band, color);
        g.Rect(x + inset, y + height - (i + 1) * band, width - 2 * inset, band, color);
    }
}

static uint32_t NameOutline(uint32_t color) {
    int red = (color >> 16) & 0xFF, green = (color >> 8) & 0xFF, blue = color & 0xFF;
    // Choose a contrasting edge while keeping the blip colour in the glyph itself.
    int brightness = (red * 299 + green * 587 + blue * 114) / 1000;
    return brightness < 120 ? TEXT_LIGHT_OUTLINE : TEXT_OUTLINE;
}

void Chat::Render() {
    auto& g = Gfx();
    if (!g.Ready()) return;
    float s = g.Scale();
    float x = 24 * s, y = 24 * s;
    float width = std::min(g.Width() - 2 * x, std::max(520.0f * s, g.Width() * 0.36f));
    float lh = std::max(g.LineHeight(FONT_CHAT_NAME), g.LineHeight(FONT_CHAT)) + 4 * s;
    int visibleLines = m_helpOpen ? 3 : kVisibleLines;
    uint64_t now = NowMs();
    const uint64_t fadeAfterMs = (uint64_t)Cfg().chatMessageSeconds * 1000;
    const float backgroundOpacity = Cfg().chatBackgroundOpacity / 100.0f;
    const uint32_t backdrop = WithAlpha(0xFF24211F, backgroundOpacity);
    const uint32_t textFlags = backgroundOpacity < 0.5f ? TEXT_SHADOW : 0;

    // collect wrapped lines from the end
    struct Vis {
        std::wstring text, name;
        uint32_t color, nameColor;
        float alpha, indent;
    };
    std::vector<Vis> vis;
    int skip = m_scroll;
    float panelWidth = 0, panelAlpha = 0;
    for (auto it = m_lines.rbegin(); it != m_lines.rend() && (int)vis.size() < visibleLines; ++it) {
        float alpha = 1.0f;
        if (!m_open) {
            uint64_t age = now - it->time;
            if (age > fadeAfterMs + kFadeDurationMs) break;
            if (age > fadeAfterMs) alpha = 1.0f - (float)(age - fadeAfterMs) / kFadeDurationMs;
        }
        std::wstring name = it->name;
        float indent = 0;
        if (!name.empty()) {
            const float maxNameWidth = width * 0.42f;
            if (g.TextWidthW(FONT_CHAT_NAME, name) > maxNameWidth) {
                const float ellipsisWidth = g.TextWidthW(FONT_CHAT_NAME, L"…");
                while (!name.empty() && g.TextWidthW(FONT_CHAT_NAME, name) + ellipsisWidth > maxNameWidth)
                    name.pop_back();
                name += L"…";
            }
            indent = g.TextWidthW(FONT_CHAT_NAME, name) + 14 * s;
        }
        auto wrapped = g.Wrap(FONT_CHAT, it->text, width - indent);
        for (auto w = wrapped.rbegin(); w != wrapped.rend() && (int)vis.size() < visibleLines; ++w) {
            if (skip > 0) {
                --skip;
                continue;
            }
            panelWidth = std::max(panelWidth, indent + g.TextWidthW(FONT_CHAT, *w));
            panelAlpha = std::max(panelAlpha, alpha);
            bool firstLine = w.base() == wrapped.begin() + 1;
            vis.push_back({*w, firstLine ? name : std::wstring{}, it->color, it->nameColor, alpha, indent});
        }
    }
    if (!vis.empty()) {
        DrawChatBackdrop(g, x - 10 * s, y - 6 * s, std::min(width, panelWidth) + 20 * s,
                         lh * vis.size() + 10 * s, WithAlpha(backdrop, panelAlpha));
        g.Clip(x - 3 * s, y - 3 * s, width + 6 * s, lh * vis.size() + 6 * s);
        float cy = y;
        for (auto v = vis.rbegin(); v != vis.rend(); ++v) {
            g.TextW(FONT_CHAT_NAME, x, cy, v->name, WithAlpha(v->nameColor, v->alpha), NameOutline(v->nameColor));
            g.TextW(FONT_CHAT, x + v->indent, cy, v->text, WithAlpha(v->color, v->alpha), textFlags);
            cy += lh;
        }
        g.ClipEnd();
    }
    if (m_helpOpen) {
        const float drawerWidth = std::min(g.Width() - 2 * x, std::max(720 * s, g.Width() * 0.37f));
        RenderHelp(x - 8 * s, y + 3 * lh + 30 * s, drawerWidth);
    } else if (m_open) {
        float iy = y + lh * vis.size() + (vis.empty() ? 0 : 12 * s);
        float ih = lh + 8 * s;
        DrawChatBackdrop(g, x - 10 * s, iy, width + 20 * s, ih, backdrop);
        const wchar_t* prefix = TR(L"Сказать: ", L"Say: ");
        std::wstring shown = prefix + m_input;
        float prefixW = g.TextWidthW(FONT_CHAT, prefix);
        // scroll the input horizontally if it is too long
        float caretX = prefixW + g.TextWidthW(FONT_CHAT, std::wstring_view(m_input).substr(0, m_cursor));
        float offset = std::max(0.0f, caretX - width + 10 * s);
        g.Clip(x, iy, width, ih);
        g.TextW(FONT_CHAT, x - offset, iy + 4 * s, shown, 0xFFF4F1EE, textFlags);
        if (((now - m_openTime) / 500) % 2 == 0) g.Rect(x - offset + caretX, iy + 5 * s, 2 * s, lh - 2 * s, 0xFFFFFFFF);
        g.ClipEnd();
        if (m_scroll > 0)
            g.Text(FONT_SMALL, x + width, iy + ih + 4 * s, Format("▲ %d", m_scroll), 0xFFB0B0B0, TEXT_RIGHT | TEXT_SHADOW);
    }
}

}  // namespace coop::ui
