// Dear ImGui v1.91.5-docking keeps process-local viewport and context pointers in window properties any process can
// read. Hovering another ImGui application's window can then dereference that application's addresses (upstream
// issue #8162). This compiles the pinned Win32 backend with its property reads kept to this process's own windows,
// without changing the fetched sources.
#include <Windows.h>
static HANDLE sm_pc_local_window_property(HWND window, LPCSTR name) {
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != GetCurrentProcessId())
        return nullptr;
    return GetPropA(window, name);
}
#define GetPropA sm_pc_local_window_property
#include "imgui_impl_win32.cpp"
#undef GetPropA
