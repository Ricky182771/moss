#include "chatbot/code/permissions.h"

#include <algorithm>
#include <array>
#include <utility>

namespace chatbot::code {

namespace {

Permission read_only_rule(std::string_view name, std::string_view /*arguments*/) {
    constexpr std::array<std::string_view, 4> kReadOnly{"read", "glob", "grep", "todo_write"};
    return std::find(kReadOnly.begin(), kReadOnly.end(), name) != kReadOnly.end()
               ? Permission::Allow
               : Permission::Ask;
}

} // namespace

PermissionEngine::PermissionEngine() : rule_(read_only_rule) {}

PermissionEngine::PermissionEngine(Rule rule) : rule_(std::move(rule)) {}

Permission PermissionEngine::decide(std::string_view name, std::string_view arguments) const {
    const Permission permission = rule_ ? rule_(name, arguments) : Permission::Ask;
    if (permission == Permission::Ask && always_.contains(name)) {
        return Permission::Allow;
    }
    return permission;
}

void PermissionEngine::allow_always(std::string_view name) {
    always_.emplace(name);
}

} // namespace chatbot::code
