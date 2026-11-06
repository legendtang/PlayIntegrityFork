#pragma once
#include "property_callback.hpp"

namespace pif {
// The immutable map must outlive the hooks (normally the target process).
bool install_property_hooks(const PropertyMap &properties, unsigned audit_limit = 0);
unsigned property_audit_events();
// Local diagnostics can compare the original getter without rewriting storage.
int original_property_get(const char *name, char *value);
}
