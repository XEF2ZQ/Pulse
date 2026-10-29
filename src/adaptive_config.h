#pragma once
#include "adaptive_policy.h"
#include <cwctype>
namespace pulse::adaptive {
inline std::wstring lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return wchar_t(towlower(c)); });
  return value;
}
struct Config {
  bool tiers = true, eco = false;
  Profile profile = Profile::Off;
  Background background = Background::Off;
  std::map<std::wstring, Override> rules;
};
inline bool parseRules(const std::wstring &text,
                       std::map<std::wstring, Override> &result) {
  std::map<std::wstring, Override> parsed;
  size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find(L'\n', start);
    auto line =
        text.substr(start, end == std::wstring::npos ? end : end - start);
    if (!line.empty() && line.back() == L'\r')
      line.pop_back();
    if (!line.empty()) {
      const auto split = line.rfind(L'|');
      if (split == std::wstring::npos || split < 3 ||
          split + 2 != line.size() || line[1] != L':' || line[2] != L'\\' ||
          line.back() < L'0' || line.back() > L'3' ||
          line.find_first_of(L"*?;\r") != std::wstring::npos ||
          parsed.size() >= 64)
        return false;
      auto path = lower(line.substr(0, split));
      if (!parsed.emplace(path, static_cast<Override>(line.back() - L'0'))
               .second)
        return false;
    }
    if (end == std::wstring::npos)
      break;
    start = end + 1;
  }
  result = std::move(parsed);
  return true;
}
inline std::wstring rulesText(const Config &config) {
  std::wstring text;
  for (const auto &[path, value] : config.rules)
    text +=
        path + L"|" + std::to_wstring(static_cast<unsigned>(value)) + L"\r\n";
  return text;
}
inline Config load(const std::wstring &path) {
  Config c;
  auto get = [&](const wchar_t *key, unsigned fallback) {
    return GetPrivateProfileIntW(L"Adaptive", key, fallback, path.c_str());
  };
  const auto p = get(L"Placement", 0), b = get(L"Background", 0),
             t = get(L"Tiers", 1), e = get(L"EcoQoS", 0);
  if (p > 2 || b > 2 || t > 1 || e > 1)
    return c;
  c.profile = static_cast<Profile>(p);
  c.background = static_cast<Background>(b);
  c.tiers = t != 0;
  c.eco = e != 0;
  wchar_t value[32768]{};
  const DWORD n = GetPrivateProfileStringW(L"Adaptive", L"Rules", L"", value,
                                           32768, path.c_str());
  if (n >= 32767)
    return Config{};
  std::wstring text(value);
  std::replace(text.begin(), text.end(), L';', L'\n');
  if (!parseRules(text, c.rules))
    return Config{};
  return c;
}
inline bool save(const std::wstring &path, const Config &c) {
  auto rules = rulesText(c);
  std::map<std::wstring, Override> validated;
  if (!parseRules(rules, validated))
    return false;
  rules.erase(std::remove(rules.begin(), rules.end(), L'\r'), rules.end());
  std::replace(rules.begin(), rules.end(), L'\n', L';');
  if (rules.size() > 30000 || static_cast<unsigned>(c.profile) > 2 ||
      static_cast<unsigned>(c.background) > 2)
    return false;
  const auto temp = path + L".adaptive.tmp";
  if (!CopyFileW(path.c_str(), temp.c_str(), FALSE) &&
      GetLastError() != ERROR_FILE_NOT_FOUND)
    return false;
  const std::pair<const wchar_t *, std::wstring> values[] = {
      {L"Tiers", c.tiers ? L"1" : L"0"},
      {L"Placement", std::to_wstring(static_cast<unsigned>(c.profile))},
      {L"Background", std::to_wstring(static_cast<unsigned>(c.background))},
      {L"EcoQoS", c.eco ? L"1" : L"0"},
      {L"Rules", rules}};
  for (const auto &[key, value] : values)
    if (!WritePrivateProfileStringW(L"Adaptive", key, value.c_str(),
                                    temp.c_str()))
      return false;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, temp.c_str());
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return false;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  return true;
}
} // namespace pulse::adaptive
