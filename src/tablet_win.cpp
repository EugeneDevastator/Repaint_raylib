#include "tablet_platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>

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
                    LeaveCriticalSection(&g_penLock);
                }
            }
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

static HHOOK g_msgHook = NULL;

bool TabletPlatform_Init(void* nativeWindow) {
    InitializeCriticalSection(&g_penLock);
    memset(&g_penState, 0, sizeof(g_penState));
    g_penState.pressure = 1.0f;
    g_penState.rotation = 0.5f;

    LoadPointerAPI();
    if (!g_GetPointerPenInfo)
        return false;
    if (!(GetSystemMetrics(SM_DIGITIZER) & 0x01))
        return false;

    HWND hwnd = (HWND)nativeWindow;
    if (!hwnd) return false;

    DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
    g_msgHook = SetWindowsHookEx(WH_GETMESSAGE, TabletMsgHook, NULL, threadId);
    return (g_msgHook != NULL);
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
int TabletPlatform_GetPenSuccess(void)  { return 0; }
int TabletPlatform_GetTypeMismatch(void){ return 0; }
int TabletPlatform_GetLastType(void)    { return 0; }
