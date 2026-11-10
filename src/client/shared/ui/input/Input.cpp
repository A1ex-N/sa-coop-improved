#include "shared/ui/input/Input.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "shared/game/Game.h"
#include "shared/config/Config.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop::ui {

Input& In() {
    static Input i;
    return i;
}

static WNDPROC g_origProc = nullptr;

// The window has its focus back (the game's MainWndProc on WM_SETFOCUS, 0x748063): the game asks for its pause menu in the
// next frame (0x53BC60). In a co-op session that would stop the shared world for everyone, so only that request is dropped.
// WM_SETFOCUS itself goes on to the game and its DefWindowProc, where GInput learns the window is active again: kept from
// it, the pad stayed off after the first Alt+Tab in a session (#74, GInput's DisableOnFocusLost).
using VoidFn = void(__cdecl*)();
static VoidFn g_origFocusMenu = nullptr;
static bool g_focusMenuHooked = false;
static void __cdecl HookFocusMenu() {
    if (core::IsInCoopSession() && game::GameState() == game::GAME_STATE_IDLE) return;
    g_origFocusMenu();
}

static LRESULT CALLBACK CoopWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (In().OnMessage(msg, wp, lp)) return 0;
    return CallWindowProcW(g_origProc, hwnd, msg, wp, lp);
}

void Input::Install(void* hwnd) {
    if (m_hwnd || !hwnd) return;
    m_hwnd = hwnd;
    g_origProc = (WNDPROC)SetWindowLongPtrW((HWND)hwnd, GWLP_WNDPROC, (LONG_PTR)&CoopWndProc);
    LOGI("Window procedure subclassed (hwnd %p)", hwnd);
    g_focusMenuHooked = mem::RedirectCallFn(0x748063, &HookFocusMenu, &g_origFocusMenu);
}

void Input::ClearGameKeys() {
    // the new, old and the "temp" state the window messages write and the pads copy every frame: a key held when the
    // chat opened would stay down there for good
    game::Call<0x53F1E0>();  // CPad::ClearKeyBoardHistory
    // also release the pad buttons of the local player
    game::CPad* pad = game::GetPad(0);
    memset(&pad->NewState, 0, sizeof(pad->NewState));
    memset(&pad->OldState, 0, sizeof(pad->OldState));
}

void Input::ProcessControllerHotkeys(const game::CControllerState& state) {
    // All GUI shortcuts use Select as a modifier. Start is deliberately NOT
    // used: it belongs to GTA's pause menu. While Select is held, HookUpdatePads
    // consumes the complete pad state, so neither the modifier nor the chord's
    // second button can trigger a normal gameplay action.
    // Select + D-pad Up = main SA:COOP window; Cross = skins;
    // Square = animations; Triangle = players.
    bool selectDpadUp = state.Select && state.DPadUp &&
                        !(m_previousHotkeyPad.Select && m_previousHotkeyPad.DPadUp);
    bool selectCross = state.Select && state.ButtonCross &&
                       !(m_previousHotkeyPad.Select && m_previousHotkeyPad.ButtonCross);
    bool selectSquare = state.Select && state.ButtonSquare &&
                        !(m_previousHotkeyPad.Select && m_previousHotkeyPad.ButtonSquare);
    bool selectTriangle = state.Select && state.ButtonTriangle &&
                          !(m_previousHotkeyPad.Select && m_previousHotkeyPad.ButtonTriangle);

    if (!m_kbCapture && !m_mouseCapture) {
        if (selectDpadUp) core::OnKeyDown(VK_F9, false);
        else if (selectCross) core::OnKeyDown(VK_F6, false);
        else if (selectSquare) core::OnKeyDown('U', false);
        else if (selectTriangle) core::OnKeyDown(Cfg().playerListKey, false);
    }
    m_previousHotkeyPad = state;
}

void Input::SetKeyboardCapture(bool on) {
    if (on && !m_kbCapture) ClearGameKeys();
    m_kbCapture = on;
}

// In exclusive fullscreen the game holds the mouse in DirectInput's exclusive mode (its play: diMouseInit(fullscreen and no
// menu)): Windows' cursor stands still, and ours (GetCursorPos) froze on the F9 window with the invite code (issue #66).
// Ours then moves by the game's own reading of the mouse. Taking the game's device into the shared mode while ours was out
// and back left it unacquired: the camera stood still after the window closed, and after joining, till an alt-tab.
static bool GameMouseExclusive() {
    auto* ps = game::Ref<uint8_t*>(0xC17054);  // RsGlobal.ps
    return ps && game::Field<int32_t>(ps, 0x08) && !game::Ref<uint8_t>(0xBA67A4);  // fullScreen, no menu of the game's
}

bool Input::CursorFromGameMouse() const { return m_mouseCapture && GameMouseExclusive(); }

void Input::FeedGameMouse(float dx, float dy, bool lmb, int wheel) {
    m_gameX += dx * 1.25f, m_gameY += dy * 1.25f;
    m_gameLmb = lmb;
    m_state.wheel += wheel;  // the window gets no wheel messages either
}

void Input::SetMouseCapture(bool on) {
    if (on == m_mouseCapture) return;
    m_mouseCapture = on;
    if (on) {
        // put the cursor in the middle so the user sees it
        RECT rc;
        if (m_hwnd && GetClientRect((HWND)m_hwnd, &rc)) {
            POINT p{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
            m_virtualX = (float)p.x;
            m_virtualY = (float)p.y;
            m_lastPhysicalX = p.x;
            m_lastPhysicalY = p.y;
            ClientToScreen((HWND)m_hwnd, &p);
            SetCursorPos(p.x, p.y);
        }
        m_padCursorActive = false;
        m_padFrameMs = NowMs();
        m_previousPad = m_controllerState;  // don't reuse the button that opened the window
        m_padCrossBlocked = true;  // wait for a release so the menu-opening press cannot activate a control
        m_padBackBlocked = true;
        m_padPrimed = true;
    } else {
        m_padCursorActive = false;
        m_padPrimed = false;
    }
}

void Input::EndFrame() {
    m_state.keyDowns.clear();
    m_state.chars.clear();
    m_state.lmbPressed = false;
    m_state.lmbReleased = false;
    m_state.wheel = 0;
}

void Input::UpdateMouse(float bbW, float bbH) {
    if (!m_hwnd) return;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient((HWND)m_hwnd, &p);
    RECT rc;
    GetClientRect((HWND)m_hwnd, &rc);
    float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    if (cw <= 0 || ch <= 0) return;

    bool fromGame = CursorFromGameMouse();
    if (fromGame && !m_fromGame) m_gameX = (float)p.x, m_gameY = (float)p.y, m_gameLmb = false;
    m_fromGame = fromGame;

    bool padCross = false;
    if (m_mouseCapture) {
        const game::CControllerState& pad = m_controllerState;
        if (!m_padPrimed) m_previousPad = pad, m_padPrimed = true;
        uint64_t now = NowMs();
        float dt = std::clamp((float)(now - m_padFrameMs) / 1000.0f, 0.0f, 0.05f);
        m_padFrameMs = now;
        float ax = (float)pad.LeftStickX / 128.0f;
        float ay = (float)pad.LeftStickY / 128.0f;
        if (std::abs(ax) < 0.22f) ax = 0.0f;
        if (std::abs(ay) < 0.22f) ay = 0.0f;
        if (pad.DPadLeft) ax = -1.0f;
        if (pad.DPadRight) ax = 1.0f;
        if (pad.DPadUp) ay = -1.0f;
        if (pad.DPadDown) ay = 1.0f;
        float magnitude = std::sqrt(ax * ax + ay * ay);
        if (magnitude > 1.0f) ax /= magnitude, ay /= magnitude;

        if (!pad.ButtonCross) m_padCrossBlocked = false;
        if (!pad.ButtonCircle) m_padBackBlocked = false;
        bool padActivity = ax != 0.0f || ay != 0.0f || (pad.ButtonCross && !m_padCrossBlocked) || pad.ButtonCircle ||
                           (pad.LeftShoulder2 && !m_previousPad.LeftShoulder2) ||
                           (pad.RightShoulder2 && !m_previousPad.RightShoulder2);
        if (padActivity && !m_padCursorActive) {
            m_virtualX = (float)p.x;
            m_virtualY = (float)p.y;
            m_padCursorActive = true;
        }

        // Real mouse movement switches back to the physical cursor. Controller input
        // switches to the virtual cursor again, so either device can be used at any time.
        if (m_padCursorActive && (std::abs(p.x - m_lastPhysicalX) > 3 || std::abs(p.y - m_lastPhysicalY) > 3))
            m_padCursorActive = false;
        m_lastPhysicalX = p.x;
        m_lastPhysicalY = p.y;

        if (m_padCursorActive) {
            m_virtualX += ax * 900.0f * dt;
            m_virtualY += ay * 900.0f * dt;
            m_virtualX = std::clamp(m_virtualX, 0.0f, cw - 1.0f);
            m_virtualY = std::clamp(m_virtualY, 0.0f, ch - 1.0f);
            p.x = (LONG)m_virtualX;
            p.y = (LONG)m_virtualY;
        }

        padCross = pad.ButtonCross != 0 && !m_padCrossBlocked;
        if (pad.ButtonCircle && !m_previousPad.ButtonCircle && !m_padBackBlocked)
            core::OnKeyDown(VK_ESCAPE, false);  // the same Back action as Escape in every co-op window
        if (pad.LeftShoulder2 && !m_previousPad.LeftShoulder2) m_state.wheel += 1;
        if (pad.RightShoulder2 && !m_previousPad.RightShoulder2) m_state.wheel -= 1;
        m_previousPad = pad;
    }

    if (fromGame && !m_padCursorActive) {
        m_gameX = std::clamp(m_gameX, 0.0f, cw - 1.0f);
        m_gameY = std::clamp(m_gameY, 0.0f, ch - 1.0f);
        p.x = (LONG)m_gameX;
        p.y = (LONG)m_gameY;
    }
    m_state.mouseX = (float)p.x * bbW / cw;
    m_state.mouseY = (float)p.y * bbH / ch;
    m_state.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    m_state.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool physicalDown = fromGame ? m_gameLmb : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && game::IsForeground();
    bool down = physicalDown || padCross;
    if (down && !m_state.lmb) m_state.lmbPressed = true;
    if (!down && m_state.lmb) m_state.lmbReleased = true;
    m_state.lmb = down;
}

bool Input::OnMessage(unsigned msg, uintptr_t wParam, intptr_t lParam) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int vk = (int)wParam;
            m_state.keyDowns.push_back(vk);
            // Global hotkeys are handled by the core; it tells us whether to swallow the key.
            bool consumed = core::OnKeyDown(vk, (lParam & (1 << 30)) != 0);
            if (consumed) return true;
            if (m_kbCapture) {
                // keep Alt+F4 / Alt+Tab working
                if (msg == WM_SYSKEYDOWN && (vk == VK_F4 || vk == VK_TAB)) return false;
                return true;
            }
            return false;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            return false;  // releases reach the game even while we type: nothing it does starts on a release
        case WM_CHAR:
            if (m_kbCapture) {
                wchar_t c = (wchar_t)wParam;
                if (c >= 0x20 && c != 0x7F) m_state.chars.push_back(c);
                return true;
            }
            return false;
        case WM_MOUSEWHEEL:
            m_state.wheel += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            return m_mouseCapture;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return m_mouseCapture;
        case WM_SETFOCUS:
            // Vanilla opens the pause menu when the window regains focus; in co-op that would pause the shared world
            // for everyone. Its request is dropped where the game makes it (HookFocusMenu); if another mod took that
            // call, the message is kept from the game as before and only the foreground flag comes back.
            if (!g_focusMenuHooked && core::IsInCoopSession() && game::GameState() == game::GAME_STATE_IDLE) {
                game::Ref<uint8_t>(game::addr::ForegroundApp) = 1;
                return true;
            }
            return false;
        case WM_SETCURSOR:
            if (m_mouseCapture && LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);  // we draw our own cursor
                return true;
            }
            return false;
        default:
            return false;
    }
}

}  // namespace coop::ui
