#pragma once
#include <windows.h>
#include <string>

namespace efficiency {
enum class Option : unsigned { Update, Realtime };
enum class State : LONG { Off, Starting, Applied, Pending, Blocked, RestoreFailed, Retry };
constexpr UINT ChangedMessage = WM_APP + 10;

// The UI never holds administrator privileges. No helper exists until an option is selected.
class Client {
    HANDLE mapping_ = nullptr, command_ = nullptr, ready_ = nullptr, process_ = nullptr;
    void *shared_ = nullptr;
    std::wstring session_;
    DWORD error_ = 0;
    DWORD start(HWND owner);

  public:
    ~Client();
    bool selected(Option option) const;
    DWORD toggle(HWND owner, Option option);
    DWORD resume(HWND owner);
    std::wstring status(Option option) const;
    std::wstring report() const;
    void stop();
};
int helper(const std::wstring &session, DWORD parent, HWND owner);
bool recoveryPending();
// Read-only provider/SCM check. Does not create a recovery journal or change settings.
int diagnose(const std::wstring &output);
// Explicit administrative test: changes wuauserv briefly, then restores its baseline.
int exercise(const std::wstring &output);
int clientExercise(const std::wstring &output);
} // namespace efficiency
