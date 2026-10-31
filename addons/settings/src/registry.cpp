#include "chatbot/addon_settings/registry.h"

#include "chatbot/addon_settings/telegram_section.h"

#include <array>

namespace chatbot::addon_settings {
namespace {

constexpr std::array<AddonInfo, 1> kAddons{{
    {"telegram", "Telegram", "moss-telegram", kTelegramConfigVersion,
     "moss-telegram.service", "Bot de Telegram de Moss"},
}};

} // namespace

std::span<const AddonInfo> known_addons() {
    return kAddons;
}

const AddonInfo* find_addon(std::string_view name) {
    for (const AddonInfo& addon : kAddons) {
        if (addon.name == name) {
            return &addon;
        }
    }
    return nullptr;
}

} // namespace chatbot::addon_settings
