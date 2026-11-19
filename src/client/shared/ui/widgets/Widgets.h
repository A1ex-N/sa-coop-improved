#pragma once
// Shared themed controls for the co-op menus, with mouse, keyboard, and controller cursor support.
#include <cstdint>
#include <string>

#include "shared/ui/theme/Theme.h"

namespace coop::ui::w {

void BeginFrame();  // call once per frame before widgets
void EndFrame();

// Returns the y where the content starts (below the title).
float Panel(float x, float y, float w, float h, const char* asciiTitle);
void MenuTitle(float x, float y, const std::string& title);
void MenuSurface(float x, float y, float w, float h, uint32_t fill);
void MenuRow(float x, float y, float w, float h, bool selected, bool hover);
// A menu item (text, no box). lit = drawn as the selected item, big = the large menu font. Returns true when clicked.
bool Button(const char* id, float x, float y, float w, float h, const std::string& label, bool enabled = true,
            bool lit = false, bool big = false);
bool Tab(const char* id, float x, float y, float w, float h, const std::string& label, bool active, bool small = false);
// Single-line text box. Returns true when Enter was pressed inside it.
bool TextBox(const char* id, float x, float y, float w, float h, std::wstring& value, size_t maxLen,
             bool password = false, bool digitsOnly = false, const char* placeholder = nullptr);
// "< text >" selector. Returns -1/+1 when an arrow was clicked, 0 otherwise.
int Selector(const char* id, float x, float y, float w, float h, const std::string& text);
bool Checkbox(const char* id, float x, float y, const std::string& label, bool& value);

void SetFocus(const char* id);
bool AnyTextFocused();
void DrawCursor();
std::wstring ClipboardText();

}  // namespace coop::ui::w
