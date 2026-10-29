#pragma once
#include <windows.h>

namespace pulse::pcie {
constexpr DWORD MaximumSavings = 2;
enum class Outcome { WaitingForBattery, Verified, Corrected, Failed };
struct Result {
  Outcome outcome = Outcome::Failed;
  DWORD error = ERROR_SUCCESS;
  bool wrote = false;
};
struct PendingApply {
  bool needed = false;
  GUID scheme{};
};

// Shared by the real Windows adapter and deterministic tests. No AC setter
// exists.
template <class Backend> Result enforce(Backend &api, PendingApply &pending) {
  BYTE source = 255;
  DWORD error = api.source(source);
  if (error)
    return {Outcome::Failed, error};
  if (source == 1)
    return {Outcome::WaitingForBattery};
  if (source != 0)
    return {Outcome::Failed, ERROR_NOT_READY};
  GUID scheme{};
  if ((error = api.active(scheme)))
    return {Outcome::Failed, error};
  DWORD dc = 0;
  if ((error = api.read(scheme, dc)))
    return {Outcome::Failed, error};
  if (dc > MaximumSavings)
    return {Outcome::Failed, ERROR_INVALID_DATA};
  if (pending.needed && pending.scheme != scheme)
    pending.needed = false;
  if (dc == MaximumSavings && !pending.needed)
    return {Outcome::Verified};
  // Recheck immediately before mutation; Windows has no atomic
  // source/plan/write API.
  if ((error = api.source(source)))
    return {Outcome::Failed, error};
  if (source != 0)
    return {source == 1 ? Outcome::WaitingForBattery : Outcome::Failed,
            static_cast<DWORD>(source == 1 ? ERROR_SUCCESS : ERROR_NOT_READY)};
  GUID current{};
  if ((error = api.active(current)))
    return {Outcome::Failed, error};
  if (current != scheme)
    return {Outcome::Failed, ERROR_RETRY};
  bool wrote = false;
  if (dc != MaximumSavings) {
    if ((error = api.write(scheme, MaximumSavings)))
      return {Outcome::Failed, error};
    wrote = true;
    pending = {true, scheme};
  }
  // Never switch back to a plan that was replaced during our write.
  if ((error = api.active(current)))
    return {Outcome::Failed, error, wrote};
  if (current != scheme)
    return {Outcome::Failed, ERROR_RETRY, wrote};
  if ((error = api.source(source)))
    return {Outcome::Failed, error, wrote};
  if (source != 0)
    return {source == 1 ? Outcome::WaitingForBattery : Outcome::Failed,
            static_cast<DWORD>(source == 1 ? ERROR_SUCCESS : ERROR_NOT_READY),
            wrote};
  if ((error = api.activate(scheme)))
    return {Outcome::Failed, error, wrote};
  if ((error = api.read(scheme, dc)))
    return {Outcome::Failed, error, wrote};
  if (dc != MaximumSavings)
    return {Outcome::Failed, ERROR_WRITE_FAULT, wrote};
  pending.needed = false;
  return {Outcome::Corrected, ERROR_SUCCESS, wrote};
}
} // namespace pulse::pcie
