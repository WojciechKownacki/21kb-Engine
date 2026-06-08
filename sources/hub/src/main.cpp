#include "HubApplication.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    kb::hub::HubApplication app;
    if (!app.Initialize(instance, showCommand)) {
        return 1;
    }
    return app.Run();
}
