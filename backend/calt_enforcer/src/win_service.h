#pragma once
#include <string>

// Run forever until stop requested. dbPath / lockPath are absolute.
int RunEnforcerLoop(const std::wstring& dbPath, const std::wstring& lockPath, volatile bool* stop);

// Windows Service control (install/start via sc.exe or service binary args).
int RunAsWindowsService(const std::wstring& dbPath, const std::wstring& lockPath);
