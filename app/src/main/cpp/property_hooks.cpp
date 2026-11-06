#include "property_hooks.hpp"
#include "hook_backend.hpp"
#include <android/log.h>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/system_properties.h>

namespace pif {
namespace {
const PropertyMap *mappings;
using ReadCallback = void (*)(const prop_info *, PropertyCallback, void *);
using Get = int (*)(const char *, char *);
using LegacyRead = int (*)(const prop_info *, char *, char *);
ReadCallback original_callback;
Get original_get;
LegacyRead original_read;
std::atomic<unsigned> audit_remaining{0};
std::atomic<unsigned> audit_emitted{0};
thread_local bool inside_audit;
struct AuditKey { const char *api; void *caller; char name[129]; };
AuditKey audit_keys[1024];
unsigned audit_key_count;
std::mutex audit_mutex;

struct AuditGuard {
    AuditGuard() { inside_audit = true; }
    ~AuditGuard() { inside_audit = false; }
};

void audit_read(const char *api, const char *name, bool mapped, void *caller) {
    if (inside_audit || !name) return;
    if (audit_remaining.load(std::memory_order_relaxed) == 0) return;
    AuditGuard guard;
    {
        std::lock_guard lock(audit_mutex);
        unsigned remaining = audit_remaining.load(std::memory_order_relaxed);
        if (remaining == 0) return;
        for (unsigned index = 0; index < audit_key_count; ++index) {
            if (audit_keys[index].caller == caller && strcmp(audit_keys[index].api, api) == 0 &&
                strncmp(audit_keys[index].name, name, 128) == 0) return;
        }
        auto &key = audit_keys[audit_key_count++];
        key.api = api;
        key.caller = caller;
        strncpy(key.name, name, 128);
        key.name[128] = '\0';
        audit_remaining.store(remaining - 1, std::memory_order_relaxed);
    }
    // Property values may contain device identifiers. Never log them here.
    Dl_info info{};
    const bool resolved = caller && dladdr(caller, &info) != 0 && info.dli_fname && info.dli_fbase;
    const char *library = resolved ? strrchr(info.dli_fname, '/') : nullptr;
    library = resolved ? (library ? library + 1 : info.dli_fname) : "unresolved";
    const uintptr_t offset = resolved ? reinterpret_cast<uintptr_t>(caller) -
        reinterpret_cast<uintptr_t>(info.dli_fbase) : 0;
    __android_log_print(ANDROID_LOG_DEBUG, "PIF/PropertyAudit",
        "api=%s key=%.128s mapped=%u caller=%.128s offset=0x%zx resolved=%u",
        api, name, mapped ? 1u : 0u, library, static_cast<size_t>(offset), resolved ? 1u : 0u);
    audit_emitted.fetch_add(1, std::memory_order_relaxed);
}

struct AuditedReadContext {
    PropertyReadContext read;
    void *caller;
};

void audited_callback(void *opaque, const char *name, const char *value, uint32_t serial) {
    auto &context = *static_cast<AuditedReadContext *>(opaque);
    audit_read("callback", name, property_override(*mappings, name) != nullptr, context.caller);
    mapped_property_callback(&context.read, name, value, serial);
}

int copy_legacy_value(const std::string &source, char *destination) {
    // The legacy API requires PROP_VALUE_MAX bytes; long read-only values are
    // only representable in full through the callback API. Never overrun it.
    const size_t size = source.size() < PROP_VALUE_MAX - 1 ? source.size() : PROP_VALUE_MAX - 1;
    memcpy(destination, source.data(), size);
    destination[size] = '\0';
    return static_cast<int>(size);
}

void callback_read(const prop_info *info, PropertyCallback callback, void *cookie) {
    if (!info || !callback) return original_callback(info, callback, cookie);
    AuditedReadContext context{{callback, cookie, mappings},
        __builtin_extract_return_addr(__builtin_return_address(0))};
    original_callback(info, audited_callback, &context);
}

int get(const char *name, char *value) {
    const int result = original_get(name, value);
    const auto *replacement = result > 0 ? property_override(*mappings, name) : nullptr;
    audit_read("get", name, replacement != nullptr,
        __builtin_extract_return_addr(__builtin_return_address(0)));
    if (result <= 0 || !value) return result;
    if (replacement)
        return copy_legacy_value(*replacement, value);
    return result;
}

int legacy_read(const prop_info *info, char *name, char *value) {
    const int result = original_read(info, name, value);
    if (result <= 0 || !info || !value) return result;
    struct LegacyContext { const std::string *replacement; void *caller; } context{
        nullptr, __builtin_extract_return_addr(__builtin_return_address(0))};
    // Legacy callers may omit the name output. Obtain it synchronously through
    // the original callback rather than assuming a populated name buffer.
    original_callback(info, [](void *opaque, const char *key, const char *, uint32_t) {
        auto &context = *static_cast<LegacyContext *>(opaque);
        context.replacement = property_override(*mappings, key);
        audit_read("read", key, context.replacement != nullptr, context.caller);
    }, &context);
    return context.replacement ? copy_legacy_value(*context.replacement, value) : result;
}

template<typename Function>
bool hook(const char *name, Function replacement, Function *original) {
    void *target = dlsym(RTLD_DEFAULT, name);
    const bool success = target && install_hook(target, reinterpret_cast<void *>(replacement),
                                               reinterpret_cast<void **>(original));
    __android_log_print(success ? ANDROID_LOG_DEBUG : ANDROID_LOG_ERROR, "PIF/Native",
        "Property API hook %s: backend=%s symbol=%s", success ? "active" : "FAILED",
        hook_backend_name(), name);
    return success;
}
}

bool install_property_hooks(const PropertyMap &properties, unsigned audit_limit) {
    if (mappings) return false; // one immutable configuration per process
    mappings = &properties;
    audit_remaining.store(audit_limit > 1024 ? 1024 : audit_limit, std::memory_order_relaxed);
    // Fail explicitly instead of treating partial API coverage as complete.
    if (!hook("__system_property_read_callback", callback_read, &original_callback)) return false;
    if (!hook("__system_property_get", get, &original_get)) return false;
    return hook("__system_property_read", legacy_read, &original_read);
}

unsigned property_audit_events() {
    return audit_emitted.load(std::memory_order_relaxed);
}

int original_property_get(const char *name, char *value) {
    return original_get ? original_get(name, value) : __system_property_get(name, value);
}
}
