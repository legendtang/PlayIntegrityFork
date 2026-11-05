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
    const auto &properties = *context.properties;
    auto exact = properties.find(name);
    if (exact != properties.end()) {
        value = exact->second.c_str();
    } else {
        const std::string property(name);
        for (const auto &[pattern, replacement] : properties) {
            if (pattern.starts_with("*") && property.ends_with(pattern.substr(1))) {
                value = replacement.c_str();
                break;
            }
        }
    }
    context.callback(context.cookie, name, value, serial);
}
}
