#include "wearable_json.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <climits>

namespace {

void Skip(const std::string& in, size_t* i) {
  while (*i < in.size()) {
    char c = in[*i];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
    ++*i;
  }
}

bool ParseValue(const std::string& in, size_t* i, Wj* out, int depth, std::string* err);

void Fail(std::string* err, const char* msg) {
  if (err && err->empty()) *err = msg;
}

bool Hex4(const std::string& in, size_t* i, unsigned* code) {
  if (*i + 4 > in.size()) return false;
  unsigned c = 0;
  for (int n = 0; n < 4; ++n) {
    char h = in[(*i)++];
    c <<= 4;
    if (h >= '0' && h <= '9')
      c |= (unsigned)(h - '0');
    else if (h >= 'a' && h <= 'f')
      c |= (unsigned)(h - 'a' + 10);
    else if (h >= 'A' && h <= 'F')
      c |= (unsigned)(h - 'A' + 10);
    else
      return false;
  }
  *code = c;
  return true;
}

void AppendUtf8(std::string* out, unsigned code) {
  if (code >= 0xD800 && code <= 0xDFFF) {
    out->push_back('?');
    return;
  }
  if (code < 0x80) {
    out->push_back((char)code);
  } else if (code < 0x800) {
    out->push_back((char)(0xC0 | (code >> 6)));
    out->push_back((char)(0x80 | (code & 0x3F)));
  } else {
    out->push_back((char)(0xE0 | (code >> 12)));
    out->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
    out->push_back((char)(0x80 | (code & 0x3F)));
  }
}

bool ParseString(const std::string& in, size_t* i, std::string* out, std::string* err) {
  if (*i >= in.size() || in[*i] != '"') {
    Fail(err, "string");
    return false;
  }
  ++*i;
  out->clear();
  while (*i < in.size()) {
    unsigned char c = (unsigned char)in[*i];
    if (c == '"') {
      ++*i;
      return true;
    }
    if (c == '\\') {
      if (*i + 1 >= in.size()) {
        Fail(err, "escape");
        return false;
      }
      char e = in[++*i];
      ++*i;
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out->push_back(e);
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'u': {
          unsigned code = 0;
          if (!Hex4(in, i, &code)) {
            Fail(err, "unicode");
            return false;
          }
          AppendUtf8(out, code);
          break;
        }
        default:
          Fail(err, "escape");
          return false;
      }
      continue;
    }
    if (c < 0x20) {
      Fail(err, "control");
      return false;
    }
    out->push_back((char)c);
    ++*i;
  }
  Fail(err, "string_eof");
  return false;
}

bool ParseNumber(const std::string& in, size_t* i, std::string* out, std::string* err) {
  size_t start = *i;
  if (*i < in.size() && in[*i] == '-') ++*i;
  if (*i >= in.size() || in[*i] < '0' || in[*i] > '9') {
    Fail(err, "number");
    return false;
  }
  if (in[*i] == '0') {
    ++*i;
  } else {
    while (*i < in.size() && in[*i] >= '0' && in[*i] <= '9') ++*i;
  }
  if (*i < in.size() && in[*i] == '.') {
    ++*i;
    if (*i >= in.size() || in[*i] < '0' || in[*i] > '9') {
      Fail(err, "number");
      return false;
    }
    while (*i < in.size() && in[*i] >= '0' && in[*i] <= '9') ++*i;
  }
  if (*i < in.size() && (in[*i] == 'e' || in[*i] == 'E')) {
    ++*i;
    if (*i < in.size() && (in[*i] == '+' || in[*i] == '-')) ++*i;
    if (*i >= in.size() || in[*i] < '0' || in[*i] > '9') {
      Fail(err, "number");
      return false;
    }
    while (*i < in.size() && in[*i] >= '0' && in[*i] <= '9') ++*i;
  }
  *out = in.substr(start, *i - start);
  return true;
}

bool ParseValue(const std::string& in, size_t* i, Wj* out, int depth, std::string* err) {
  if (depth > 48) {
    Fail(err, "depth");
    return false;
  }
  Skip(in, i);
  if (*i >= in.size()) {
    Fail(err, "eof");
    return false;
  }
  char c = in[*i];
  if (c == 'n') {
    if (in.compare(*i, 4, "null") != 0) {
      Fail(err, "null");
      return false;
    }
    *i += 4;
    *out = WjNull();
    return true;
  }
  if (c == 't') {
    if (in.compare(*i, 4, "true") != 0) {
      Fail(err, "bool");
      return false;
    }
    *i += 4;
    *out = WjBool(true);
    return true;
  }
  if (c == 'f') {
    if (in.compare(*i, 5, "false") != 0) {
      Fail(err, "bool");
      return false;
    }
    *i += 5;
    *out = WjBool(false);
    return true;
  }
  if (c == '"') {
    std::string s;
    if (!ParseString(in, i, &s, err)) return false;
    *out = WjStr(std::move(s));
    return true;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    std::string n;
    if (!ParseNumber(in, i, &n, err)) return false;
    Wj v;
    v.type = Wj::kNum;
    v.num = std::move(n);
    *out = std::move(v);
    return true;
  }
  if (c == '[') {
    ++*i;
    Wj arr;
    arr.type = Wj::kArr;
    Skip(in, i);
    if (*i < in.size() && in[*i] == ']') {
      ++*i;
      *out = std::move(arr);
      return true;
    }
    while (*i < in.size()) {
      Wj item;
      if (!ParseValue(in, i, &item, depth + 1, err)) return false;
      arr.arr.push_back(std::move(item));
      Skip(in, i);
      if (*i < in.size() && in[*i] == ',') {
        ++*i;
        continue;
      }
      if (*i < in.size() && in[*i] == ']') {
        ++*i;
        *out = std::move(arr);
        return true;
      }
      Fail(err, "array");
      return false;
    }
    Fail(err, "array_eof");
    return false;
  }
  if (c == '{') {
    ++*i;
    Wj obj;
    obj.type = Wj::kObj;
    Skip(in, i);
    if (*i < in.size() && in[*i] == '}') {
      ++*i;
      *out = std::move(obj);
      return true;
    }
    while (*i < in.size()) {
      Skip(in, i);
      std::string key;
      if (!ParseString(in, i, &key, err)) return false;
      Skip(in, i);
      if (*i >= in.size() || in[*i] != ':') {
        Fail(err, "colon");
        return false;
      }
      ++*i;
      Wj val;
      if (!ParseValue(in, i, &val, depth + 1, err)) return false;
      WjSet(obj, key, std::move(val));
      Skip(in, i);
      if (*i < in.size() && in[*i] == ',') {
        ++*i;
        continue;
      }
      if (*i < in.size() && in[*i] == '}') {
        ++*i;
        *out = std::move(obj);
        return true;
      }
      Fail(err, "object");
      return false;
    }
    Fail(err, "object_eof");
    return false;
  }
  Fail(err, "value");
  return false;
}

void Escape(const std::string& s, std::string* out) {
  out->push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\b':
        out->append("\\b");
        break;
      case '\f':
        out->append("\\f");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out->append(buf);
        } else {
          out->push_back((char)c);
        }
    }
  }
  out->push_back('"');
}

bool SleepIsEmpty(const Wj& sleep) {
  const Wj* total = WjGet(sleep, "total_min");
  if (!total) return false;
  int n = 0;
  if (!WjAsInt(*total, &n)) return false;
  return n <= 0;
}

}  // namespace

Wj WjNull() {
  Wj v;
  v.type = Wj::kNull;
  return v;
}

Wj WjBool(bool v) {
  Wj o;
  o.type = Wj::kBool;
  o.b = v;
  return o;
}

Wj WjInt(long long n) {
  Wj o;
  o.type = Wj::kNum;
  o.num = std::to_string(n);
  return o;
}

Wj WjNum(double v, int decimals) {
  Wj o;
  o.type = Wj::kNum;
  if (decimals < 0) decimals = 0;
  if (decimals > 6) decimals = 6;
  char fmt[8];
  snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  char buf[64];
  snprintf(buf, sizeof(buf), fmt, v);
  o.num = buf;
  return o;
}

Wj WjStr(std::string s) {
  Wj o;
  o.type = Wj::kStr;
  o.str = std::move(s);
  return o;
}

bool WjParse(const std::string& in, Wj* out, std::string* err) {
  if (err) err->clear();
  size_t i = 0;
  if (in.size() >= 3 && (unsigned char)in[0] == 0xEF && (unsigned char)in[1] == 0xBB &&
      (unsigned char)in[2] == 0xBF) {
    i = 3;
  }
  Wj v;
  if (!ParseValue(in, &i, &v, 0, err)) return false;
  Skip(in, &i);
  if (i != in.size()) {
    Fail(err, "trailing");
    return false;
  }
  *out = std::move(v);
  return true;
}

std::string WjStringify(const Wj& v) {
  switch (v.type) {
    case Wj::kNull:
      return "null";
    case Wj::kBool:
      return v.b ? "true" : "false";
    case Wj::kNum:
      return v.num.empty() ? "0" : v.num;
    case Wj::kStr: {
      std::string o;
      Escape(v.str, &o);
      return o;
    }
    case Wj::kArr: {
      std::string o = "[";
      for (size_t n = 0; n < v.arr.size(); ++n) {
        if (n) o.push_back(',');
        o += WjStringify(v.arr[n]);
      }
      o.push_back(']');
      return o;
    }
    case Wj::kObj: {
      std::string o = "{";
      for (size_t n = 0; n < v.obj.size(); ++n) {
        if (n) o.push_back(',');
        Escape(v.obj[n].first, &o);
        o.push_back(':');
        o += WjStringify(v.obj[n].second);
      }
      o.push_back('}');
      return o;
    }
  }
  return "null";
}

const Wj* WjGet(const Wj& obj, const char* key) {
  if (obj.type != Wj::kObj || !key) return nullptr;
  for (const auto& kv : obj.obj) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

void WjSet(Wj& obj, const std::string& key, Wj val) {
  if (obj.type != Wj::kObj) {
    obj = Wj{};
    obj.type = Wj::kObj;
  }
  for (auto& kv : obj.obj) {
    if (kv.first == key) {
      kv.second = std::move(val);
      return;
    }
  }
  obj.obj.emplace_back(key, std::move(val));
}

bool WjAsInt(const Wj& v, int* out) {
  if (!out || v.type != Wj::kNum || v.num.empty()) return false;
  char* end = nullptr;
  double d = strtod(v.num.c_str(), &end);
  if (!end || *end != 0 || !std::isfinite(d)) return false;
  long long n = std::llround(d);
  if (n > INT_MAX || n < INT_MIN) return false;
  *out = (int)n;
  return true;
}

bool WjAsString(const Wj& v, std::string* out) {
  if (!out || v.type != Wj::kStr) return false;
  *out = v.str;
  return true;
}

void WjMergeDay(Wj* prior, const Wj& incoming) {
  if (!prior) return;
  if (prior->type != Wj::kObj) {
    *prior = Wj{};
    prior->type = Wj::kObj;
  }
  if (incoming.type != Wj::kObj) return;
  for (const auto& kv : incoming.obj) {
    if (kv.second.type == Wj::kNull) continue;
    if (kv.first == "sleep" && kv.second.type == Wj::kObj) {
      const Wj* oldSleep = WjGet(*prior, "sleep");
      if (oldSleep && oldSleep->type == Wj::kObj && SleepIsEmpty(kv.second)) continue;
    }
    if (kv.second.type == Wj::kObj) {
      const Wj* old = WjGet(*prior, kv.first.c_str());
      if (old && old->type == Wj::kObj) {
        Wj merged = *old;
        for (const auto& inner : kv.second.obj) {
          if (inner.second.type == Wj::kNull) continue;
          WjSet(merged, inner.first, inner.second);
        }
        WjSet(*prior, kv.first, std::move(merged));
        continue;
      }
    }
    WjSet(*prior, kv.first, kv.second);
  }
}
