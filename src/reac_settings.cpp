#include "reac_settings.h"

#include <windows.h>

namespace {

constexpr const char* kSettingsKey = "Software\\REAC Decoder";

std::string read_registry_string(const char* value_name, const char* fallback)
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return fallback;
    }

    char value[1024]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LONG result = RegQueryValueExA(key, value_name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && type == REG_SZ && value[0]) {
        return value;
    }
    return fallback;
}

std::string read_env_string(const char* name, const char* fallback)
{
    char value[1024]{};
    const DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len > 0 && len < sizeof(value)) {
        return value;
    }
    return fallback;
}

bool write_registry_string(HKEY key, const char* name, const std::string& value)
{
    return RegSetValueExA(key,
                          name,
                          0,
                          REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>(value.size() + 1)) == ERROR_SUCCESS;
}

} // namespace

ReacSettings load_reac_settings()
{
    ReacSettings settings;
    const std::string capture_env = read_env_string("REAC_ASIO_DEVICE", settings.capture_selector.c_str());
    const std::string output_env = read_env_string("REAC_ASIO_OUTPUT", settings.output_selector.c_str());
    settings.capture_selector = read_registry_string("CaptureDevice", capture_env.c_str());
    settings.output_selector = read_registry_string("OutputDevice", output_env.c_str());
    return settings;
}

bool save_reac_settings(const ReacSettings& settings)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    const bool ok = write_registry_string(key, "CaptureDevice", settings.capture_selector) &&
                    write_registry_string(key, "OutputDevice", settings.output_selector);
    RegCloseKey(key);
    return ok;
}
