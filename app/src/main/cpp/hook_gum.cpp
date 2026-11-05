#include "hook_backend.hpp"
#include "frida-gum.h"
#include <android/log.h>

namespace pif {
const char *hook_backend_name() { return "gum"; }

bool install_hook(void *target, void *replacement, void **original) {
    if (!target || !replacement || !original) return false;
    // Called after specialization in a single target process, never in Zygote.
    static GumInterceptor *interceptor = [] {
        gum_init_embedded();
        return gum_interceptor_obtain();
    }();
    gum_interceptor_begin_transaction(interceptor);
    // Fast replacement avoids Gum's invocation recursion guard: a client's
    // property callback may synchronously read another mapped property.
    GumReplaceReturn result = gum_interceptor_replace_fast(
        interceptor, target, replacement, original, nullptr);
    gum_interceptor_end_transaction(interceptor);
    if (result != GUM_REPLACE_OK || !*original) {
        __android_log_print(ANDROID_LOG_ERROR, "PIF/Native",
            "Gum hook installation failed: %d", static_cast<int>(result));
        return false;
    }
    // Keep the interceptor alive together with the hook; do not deinitialize Gum
    // or unmap this module while a target can call the replacement/trampoline.
    return true;
}
}
