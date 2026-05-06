#include "pcap_capture.h"
#include "reac_settings.h"
#include "waveout_player.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr int kCaptureCombo = 1001;
constexpr int kOutputCombo = 1002;
constexpr int kSaveButton = 1003;
constexpr int kCancelButton = 1004;

struct AppState {
    HWND capture_combo = nullptr;
    HWND output_combo = nullptr;
    std::vector<CaptureDeviceInfo> capture_devices;
    std::vector<WaveOutPlayer::DeviceInfo> output_devices;
    ReacSettings settings;
};

AppState* state_from(HWND hwnd)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}

void add_combo_item(HWND combo, const std::string& text)
{
    SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle)
{
    auto lower = [](std::string value) {
        for (char& ch : value) {
            ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

void select_capture(AppState& state)
{
    int selection = 0;
    for (size_t i = 0; i < state.capture_devices.size(); ++i) {
        const CaptureDeviceInfo& device = state.capture_devices[i];
        if (device.name == state.settings.capture_selector ||
            contains_case_insensitive(device.description, state.settings.capture_selector) ||
            contains_case_insensitive(device.name, state.settings.capture_selector)) {
            selection = static_cast<int>(i);
            break;
        }
    }
    SendMessageA(state.capture_combo, CB_SETCURSEL, selection, 0);
}

void select_output(AppState& state)
{
    int selection = 0;
    for (size_t i = 0; i < state.output_devices.size(); ++i) {
        const auto& device = state.output_devices[i];
        if (device.name == state.settings.output_selector ||
            contains_case_insensitive(device.name, state.settings.output_selector)) {
            selection = static_cast<int>(i);
            break;
        }
    }
    SendMessageA(state.output_combo, CB_SETCURSEL, selection, 0);
}

void populate_controls(HWND hwnd, AppState& state)
{
    PcapCapture capture;
    if (capture.available()) {
        state.capture_devices = capture.list_devices();
    }
    state.output_devices = WaveOutPlayer::list_devices();

    for (const CaptureDeviceInfo& device : state.capture_devices) {
        add_combo_item(state.capture_combo, device.description + "  [" + device.name + "]");
    }

    for (const auto& device : state.output_devices) {
        add_combo_item(state.output_combo, device.name);
    }

    if (state.capture_devices.empty()) {
        add_combo_item(state.capture_combo, "No Npcap devices found");
        EnableWindow(state.capture_combo, FALSE);
    }

    if (state.output_devices.empty()) {
        add_combo_item(state.output_combo, "No Windows audio outputs found");
        EnableWindow(state.output_combo, FALSE);
    }

    select_capture(state);
    select_output(state);

    CreateWindowA("STATIC",
                  "Restart Reaper or reselect the ASIO driver after saving.",
                  WS_CHILD | WS_VISIBLE,
                  20,
                  150,
                  460,
                  20,
                  hwnd,
                  nullptr,
                  nullptr,
                  nullptr);
}

void save_from_controls(HWND hwnd, AppState& state)
{
    const int capture_index = static_cast<int>(SendMessageA(state.capture_combo, CB_GETCURSEL, 0, 0));
    const int output_index = static_cast<int>(SendMessageA(state.output_combo, CB_GETCURSEL, 0, 0));

    if (capture_index >= 0 && capture_index < static_cast<int>(state.capture_devices.size())) {
        state.settings.capture_selector = state.capture_devices[static_cast<size_t>(capture_index)].name;
    }
    if (output_index >= 0 && output_index < static_cast<int>(state.output_devices.size())) {
        state.settings.output_selector = state.output_devices[static_cast<size_t>(output_index)].name;
    }

    if (!save_reac_settings(state.settings)) {
        MessageBoxA(hwnd, "Could not save settings.", "REAC ASIO Settings", MB_OK | MB_ICONERROR);
        return;
    }

    SetEnvironmentVariableA("REAC_ASIO_DEVICE", state.settings.capture_selector.c_str());
    SetEnvironmentVariableA("REAC_ASIO_OUTPUT", state.settings.output_selector.c_str());
    MessageBoxA(hwnd, "Settings saved.", "REAC ASIO Settings", MB_OK | MB_ICONINFORMATION);
    DestroyWindow(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        auto* state = new AppState();
        state->settings = load_reac_settings();
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        CreateWindowA("STATIC", "REAC capture adapter", WS_CHILD | WS_VISIBLE, 20, 20, 180, 20, hwnd, nullptr, nullptr, nullptr);
        state->capture_combo = CreateWindowA("COMBOBOX",
                                             "",
                                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             20,
                                             42,
                                             540,
                                             180,
                                             hwnd,
                                             reinterpret_cast<HMENU>(kCaptureCombo),
                                             nullptr,
                                             nullptr);

        CreateWindowA("STATIC", "Reaper monitor output", WS_CHILD | WS_VISIBLE, 20, 82, 180, 20, hwnd, nullptr, nullptr, nullptr);
        state->output_combo = CreateWindowA("COMBOBOX",
                                            "",
                                            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                            20,
                                            104,
                                            540,
                                            140,
                                            hwnd,
                                            reinterpret_cast<HMENU>(kOutputCombo),
                                            nullptr,
                                            nullptr);

        CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 365, 185, 90, 28, hwnd, reinterpret_cast<HMENU>(kSaveButton), nullptr, nullptr);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE, 470, 185, 90, 28, hwnd, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);
        populate_controls(hwnd, *state);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == kSaveButton) {
            save_from_controls(hwnd, *state_from(hwnd));
            return 0;
        }
        if (LOWORD(wparam) == kCancelButton) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        delete state_from(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show)
{
    const char* class_name = "ReacAsioSettingsWindow";
    WNDCLASSA wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0,
                                class_name,
                                "REAC ASIO Settings",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                600,
                                270,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return static_cast<int>(msg.wParam);
}
