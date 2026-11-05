#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace pif {
using PropertyMap = std::map<std::string, std::string>;
using PropertyCallback = void (*)(void *, const char *, const char *, uint32_t);

struct PropertyReadContext {
    PropertyCallback callback;
    void *cookie;
    const PropertyMap *properties;
};

inline const std::string *property_override(const PropertyMap &properties, const char *name) {
    if (!name) return nullptr;
    auto exact = properties.find(name);
    if (exact != properties.end()) return &exact->second;
    const std::string property(name);
    for (const auto &[pattern, replacement] : properties) {
        if (pattern.starts_with("*") && property.ends_with(pattern.substr(1))) return &replacement;
    }
    return nullptr;
}

// Bionic invokes the callback synchronously. Each read owns a stack context,
// supporting null/reused cookies, nested reads, and concurrent readers without
// a global callback registry or a lock held across arbitrary client code.
inline void mapped_property_callback(void *opaque, const char *name,
                                     const char *value, uint32_t serial) {
    const auto &context = *static_cast<PropertyReadContext *>(opaque);
    if (!name || !value) {
        context.callback(context.cookie, name, value, serial);
        return;
    }
    if (const auto *replacement = property_override(*context.properties, name))
        value = replacement->c_str();
    context.callback(context.cookie, name, value, serial);
}
}
