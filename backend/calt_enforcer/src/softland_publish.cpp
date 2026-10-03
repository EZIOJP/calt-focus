#include "softland_publish.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

}  // namespace

bool PublishSoftlandMirror(const std::wstring& behaviorDir, const ProductivitySoftland& s) {
  if (s.document_json.empty() || behaviorDir.empty()) return false;
  CreateDirectoryW(behaviorDir.c_str(), nullptr);
  const std::wstring dest = Join(behaviorDir, L"softland_policy.json");
  const std::wstring tmp = Join(behaviorDir, L"softland_policy.json.tmp");

  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, tmp.c_str(), L"wb");
#else
  f = _wfopen(tmp.c_str(), L"wb");
#endif
  if (!f) return false;
  fwrite(s.document_json.data(), 1, s.document_json.size(), f);
  fclose(f);

  if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#if defined(_MSC_VER)
    _wfopen_s(&f, dest.c_str(), L"wb");
#else
    f = _wfopen(dest.c_str(), L"wb");
#endif
    if (!f) {
      DeleteFileW(tmp.c_str());
      return false;
    }
    fwrite(s.document_json.data(), 1, s.document_json.size(), f);
    fclose(f);
    DeleteFileW(tmp.c_str());
  }
  return true;
}
