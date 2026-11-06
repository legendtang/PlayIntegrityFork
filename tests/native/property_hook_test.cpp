#include "hook_backend.hpp"
#include "property_callback.hpp"
#include "property_hooks.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <thread>
#include <vector>

using Read = void (*)(const prop_info *, pif::PropertyCallback, void *);
static Read reader;
static const prop_info *model;
static const prop_info *device;
static pif::PropertyMap mappings;
static std::atomic<unsigned> failures{0};
static std::atomic<unsigned> calls{0};

struct Check { const char *name; const char *value; bool nested; };
static void verify(void *cookie, const char *name, const char *value, uint32_t) {
    calls.fetch_add(1, std::memory_order_relaxed);
    const Check fallback{"ro.product.model", "offline-exact", false};
    const auto &check = cookie ? *static_cast<Check *>(cookie) : fallback;
    if (strcmp(name, check.name) || strcmp(value, check.value)) ++failures;
    if (check.nested) {
        Check nested{"ro.product.device", "offline-wildcard", false};
        reader(device, verify, &nested);
    }
}

int main() {
    model = __system_property_find("ro.product.model");
    device = __system_property_find("ro.product.device");
    reader = reinterpret_cast<Read>(dlsym(RTLD_DEFAULT, "__system_property_read_callback"));
    if (!model || !device || !reader) return 2;
    char before[PROP_VALUE_MAX];
    __system_property_get("ro.product.model", before);
    mappings = {{"ro.product.model", "offline-exact"}, {"*.model", "wrong-precedence"},
                {"*.device", "offline-wildcard"}, {"ro.product.name", std::string(200, 'x')}};
    if (!pif::install_property_hooks(mappings, 32)) return 3;
    reader(model, verify, nullptr);
    Check check{"ro.product.model", "offline-exact", true};
    reader(model, verify, &check);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i != 8; ++i) threads.emplace_back([] {
        for (unsigned n = 0; n != 1000; ++n) {
            // Reuse one cookie across concurrent reads with different callbacks.
            static int shared;
            if (n & 1) {
                reader(device, [](void *, const char *, const char *v, uint32_t) {
                    ++calls;
                    if (strcmp(v, "offline-wildcard")) ++failures;
                }, &shared);
            } else {
                reader(model, [](void *, const char *, const char *v, uint32_t) {
                    ++calls;
                    if (strcmp(v, "offline-exact")) ++failures;
                }, &shared);
            }
            Check nested{"ro.product.model", "offline-exact", true};
            reader(model, verify, &nested);
        }
    });
    for (auto &thread : threads) thread.join();
    char raw[PROP_VALUE_MAX];
    pif::original_property_get("ro.product.model", raw);
    const bool unchanged = strcmp(raw, before) == 0;
    if (!unchanged) ++failures;
    bool legacy_ok = true;
    int length = __system_property_get("ro.product.model", raw);
    legacy_ok &= strcmp(raw, "offline-exact") == 0 && length == strlen("offline-exact");
    char name[PROP_NAME_MAX];
    auto legacy = reinterpret_cast<int (*)(const prop_info *, char *, char *)>(
        dlsym(RTLD_DEFAULT, "__system_property_read"));
    length = legacy(device, name, raw);
    legacy_ok &= strcmp(name, "ro.product.device") == 0 && strcmp(raw, "offline-wildcard") == 0;
    legacy_ok &= length == strlen("offline-wildcard");
    legacy(model, nullptr, raw);
    legacy_ok &= strcmp(raw, "offline-exact") == 0;
    struct { char value[PROP_VALUE_MAX]; unsigned char guard[16]; } bounded;
    memset(&bounded, 0xa5, sizeof(bounded));
    length = __system_property_get("ro.product.name", bounded.value);
    legacy_ok &= length == PROP_VALUE_MAX - 1 && bounded.value[length] == '\0';
    for (unsigned char byte : bounded.guard) legacy_ok &= byte == 0xa5;
    char missing[PROP_VALUE_MAX];
    legacy_ok &= __system_property_get("pif.nonexistent.offline.test", missing) == 0 && missing[0] == '\0';
    if (!legacy_ok) ++failures;
    const unsigned distinct_before = pif::property_audit_events();
    // Repeated reads must not use the budget. New synthetic names exercise the
    // cap without creating/changing any system property.
    for (unsigned n = 0; n != 64; ++n) {
        const std::string key = "pif.offline.audit." + std::to_string(n);
        __system_property_get(key.c_str(), missing);
    }
    if (distinct_before >= 32 || pif::property_audit_events() != 32) ++failures;
    printf("backend=%s callbacks=%u failures=%u global_property_unchanged=%s legacy_apis_consistent=%s\n",
           pif::hook_backend_name(), calls.load(), failures.load(), unchanged ? "true" : "false",
           legacy_ok ? "true" : "false");
    fflush(stdout);
    printf("property_audit_events=%u expected_limit=32\n", pif::property_audit_events());
    return failures.load() == 0 && calls.load() == 24003 ? 0 : 4;
}
