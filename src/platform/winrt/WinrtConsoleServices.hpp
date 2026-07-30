#pragma once
// Installs the WinRT/UWP ConsoleServices implementation (save data in the app's LocalFolder, lifecycle
// from CoreApplication Suspending/Resuming). Call once at startup from the UWP entry (uwp/App.cpp),
// BEFORE Application::run(), so the engine routes settings/hotbar/config through it. No-op linkage on
// non-WinRT builds (this file is only compiled into the xbox target).

namespace uaro {

// Create + register the UWP ConsoleServices as the active instance (setConsoleServices). The instance
// is owned statically for the process lifetime. Safe to call once.
void installWinrtConsoleServices();

}  // namespace uaro
