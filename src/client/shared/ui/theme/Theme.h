#pragma once

#include <cstdint>

namespace coop::ui {

enum class UiStyle { Classic, Beveled, Glass, Atlas, Arcade, Terminal };

struct UiTheme {
    UiStyle style;
    uint32_t panel, dialog, field, preview, hover, selection, selectedText;
    uint32_t text, strongText, muted, idle, disabled, accent, border, success, error;
    uint32_t titleFill, titleText, bevelLight, bevelDark;
};

const UiTheme& Theme();
const char* ThemeName();
void LoadThemes();
void CycleTheme(int direction);

}  // namespace coop::ui
