#include "hook_backend.hpp"
#include "shadowhook.h"
#include <android/log.h>

extern "C" int sh_linker_init(void) { return 0; }

namespace pif {
const char *hook_backend_name() { return "shadowhook"; }
bool install_hook(void *target, void *replacement, void **original) {
    if (!target || !replacement || !original) return false;
    int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (result != 0) return false;
    void *handle = shadowhook_hook_sym_addr(target, replacement, original);
    if (!handle || !*original) {
        __android_log_print(ANDROID_LOG_ERROR, "PIF/Native",
            "ShadowHook installation failed: %d", shadowhook_get_errno());
        return false;
    }
    return true;
}
}
