#pragma once
#include <cstdint>
#include <vector>

#include "shared/game/Game.h"

namespace coop::ui {

// Shared input for the co-op UI (keyboard, mouse, and GInput controller state).
// The game reads the keyboard only through window messages (WM_KEYDOWN/UP) and the mouse through DirectInput, so:
//  * keyboard capture = swallowing the messages;
//  * mouse capture    = skipping CPad::UpdateMouse (hooked in Patches) and not re-centring the cursor.
struct InputState {
    // per-frame event queues (filled by the window procedure, consumed by the UI each frame)
    std::vector<int> keyDowns;   // virtual keys (incl. auto-repeat)
    std::vector<wchar_t> chars;  // typed characters
    float mouseX = 0, mouseY = 0;  // in back-buffer pixels
    bool lmb = false;
    bool lmbPressed = false;   // went down this frame
    bool lmbReleased = false;
    int wheel = 0;
    bool ctrl = false, shift = false;
};

class Input {
public:
    void Install(void* hwnd);
    bool Installed() const { return m_hwnd != nullptr; }

    // While set, keyboard messages never reach the game.
    void SetKeyboardCapture(bool on);
    // While set, the game does not receive mouse input and the cursor is free.
    void SetMouseCapture(bool on);
    bool KeyboardCaptured() const { return m_kbCapture; }
    bool MouseCaptured() const { return m_mouseCapture; }
    bool ControllerCursorActive() const { return m_mouseCapture && m_padCursorActive; }
    // Captured while the game holds the mouse exclusively (its fullscreen play): our cursor moves by the game's own
    // reading of the mouse, fed here from CPad::UpdateMouse (Patches)
    bool CursorFromGameMouse() const;
    void FeedGameMouse(float dx, float dy, bool lmb, int wheel);
    // Save the controller state before the game input hook clears it while a UI window is open.
    void FeedControllerState(const game::CControllerState& state) { m_controllerState = state; }
    // Controller-only shortcuts, called once per game pad update before gameplay input is suppressed.
    void ProcessControllerHotkeys(const game::CControllerState& state);

    InputState& State() { return m_state; }
    void EndFrame();  // clears per-frame events
    void UpdateMouse(float backBufferW, float backBufferH);

    void* Hwnd() const { return m_hwnd; }
    // Called by the window procedure. Returns true if the message must not reach the game.
    bool OnMessage(unsigned msg, uintptr_t wParam, intptr_t lParam);

    // Clears the game's keyboard state so no key stays "held" after capture starts.
    static void ClearGameKeys();

private:
    void* m_hwnd = nullptr;
    bool m_kbCapture = false;
    bool m_mouseCapture = false;
    bool m_fromGame = false;          // the cursor below is ours (CursorFromGameMouse)
    float m_gameX = 0, m_gameY = 0;  // in client pixels
    bool m_gameLmb = false;
    // GInput feeds the same CPad state used by the game. While a co-op window owns
    // the mouse, the left stick / D-pad drive a virtual cursor and Cross clicks it.
    game::CControllerState m_controllerState{};
    game::CControllerState m_previousPad{};
    game::CControllerState m_previousHotkeyPad{};
    bool m_padPrimed = false;
    bool m_padCursorActive = false;
    bool m_padCrossBlocked = false;
    bool m_padBackBlocked = false;
    float m_virtualX = 0, m_virtualY = 0;
    int32_t m_lastPhysicalX = 0, m_lastPhysicalY = 0;
    uint64_t m_padFrameMs = 0;
    InputState m_state;
};

Input& In();

}  // namespace coop::ui
