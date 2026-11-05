#include "hook_backend.hpp"
#include "property_callback.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <thread>
#include <vector>

using Read = void (*)(const prop_info *, pif::PropertyCallback, void *);
static Read original;
static Read reader;
static const prop_info *model;
static const prop_info *device;
static pif::PropertyMap mappings;
static std::atomic<unsigned> failures{0};
static std::atomic<unsigned> calls{0};

static void replacement(const prop_info *info, pif::PropertyCallback callback, void *cookie) {
    if (!info || !callback) return original(info, callback, cookie);
    pif::PropertyReadContext context{callback, cookie, &mappings};
    original(info, pif::mapped_property_callback, &context);
}

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
    mappings = {{"ro.product.model", "offline-exact"}, {"*.device", "offline-wildcard"}};
    if (!pif::install_hook(reinterpret_cast<void *>(reader), reinterpret_cast<void *>(replacement),
                          reinterpret_cast<void **>(&original))) return 3;
    reader(model, verify, nullptr);
    Check check{"ro.product.model", "offline-exact", true};
    reader(model, verify, &check);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i != 8; ++i) threads.emplace_back([] {
        for (unsigned n = 0; n != 1000; ++n) {
            // Reuse one cookie across concurrent reads with different callbacks.
            static int shared;
            reader(model, [](void *, const char *, const char *v, uint32_t) {
                ++calls;
                if (strcmp(v, "offline-exact")) ++failures;
            }, &shared);
            Check nested{"ro.product.model", "offline-exact", true};
            reader(model, verify, &nested);
        }
    });
    for (auto &thread : threads) thread.join();
    char raw[PROP_VALUE_MAX];
    __system_property_get("ro.product.model", raw);
    const bool unchanged = strcmp(raw, "offline-exact") != 0;
    if (!unchanged) ++failures;
    printf("backend=%s callbacks=%u failures=%u global_property_unchanged=%s\n",
           pif::hook_backend_name(), calls.load(), failures.load(), unchanged ? "true" : "false");
    fflush(stdout);
    return failures.load() == 0 && calls.load() == 24003 ? 0 : 4;
}
