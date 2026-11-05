#include "property_hooks.hpp"
#include "hook_backend.hpp"
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>
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
    PropertyReadContext context{callback, cookie, mappings};
    original_callback(info, mapped_property_callback, &context);
}

int get(const char *name, char *value) {
    const int result = original_get(name, value);
    if (result <= 0 || !value) return result;
    if (const auto *replacement = property_override(*mappings, name))
        return copy_legacy_value(*replacement, value);
    return result;
}

int legacy_read(const prop_info *info, char *name, char *value) {
    const int result = original_read(info, name, value);
    if (result <= 0 || !info || !value) return result;
    const std::string *replacement = nullptr;
    // Legacy callers may omit the name output. Obtain it synchronously through
    // the original callback rather than assuming a populated name buffer.
    original_callback(info, [](void *opaque, const char *key, const char *, uint32_t) {
        *static_cast<const std::string **>(opaque) = property_override(*mappings, key);
    }, &replacement);
    return replacement ? copy_legacy_value(*replacement, value) : result;
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

bool install_property_hooks(const PropertyMap &properties) {
    if (mappings) return false; // one immutable configuration per process
    mappings = &properties;
    // Fail explicitly instead of treating partial API coverage as complete.
    if (!hook("__system_property_read_callback", callback_read, &original_callback)) return false;
    if (!hook("__system_property_get", get, &original_get)) return false;
    return hook("__system_property_read", legacy_read, &original_read);
}

int original_property_get(const char *name, char *value) {
    return original_get ? original_get(name, value) : __system_property_get(name, value);
}
}
