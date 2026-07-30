// uaRO — WinRT/UWP ConsoleServices: save data + app lifecycle for Xbox Dev Mode / UWP.
//
// Save data lives in the app's LocalFolder (ApplicationData::Current().LocalFolder()). Files inside
// that folder are reachable with ordinary C++ streams in UWP, so `key` maps to LocalFolder/<key> just
// like the desktop impl maps it to pref_dir/<key> — byte-identical blobs, no SDK containers needed for
// Dev Mode. (Full GDK would swap this for XGameSave containers + XUser; that's a licensed step.)
//
// Lifecycle: CoreApplication Suspending -> Suspended (flush + stop the frame), Resuming -> Resumed.
// The engine already pauses the loop on Suspended (Application.cpp) and reacquires on Resumed.

#include "platform/winrt/WinrtConsoleServices.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/ConsoleServices.hpp"

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Windows::Foundation;  // IInspectable (Suspending/Resuming handler signatures)
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;

namespace uaro {
namespace {

std::filesystem::path localRoot() {
    // ApplicationData LocalFolder — persisted per-user on the console, backed up with the title.
    const auto path = Windows::Storage::ApplicationData::Current().LocalFolder().Path();
    return std::filesystem::path(std::wstring(path.c_str()));
}

class WinrtConsoleServices final : public ConsoleServices {
public:
    WinrtConsoleServices() {
        // Route OS lifecycle into the engine callback. Suspending takes a deferral so we finish before
        // the OS freezes the process.
        CoreApplication::Suspending([this](IInspectable const&, auto const& args) {
            auto deferral = args.SuspendingOperation().GetDeferral();
            if (cb_) cb_(AppLifecycle::Suspended);
            deferral.Complete();
        });
        CoreApplication::Resuming([this](IInspectable const&, IInspectable const&) {
            if (cb_) cb_(AppLifecycle::Resumed);
        });
    }

    void onLifecycle(std::function<void(AppLifecycle)> cb) override { cb_ = std::move(cb); }

    bool saveWrite(const std::string& key, const std::vector<u8>& data) override {
        try {
            const auto p = localRoot() / key;
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            if (!data.empty())
                f.write(reinterpret_cast<const char*>(data.data()),
                        static_cast<std::streamsize>(data.size()));
            return static_cast<bool>(f);
        } catch (...) {
            return false;
        }
    }

    bool saveRead(const std::string& key, std::vector<u8>& out) override {
        try {
            const auto p = localRoot() / key;
            std::ifstream f(p, std::ios::binary | std::ios::ate);
            if (!f) return false;
            const auto n = f.tellg();
            if (n < 0) return false;
            out.resize(static_cast<size_t>(n));
            f.seekg(0);
            if (n > 0)
                f.read(reinterpret_cast<char*>(out.data()), n);
            return static_cast<bool>(f);
        } catch (...) {
            return false;
        }
    }

    bool saveExists(const std::string& key) override {
        try {
            std::error_code ec;
            return std::filesystem::exists(localRoot() / key, ec);
        } catch (...) {
            return false;
        }
    }

    std::string activeUserName() override { return {}; }  // TODO(GDK): XUser display name
    bool onlineAllowed() override { return true; }        // TODO(GDK): XUser multiplayer privilege

private:
    std::function<void(AppLifecycle)> cb_;
};

}  // namespace

void installWinrtConsoleServices() {
    static WinrtConsoleServices instance;
    setConsoleServices(&instance);
}

}  // namespace uaro
