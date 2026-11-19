#include "shared/ui/theme/Theme.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <iterator>
#include <string>
#include <vector>

#include "common/lib/ini/Ini.h"
#include "common/lib/util/Util.h"
#include "shared/config/Config.h"

namespace coop::ui {
namespace {

// A preset owns its colors and control treatment. Gameplay colors and chat settings are independent.
struct Preset {
    const char* id;
    const char* (*name)();
    UiTheme style;
};

constexpr Preset kPresets[] = {
    {"classic", [] { return TR("Классическая", "Classic"); }, {
        .style = UiStyle::Classic,
        .panel = 0xFF000000, .dialog = 0xF0000000, .field = 0xFF090C10, .preview = 0xFF0C1117,
        .hover = 0xFF10161D, .selection = 0xFF16202A, .selectedText = 0xFFACCBF1,
        .text = 0xFFE6E9EF, .strongText = 0xFFFFFFFF, .muted = 0xFF8D9CAD, .idle = 0xFF4A5A6B,
        .disabled = 0xFF353F4A, .accent = 0xFFACCBF1, .border = 0xFF26303B, .success = 0xFF5FA04E, .error = 0xFFC8282D,
        .titleFill = 0xFF000000, .titleText = 0xFFACCBF1, .bevelLight = 0xFF000000, .bevelDark = 0xFF000000,
    }},
    {"retro-pc", [] { return TR("Ретро ПК", "Retro PC"); }, {
        .style = UiStyle::Beveled,
        .panel = 0xFFE3E0D9, .dialog = 0xFFE3E0D9, .field = 0xFFF4F2ED, .preview = 0xFFCAC7BF,
        .hover = 0xFFD7E3F8, .selection = 0xFF2B52A5, .selectedText = 0xFFFFFFFF,
        .text = 0xFF182645, .strongText = 0xFF101D37, .muted = 0xFF48546B, .idle = 0xFF293A60,
        .disabled = 0xFF626775, .accent = 0xFF1B448D, .border = 0xFF787E8A, .success = 0xFF236538, .error = 0xFFA12538,
        .titleFill = 0xFF244594, .titleText = 0xFFFFFFFF, .bevelLight = 0xFFFFFFFF, .bevelDark = 0xFF696C76,
    }},
};

struct CustomTheme {
    std::string id, name;
    UiTheme style;
};

constexpr size_t kPresetCount = std::size(kPresets);
std::vector<CustomTheme> g_customThemes;
size_t g_selected = 0;

size_t ThemeIndex(const std::string& id) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (id == kPresets[i].id) return i;
    for (size_t i = 0; i < g_customThemes.size(); ++i)
        if (id == g_customThemes[i].id) return kPresetCount + i;
    return 0;
}

uint32_t ReadColor(std::string text, uint32_t fallback) {
    if (!text.empty() && text.front() == '#') text.erase(0, 1);
    if (text.size() != 6 && text.size() != 8) return fallback;
    uint32_t value = 0;
    auto end = text.data() + text.size();
    auto parsed = std::from_chars(text.data(), end, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return fallback;
    return text.size() == 6 ? value | 0xFF000000u : value;
}

UiTheme ReadStyle(const Ini& ini) {
    const std::string base = ToLower(ini.Get("theme", "base"));
    UiTheme style = kPresets[0].style;
    for (const auto& preset : kPresets)
        if (base == preset.id) style = preset.style;
    // Legacy themes can still opt in or out of the old bevel treatment.
    if (!ini.Get("theme", "beveled").empty())
        style.style = ini.GetBool("theme", "beveled") ? UiStyle::Beveled : UiStyle::Classic;
    const std::string treatment = ToLower(ini.Get("theme", "style"));
    constexpr struct { const char* name; UiStyle style; } treatments[] = {
        {"classic", UiStyle::Classic}, {"beveled", UiStyle::Beveled}, {"glass", UiStyle::Glass},
        {"atlas", UiStyle::Atlas}, {"arcade", UiStyle::Arcade}, {"terminal", UiStyle::Terminal},
    };
    for (const auto& choice : treatments)
        if (treatment == choice.name) style.style = choice.style;

    constexpr struct { const char* key; uint32_t UiTheme::*value; } colors[] = {
        {"panel", &UiTheme::panel}, {"dialog", &UiTheme::dialog}, {"field", &UiTheme::field},
        {"preview", &UiTheme::preview}, {"hover", &UiTheme::hover}, {"selection", &UiTheme::selection},
        {"selected_text", &UiTheme::selectedText}, {"text", &UiTheme::text}, {"strong_text", &UiTheme::strongText},
        {"muted", &UiTheme::muted}, {"idle", &UiTheme::idle}, {"disabled", &UiTheme::disabled}, {"accent", &UiTheme::accent},
        {"border", &UiTheme::border}, {"success", &UiTheme::success}, {"error", &UiTheme::error},
        {"title_fill", &UiTheme::titleFill}, {"title_text", &UiTheme::titleText},
        {"bevel_light", &UiTheme::bevelLight}, {"bevel_dark", &UiTheme::bevelDark},
    };
    for (const auto& color : colors)
        style.*color.value = ReadColor(ini.Get("colors", color.key), style.*color.value);
    return style;
}

}  // namespace

const UiTheme& Theme() {
    return g_selected < kPresetCount ? kPresets[g_selected].style : g_customThemes[g_selected - kPresetCount].style;
}

const char* ThemeName() {
    return g_selected < kPresetCount ? kPresets[g_selected].name() : g_customThemes[g_selected - kPresetCount].name.c_str();
}

void LoadThemes() {
    g_customThemes.clear();
    if (Cfg().coopDir.empty()) {
        g_selected = ThemeIndex(Cfg().uiTheme);
        return;
    }
    const std::wstring directory = Cfg().coopDir + L"themes\\";
    WIN32_FIND_DATAW file{};
    HANDLE files = FindFirstFileW((directory + L"*.ini").c_str(), &file);
    if (files != INVALID_HANDLE_VALUE) {
        do {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || file.nFileSizeHigh || file.nFileSizeLow > 64 * 1024) continue;
            std::wstring filename = file.cFileName;
            std::string lower = ToLower(WideToUtf8(filename));
            if (lower.size() <= 4 || !lower.ends_with(".ini")) continue;
            std::string id = lower.substr(0, lower.size() - 4);
            if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos ||
                std::any_of(std::begin(kPresets), std::end(kPresets), [&](const Preset& preset) { return id == preset.id; })) continue;
            Ini ini;
            if (!ini.Load(directory + filename)) continue;
            std::string name = ini.Get("theme", "name", id);
            g_customThemes.push_back({id, name.empty() ? id : name, ReadStyle(ini)});
        } while (FindNextFileW(files, &file));
        FindClose(files);
    }
    std::sort(g_customThemes.begin(), g_customThemes.end(), [](const CustomTheme& a, const CustomTheme& b) { return a.id < b.id; });
    g_selected = ThemeIndex(Cfg().uiTheme);
}

void CycleTheme(int direction) {
    size_t count = kPresetCount + g_customThemes.size();
    g_selected = (g_selected + (direction < 0 ? count - 1 : 1)) % count;
    Cfg().uiTheme = g_selected < kPresetCount ? kPresets[g_selected].id : g_customThemes[g_selected - kPresetCount].id;
}

}  // namespace coop::ui
