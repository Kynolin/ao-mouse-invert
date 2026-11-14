// ao-mouse-invert: a dinput8.dll proxy that inverts camera Y-axis mouse input.
// See README.md for installation, configuration and build instructions.
//
// How it works:
//   - Windows loads a dinput8.dll from the game's folder before the system one.
//     This DLL forwards every DirectInput call to the real System32 dinput8.dll.
//   - On load it hooks three user32 raw-input functions (via MinHook).
//   - The client enables raw mouse input only while the camera is being
//     dragged, so negating lLastY in raw mouse packets affects only the
//     camera, never UI cursor movement.
//   - Inversion is gated on which mouse button is held (dinput8_invert.ini).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "MinHook.h"

static HMODULE realDll;
static FILE *g_log;
static CRITICAL_SECTION lock;

// Settings (dinput8_invert.ini, [invert] section)
static int invertWhenRight = 1; // buttons=right|left|both|any
static int invertWhenLeft = 0;
static int invertAlways = 0;

static void logmsg(const char *fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&lock);
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "[%6lu] ", GetTickCount() % 1000000);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&lock);
}

static BOOL held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static BOOL should_invert(void) {
    return invertAlways ||
           (invertWhenRight && held(VK_RBUTTON)) ||
           (invertWhenLeft && held(VK_LBUTTON));
}

static FARPROC real(const char *name) {
    if (!realDll) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        lstrcpyW(path + n, L"\\dinput8.dll");
        realDll = LoadLibraryW(path);
        if (!realDll) logmsg("FAILED to load real dinput8.dll");
    }
    return realDll ? GetProcAddress(realDll, name) : NULL;
}

// ---- raw input hooks ----

static UINT (WINAPI *oGetRawInputData)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static UINT (WINAPI *oGetRawInputBuffer)(PRAWINPUT, PUINT, UINT);
static BOOL (WINAPI *oRegisterRawInputDevices)(PCRAWINPUTDEVICE, UINT, UINT);

static void flip_raw(RAWINPUT *ri) {
    if (ri->header.dwType != RIM_TYPEMOUSE) return;
    RAWMOUSE *m = &ri->data.mouse;
    if (!(m->usFlags & MOUSE_MOVE_ABSOLUTE) && m->lLastY && should_invert()) m->lLastY = -m->lLastY;
}

static UINT WINAPI hkGetRawInputData(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT hdr) {
    UINT r = oGetRawInputData(h, cmd, data, size, hdr);
    if (cmd == RID_INPUT && data && r != (UINT)-1 && r >= sizeof(RAWINPUTHEADER)) flip_raw((RAWINPUT *)data);
    return r;
}

// Next packet in a GetRawInputBuffer batch: packets are pointer-size aligned.
// Same as the SDK's NEXTRAWINPUTBLOCK, which needs a QWORD type that neither
// MSVC nor MinGW defines in C.
static PRAWINPUT next_raw(PRAWINPUT p) {
    ULONG_PTR a = (ULONG_PTR)p + p->header.dwSize;
    return (PRAWINPUT)((a + sizeof(ULONG_PTR) - 1) & ~(ULONG_PTR)(sizeof(ULONG_PTR) - 1));
}

static UINT WINAPI hkGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT hdr) {
    UINT r = oGetRawInputBuffer(data, size, hdr);
    if (data && r != (UINT)-1) {
        PRAWINPUT p = data;
        for (UINT i = 0; i < r; i++) { flip_raw(p); p = next_raw(p); }
    }
    return r;
}

// Logged so you can see which button press puts the client into camera mode.
static BOOL WINAPI hkRegisterRawInputDevices(PCRAWINPUTDEVICE d, UINT n, UINT cb) {
    BOOL ok = oRegisterRawInputDevices(d, n, cb);
    for (UINT i = 0; d && i < n; i++)
        if (d[i].usUsagePage == 0x1 && d[i].usUsage == 0x2)
            logmsg("camera raw mouse %s  (L=%d R=%d)",
                   (d[i].dwFlags & RIDEV_REMOVE) ? "OFF" : "ON ",
                   held(VK_LBUTTON), held(VK_RBUTTON));
    return ok;
}

// ---- install ----

static volatile LONG hooksDone;

static void install_hooks(void) {
    if (InterlockedExchange(&hooksDone, 1)) return;
    if (MH_Initialize() != MH_OK) { logmsg("MH_Initialize failed"); return; }
#define H(name) do { MH_STATUS e_ = MH_CreateHookApi(L"user32", #name, (LPVOID)hk##name, (LPVOID *)&o##name); \
    if (e_ != MH_OK) logmsg("hook " #name " failed: %d", e_); } while (0)
    H(GetRawInputData);
    H(GetRawInputBuffer);
    H(RegisterRawInputDevices);
#undef H
    logmsg("hooks %s", MH_EnableHook(MH_ALL_HOOKS) == MH_OK ? "enabled" : "FAILED");
}

static DWORD WINAPI init_thread(LPVOID p) { (void)p; install_hooks(); return 0; }

static void load_settings(const char *dir) {
    char ini[MAX_PATH], v[32];
    snprintf(ini, sizeof ini, "%sdinput8_invert.ini", dir);
    GetPrivateProfileStringA("invert", "buttons", "right", v, sizeof v, ini);
    invertWhenRight = !lstrcmpiA(v, "right") || !lstrcmpiA(v, "both");
    invertWhenLeft  = !lstrcmpiA(v, "left")  || !lstrcmpiA(v, "both");
    invertAlways    = !lstrcmpiA(v, "any");
    if (!invertWhenRight && !invertWhenLeft && !invertAlways) {
        logmsg("unknown buttons=%s, using right", v);
        invertWhenRight = 1;
        lstrcpyA(v, "right");
    }
    logmsg("buttons=%s", v);
}

// ---- dinput8 exports (pass-through) ----

typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, void *);

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID *out, void * outer) {
    install_hooks();
    DI8Create_t fn = (DI8Create_t)real("DirectInput8Create");
    return fn ? fn(inst, ver, riid, out, outer) : E_FAIL;
}
HRESULT WINAPI DllCanUnloadNow(void) {
    FARPROC f = real("DllCanUnloadNow");
    return f ? ((HRESULT (WINAPI *)(void))f)() : S_FALSE;
}
HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID r, LPVOID *o) {
    FARPROC f = real("DllGetClassObject");
    return f ? ((HRESULT (WINAPI *)(REFCLSID, REFIID, LPVOID *))f)(c, r, o) : CLASS_E_CLASSNOTAVAILABLE;
}
HRESULT WINAPI DllRegisterServer(void) {
    FARPROC f = real("DllRegisterServer");
    return f ? ((HRESULT (WINAPI *)(void))f)() : E_FAIL;
}
HRESULT WINAPI DllUnregisterServer(void) {
    FARPROC f = real("DllUnregisterServer");
    return f ? ((HRESULT (WINAPI *)(void))f)() : E_FAIL;
}
void *WINAPI GetdfDIJoystick(void) {
    FARPROC f = real("GetdfDIJoystick");
    return f ? ((void *(WINAPI *)(void))f)() : NULL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&lock);
        char dir[MAX_PATH], path[MAX_PATH];
        DWORD n = GetModuleFileNameA(h, dir, MAX_PATH);
        while (n && dir[n - 1] != '\\') n--;
        dir[n] = 0;
        snprintf(path, sizeof path, "%sdinput8_invert.log", dir);
        g_log = fopen(path, "w");
        logmsg("invert proxy v3 loaded (%d-bit)", (int)(sizeof(void *) * 8));
        load_settings(dir);
        // Hooking suspends threads, which isn't safe under the loader lock; defer it.
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
