#pragma once

#include <string>
#include <utility>
#include <vector>

/** Small JSON DOM for watch dumps. Objects keep insertion order. */
struct Wj {
  enum Type { kNull, kBool, kNum, kStr, kArr, kObj } type = kNull;
  bool b = false;
  std::string num;
  std::string str;
  std::vector<Wj> arr;
  std::vector<std::pair<std::string, Wj>> obj;
};

bool WjParse(const std::string& in, Wj* out, std::string* err);
std::string WjStringify(const Wj& v);

const Wj* WjGet(const Wj& obj, const char* key);
void WjSet(Wj& obj, const std::string& key, Wj val);

Wj WjNull();
Wj WjBool(bool v);
Wj WjInt(long long n);
Wj WjNum(double v, int decimals);
Wj WjStr(std::string s);

bool WjAsInt(const Wj& v, int* out);
bool WjAsString(const Wj& v, std::string* out);

/**
 * One-level object merge used by chunked watch dumps.
 * Nulls are skipped. A sleep object with total_min <= 0 does not wipe a real prior sleep.
 */
void WjMergeDay(Wj* prior, const Wj& incoming);
