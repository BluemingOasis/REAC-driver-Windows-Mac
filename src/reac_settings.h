#pragma once

#include <string>

struct ReacSettings {
    std::string capture_selector = "Realtek";
    std::string output_selector = "Speakers";
};

ReacSettings load_reac_settings();
bool save_reac_settings(const ReacSettings& settings);
