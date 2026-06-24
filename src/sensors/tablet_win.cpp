#include "tablet_platform.h"
#include "platform_utils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>
#include <cstdio>

#ifndef GET_POINTERID_WPARAM
#define GET_POINTERID_WPARAM(wParam) (LOWORD(wParam))
#endif

#ifndef WM_POINTERUPDATE
#define WM_POINTERUPDATE    0x0245
#define WM_POINTERDOWN      0x0246
#define WM_POINTERUP        0x0247
#endif

static TabletState g_penState = {0};
static CRITICAL_SECTION g_penLock;
static int g_hookCount = 0;

#define MOUSE_RING_SIZE 2048
static float g_mouseX[MOUSE_RING_SIZE];
static float g_mouseY[MOUSE_RING_SIZE];
static volatile int g_mouseHead = 0;
static volatile int g_mouseTail = 0;

typedef BOOL (WINAPI *GetPointerPenInfoFn)(UINT32 pointerId, POINTER_PEN_INFO* penInfo);
static GetPointerPenInfoFn g_GetPointerPenInfo = NULL;

static void LoadPointerAPI(void) {
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32)
        g_GetPointerPenInfo = (GetPointerPenInfoFn)GetProcAddress(hUser32, "GetPointerPenInfo");
}

static LRESULT CALLBACK TabletMsgHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        MSG* msg = (MSG*)lParam;
        if (msg->message >= WM_POINTERUPDATE && msg->message <= WM_POINTERUP) {
            g_hookCount++;
            UINT32 pointerId = GET_POINTERID_WPARAM(msg->wParam);

            if (g_GetPointerPenInfo) {
                POINTER_PEN_INFO penInfo;
                if (g_GetPointerPenInfo(pointerId, &penInfo)) {
                    EnterCriticalSection(&g_penLock);
                    g_penState.pressure = penInfo.pressure / 1024.0f;
                    if (g_penState.pressure > 1.0f) g_penState.pressure = 1.0f;
                    if (g_penState.pressure < 0.0f) g_penState.pressure = 0.0f;
                    g_penState.tiltX = penInfo.tiltX / 90.0f;
                    if (g_penState.tiltX < -1.0f) g_penState.tiltX = -1.0f;
                    if (g_penState.tiltX > 1.0f) g_penState.tiltX = 1.0f;
                    g_penState.tiltY = penInfo.tiltY / 90.0f;
                    if (g_penState.tiltY < -1.0f) g_penState.tiltY = -1.0f;
                    if (g_penState.tiltY > 1.0f) g_penState.tiltY = 1.0f;
                    g_penState.rotation = penInfo.rotation / 360.0f;
                    g_penState.active = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE) != 0;
                    g_penState.touching = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
                    g_penState.buttons = 0;
                    if (g_penState.touching) g_penState.buttons |= 1;
                    if (penInfo.penFlags & PEN_FLAG_BARREL)   g_penState.buttons |= 2;
                    if (penInfo.penFlags & PEN_FLAG_INVERTED) g_penState.buttons |= 4;
                    LeaveCriticalSection(&g_penLock);
                }
            }
        }
        // Capture every real mouse position while button is held
        if (msg->message == WM_MOUSEMOVE && (msg->wParam & MK_LBUTTON)) {
            int next = (g_mouseTail + 1) % MOUSE_RING_SIZE;
            if (next != g_mouseHead) {
                g_mouseX[g_mouseTail] = (float)(short)LOWORD(msg->lParam);
                g_mouseY[g_mouseTail] = (float)(short)HIWORD(msg->lParam);
                g_mouseTail = next;
            }
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

static HHOOK g_msgHook = NULL;

bool TabletPlatform_Init(void* nativeWindow) {
    LoadPointerAPI();
    if (!g_GetPointerPenInfo)
        return false;

    HWND hwnd = (HWND)nativeWindow;
    if (!hwnd) return false;

    InitializeCriticalSection(&g_penLock);
    memset(&g_penState, 0, sizeof(g_penState));
    g_penState.pressure = 1.0f;
    g_penState.rotation = 0.5f;

    DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
    g_msgHook = SetWindowsHookEx(WH_GETMESSAGE, TabletMsgHook, NULL, threadId);
    if (!g_msgHook) {
        DeleteCriticalSection(&g_penLock);
        return false;
    }
    return true;
}

void TabletPlatform_Shutdown(void) {
    if (g_msgHook) UnhookWindowsHookEx(g_msgHook);
    g_msgHook = NULL;
    DeleteCriticalSection(&g_penLock);
}

bool TabletPlatform_Poll(TabletState* state) {
    EnterCriticalSection(&g_penLock);
    *state = g_penState;
    LeaveCriticalSection(&g_penLock);
    return state->active;
}

int TabletPlatform_GetHookCount(void)   { return g_hookCount; }

int TabletPlatform_DrainMousePos(float* buf, int maxOut) {
    int count = 0;
    while (g_mouseHead != g_mouseTail && count + 1 < maxOut) {
        buf[count * 2]     = g_mouseX[g_mouseHead];
        buf[count * 2 + 1] = g_mouseY[g_mouseHead];
        g_mouseHead = (g_mouseHead + 1) % MOUSE_RING_SIZE;
        count++;
    }
    if (g_mouseHead == g_mouseTail)
        g_mouseHead = g_mouseTail = 0;
    return count;
}

void TabletPlatform_ClearMousePos(void) {
    g_mouseHead = g_mouseTail = 0;
}

void TabletPlatform_GetDebugInfo(char* buf, size_t sz) {
    UINT digitizer = GetSystemMetrics(SM_DIGITIZER);
    const char* flavor;
    if (digitizer == 0)
        flavor = "NONE";
    else if (digitizer == 0x01)
        flavor = "INTEGRATED_TOUCH";
    else if (digitizer == 0x02)
        flavor = "INTEGRATED_PEN";
    else if (digitizer == 0x40)
        flavor = "EXTERNAL_PEN";
    else if (digitizer == 0x42)
        flavor = "INTEGRATED_PEN | EXTERNAL_PEN";
    else
        flavor = "MULTI";

    HMODULE hMod = GetModuleHandleA("user32.dll");
    bool hasPtr = hMod && GetProcAddress(hMod, "GetPointerPenInfo");

    HWND hwnd = (HWND)Platform_GetNativeWindowHandle();
    DWORD tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;

    snprintf(buf, sz,
        "SM_DIGITIZER:     0x%02X (%s)\n"
        "GetPointerPenInfo: %s\n"
        "Window handle:     0x%p (thread %lu)\n"
        "Hook installed:    %s\n"
        "WM_POINTER msgs:   %i\n"
        "Hint: enable 'Windows Ink' in Wacom settings",
        digitizer, flavor,
        hasPtr ? "AVAILABLE" : "MISSING",
        (void*)hwnd, tid,
        (hwnd && tid) ? "YES" : "NO",
        g_hookCount);
}
