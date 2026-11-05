#pragma once

namespace pif {
// A successful hook and its trampoline live for the rest of the target process.
// The Zygisk module must remain loaded while the hook is active.
bool install_hook(void *target, void *replacement, void **original);
const char *hook_backend_name();
}
