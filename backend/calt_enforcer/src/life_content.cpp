#include "life_content.h"
#include "productivity_store.h"
#include "softland_publish.h"
#include "sqlite3.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// gDb declared in productivity_store.h

namespace {

std::string EscapeJson(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        o.push_back(c);
    }
  }
  return o;
}

std::string LocalDateYmd() { return ProductivityLocalDate(); }

std::string IsoNow() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

bool DecodeJsonEscapeLocal(const std::string& body, size_t& i, std::string* out) {
  if (i >= body.size() || body[i] != '\\' || i + 1 >= body.size()) return false;
  char e = body[++i];
  ++i;
  switch (e) {
    case '"':
    case '\\':
    case '/':
      out->push_back(e);
      return true;
    case 'b':
      out->push_back('\b');
      return true;
    case 'f':
      out->push_back('\f');
      return true;
    case 'n':
      out->push_back('\n');
      return true;
    case 'r':
      out->push_back('\r');
      return true;
    case 't':
      out->push_back('\t');
      return true;
    case 'u': {
      if (i + 4 > body.size()) return false;
      unsigned code = 0;
      for (int n = 0; n < 4; ++n) {
        char c = body[i++];
        code <<= 4;
        if (c >= '0' && c <= '9')
          code |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
          code |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
          code |= (unsigned)(c - 'A' + 10);
        else
          return false;
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
      return true;
    }
    default:
      out->push_back(e);
      return true;
  }
}

bool JsonGetStringLocal(const std::string& body, const char* key, std::string* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size() || body[i] != '"') return false;
  ++i;
  std::string val;
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\') {
      if (!DecodeJsonEscapeLocal(body, i, &val)) return false;
      continue;
    }
    val.push_back(body[i++]);
  }
  *out = val;
  return true;
}

bool JsonGetIntLocal(const std::string& body, const char* key, int* out) {
  std::string needle = std::string("\"") + key + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return false;
  size_t colon = body.find(':', p + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
  if (i >= body.size()) return false;
  *out = atoi(body.c_str() + i);
  return true;
}

std::string ReadFileUtf8(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
    CloseHandle(h);
    return {};
  }
  std::string out(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD n = 0;
  BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &n, nullptr);
  CloseHandle(h);
  if (!ok) return {};
  out.resize(n);
  return out;
}

bool WriteFileUtf8Atomic(const std::wstring& dest, const std::string& body) {
  if (dest.empty()) return false;
  const size_t slash = dest.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    CreateDirectoryW(dest.substr(0, slash).c_str(), nullptr);
  }
  const std::wstring tmp = dest + L".tmp";
  FILE* f = nullptr;
#if defined(_MSC_VER)
  _wfopen_s(&f, tmp.c_str(), L"wb");
#else
  f = _wfopen(tmp.c_str(), L"wb");
#endif
  if (!f) return false;
  fwrite(body.data(), 1, body.size(), f);
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
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    DeleteFileW(tmp.c_str());
  }
  return true;
}

std::wstring BehaviorDirBesideBible(const std::wstring& bibleDataDir) {
  const size_t slash = bibleDataDir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  return bibleDataDir.substr(0, slash) + L"\\behavior";
}

void PublishDevotionMirrorBestEffort(const std::wstring& bibleDataDir,
                                     const std::wstring& behaviorDirOpt, int userId) {
  std::wstring beh = behaviorDirOpt;
  if (beh.empty()) beh = BehaviorDirBesideBible(bibleDataDir);
  if (beh.empty()) return;
  PublishBibleDevotionMirror(beh, bibleDataDir, userId);
}

struct PlanChapter {
  std::string book;
  int chapter = 1;
};

std::vector<PlanChapter> LoadPlan(const std::wstring& bibleDataDir) {
  std::vector<PlanChapter> plan;
  std::string body = ReadFileUtf8(bibleDataDir + L"\\chapter_index.json");
  if (body.empty()) {
    plan.push_back({"Genesis", 1});
    return plan;
  }
  // Naive scan: "book":"...","chapter":N
  size_t i = 0;
  while (i < body.size()) {
    size_t b = body.find("\"book\"", i);
    if (b == std::string::npos) break;
    std::string book;
    int ch = 0;
    if (!JsonGetStringLocal(body.substr(b), "book", &book)) {
      i = b + 6;
      continue;
    }
    size_t cpos = body.find("\"chapter\"", b);
    if (cpos == std::string::npos || cpos > b + 200) {
      i = b + 6;
      continue;
    }
    JsonGetIntLocal(body.substr(cpos), "chapter", &ch);
    if (!book.empty() && ch > 0) plan.push_back({book, ch});
    i = b + 6;
  }
  if (plan.empty()) plan.push_back({"Genesis", 1});
  return plan;
}

std::string ChapterKey(const std::string& book, int chapter) {
  return book + "|" + std::to_string(chapter);
}

bool LoadDayRow(int userId, const std::string& day, std::string* docOut) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT document_json FROM productivity_bible_day WHERE user_id=? AND day=? LIMIT 1;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, day.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const char* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    if (t && docOut) *docOut = t;
    ok = true;
  }
  sqlite3_finalize(st);
  return ok;
}

bool SaveDayRow(int userId, const std::string& day, const std::string& doc) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "INSERT INTO productivity_bible_day(user_id, day, document_json, updated_at) VALUES(?,?,?,?) "
      "ON CONFLICT(user_id, day) DO UPDATE SET document_json=excluded.document_json, "
      "updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  std::string now = IsoNow();
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, day.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, doc.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, now.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

std::string EmptyDayDoc(const std::string& day, const PlanChapter& assigned) {
  return std::string("{\"day\":\"") + day + "\",\"bible_seconds\":0,\"chapters_completed\":[],"
         "\"assigned_book\":\"" + EscapeJson(assigned.book) + "\",\"assigned_chapter\":" +
         std::to_string(assigned.chapter) + ",\"devotion\":{},\"last_heartbeat_at\":0}";
}

bool PublishBibleDone(bool done, const std::wstring& behaviorDir) {
  ProductivitySoftland s;
  if (!ProductivityLoadSoftland(s)) return false;
  std::string needle = "\"goals\"";
  size_t p = s.document_json.find(needle);
  if (p == std::string::npos) return false;
  size_t brace = s.document_json.find('{', p);
  if (brace == std::string::npos) return false;
  int depth = 0;
  size_t end = brace;
  for (; end < s.document_json.size(); ++end) {
    if (s.document_json[end] == '{') ++depth;
    else if (s.document_json[end] == '}') {
      --depth;
      if (depth == 0) {
        ++end;
        break;
      }
    }
  }
  std::string goalsObj = s.document_json.substr(brace, end - brace);
  if (!ProductivityReplaceJsonValue(goalsObj, "bible_done", done ? "true" : "false")) {
    if (goalsObj.size() > 2) {
      goalsObj.insert(goalsObj.size() - 1,
                      std::string(",\"bible_done\":") + (done ? "true" : "false"));
    }
  }
  const std::string today = ProductivityLocalDate();
  const std::string dateLit = done ? ("\"" + today + "\"") : "null";
  if (!ProductivityReplaceJsonValue(goalsObj, "bible_done_for_date", dateLit)) {
    if (goalsObj.size() > 2) {
      goalsObj.insert(goalsObj.size() - 1, std::string(",\"bible_done_for_date\":") + dateLit);
    }
  }
  if (!ProductivityReplaceJsonValue(s.document_json, "goals", goalsObj)) return false;
  // Always bump wall-clock so Focus mirrors/ImportIfStale see the SoftLand write.
  s.updated_at.clear();
  if (!ProductivitySaveSoftland(s)) return false;
  PublishSoftlandMirror(behaviorDir, s);
  ProductivityBumpSeq();
  return true;
}

int CountCompleted(const std::string& doc) {
  size_t p = doc.find("\"chapters_completed\"");
  if (p == std::string::npos) return 0;
  size_t a = doc.find('[', p);
  size_t b = doc.find(']', a);
  if (a == std::string::npos || b == std::string::npos || b <= a) return 0;
  std::string arr = doc.substr(a, b - a + 1);
  int n = 0;
  for (size_t i = 0; i + 1 < arr.size(); ++i) {
    if (arr[i] == '"' && arr[i + 1] != ',' && arr[i + 1] != ']') {
      // start of string
      size_t j = i + 1;
      while (j < arr.size() && arr[j] != '"') {
        if (arr[j] == '\\') ++j;
        ++j;
      }
      if (j < arr.size()) ++n;
      i = j;
    }
  }
  return n;
}

bool ChaptersCompletedHas(const std::string& doc, const std::string& key) {
  size_t p = doc.find("\"chapters_completed\"");
  if (p == std::string::npos) return false;
  size_t a = doc.find('[', p);
  size_t b = doc.find(']', a);
  if (a == std::string::npos || b == std::string::npos) return false;
  return doc.substr(a, b - a + 1).find("\"" + key + "\"") != std::string::npos;
}

std::string BuildStateJson(const std::string& doc, const PlanChapter& assigned) {
  std::string day = LocalDateYmd();
  JsonGetStringLocal(doc, "day", &day);
  int secs = 0;
  JsonGetIntLocal(doc, "bible_seconds", &secs);
  std::string key = ChapterKey(assigned.book, assigned.chapter);
  bool done = ChaptersCompletedHas(doc, key);
  int completed = CountCompleted(doc);
  bool met = completed >= 1;
  std::string chaptersArr = done ? (std::string("[\"") + EscapeJson(key) + "\"]") : "[]";
  return std::string("{\"day\":\"") + EscapeJson(day) + "\",\"bible_minutes\":" +
         std::to_string(secs / 60.0) + ",\"bible_seconds\":" + std::to_string(secs) +
         ",\"game_bank_remaining_minutes\":0,\"game_bank_remaining_seconds\":0,"
         "\"last_page\":1,\"bookmarks\":[],\"today_chapter\":{\"book\":\"" +
         EscapeJson(assigned.book) + "\",\"chapter\":" + std::to_string(assigned.chapter) +
         ",\"key\":\"" + EscapeJson(key) + "\",\"label\":\"" + EscapeJson(assigned.book) + " " +
         std::to_string(assigned.chapter) + "\",\"done\":" + (done ? "true" : "false") +
         ",\"mode\":\"today_only\"},\"chapter_goal\":{\"done\":" + std::to_string(completed) +
         ",\"target\":1,\"met\":" + (met ? "true" : "false") + "},\"chapters_completed_today\":" +
         chaptersArr + "}";
}

PlanChapter AssignedFromDoc(const std::string& doc, const std::vector<PlanChapter>& plan) {
  std::string book;
  int ch = 1;
  if (JsonGetStringLocal(doc, "assigned_book", &book) && JsonGetIntLocal(doc, "assigned_chapter", &ch) &&
      !book.empty() && ch > 0) {
    return {book, ch};
  }
  return plan.empty() ? PlanChapter{"Genesis", 1} : plan[0];
}

int DayOfYear() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  // Approximate: month days
  static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int d = st.wDay;
  for (int m = 1; m < st.wMonth; ++m) d += mdays[m];
  // leap
  if (st.wMonth > 2) {
    int y = st.wYear;
    if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ++d;
  }
  return d;
}

const char* kLordsPrayer =
    "Our Father in heaven,\n"
    "hallowed be your name.\n"
    "Your kingdom come,\n"
    "your will be done,\n"
    "on earth as it is in heaven.\n"
    "Give us today our daily bread.\n"
    "And forgive us our debts,\n"
    "as we also have forgiven our debtors.\n"
    "And lead us not into temptation,\n"
    "but deliver us from evil.\n"
    "For yours is the kingdom and the power and the glory forever.\n"
    "Amen.";

const char* kAfternoonPrayerDefault =
    "Lord, grant me wisdom for today's work,\n"
    "health for my body, and a steady mind.\n"
    "Guide my decisions and keep my words kind.\n"
    "Amen.";

const char* kEveningPrayerDefault =
    "Father, thank You for this day.\n"
    "Forgive what was unfinished or unwise,\n"
    "and give me rest under Your care.\n"
    "\n"
    "Our Father in heaven,\n"
    "hallowed be your name.\n"
    "Your kingdom come,\n"
    "your will be done,\n"
    "on earth as it is in heaven.\n"
    "Give us today our daily bread.\n"
    "And forgive us our debts,\n"
    "as we also have forgiven our debtors.\n"
    "And lead us not into temptation,\n"
    "but deliver us from evil.\n"
    "Amen.";

void SetJsonStringField(std::string* doc, const char* key, const std::string& value) {
  if (!doc) return;
  std::string needle = std::string("\"") + key + "\"";
  std::string esc = EscapeJson(value);
  size_t p = doc->find(needle);
  if (p == std::string::npos) {
    if (doc->size() >= 2 && (*doc)[doc->size() - 1] == '}') {
      doc->insert(doc->size() - 1, std::string(",\"") + key + "\":\"" + esc + "\"");
    }
    return;
  }
  size_t colon = doc->find(':', p);
  if (colon == std::string::npos) return;
  size_t q1 = doc->find('"', colon + 1);
  if (q1 == std::string::npos) return;
  size_t q2 = q1 + 1;
  while (q2 < doc->size()) {
    if ((*doc)[q2] == '\\' && q2 + 1 < doc->size()) {
      q2 += 2;
      continue;
    }
    if ((*doc)[q2] == '"') break;
    ++q2;
  }
  if (q2 >= doc->size()) return;
  doc->replace(q1, q2 - q1 + 1, std::string("\"") + esc + "\"");
}

void SetJsonIntField(std::string* doc, const char* key, int value) {
  if (!doc) return;
  std::string needle = std::string("\"") + key + "\"";
  size_t p = doc->find(needle);
  if (p == std::string::npos) {
    if (doc->size() >= 2 && (*doc)[doc->size() - 1] == '}') {
      doc->insert(doc->size() - 1, std::string(",\"") + key + "\":" + std::to_string(value));
    }
    return;
  }
  size_t colon = doc->find(':', p);
  if (colon == std::string::npos) return;
  size_t i = colon + 1;
  while (i < doc->size() && ((*doc)[i] == ' ' || (*doc)[i] == '\t')) ++i;
  size_t j = i;
  if (j < doc->size() && (*doc)[j] == '-') ++j;
  while (j < doc->size() && isdigit((unsigned char)(*doc)[j])) ++j;
  doc->replace(i, j - i, std::to_string(value));
}

void SetJsonBoolField(std::string* doc, const char* key, bool value) {
  if (!doc) return;
  std::string needle = std::string("\"") + key + "\"";
  std::string val = value ? "true" : "false";
  size_t p = doc->find(needle);
  if (p == std::string::npos) {
    if (doc->size() >= 2 && (*doc)[doc->size() - 1] == '}') {
      doc->insert(doc->size() - 1, std::string(",\"") + key + "\":" + val);
    }
    return;
  }
  size_t colon = doc->find(':', p);
  if (colon == std::string::npos) return;
  size_t i = colon + 1;
  while (i < doc->size() && ((*doc)[i] == ' ' || (*doc)[i] == '\t')) ++i;
  size_t j = i;
  while (j < doc->size() && isalpha((unsigned char)(*doc)[j])) ++j;
  doc->replace(i, j - i, val);
}

bool LoadReaderDoc(int userId, std::string* out) {
  if (!gDb || !out) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "SELECT document_json FROM productivity_bible_reader WHERE user_id=? LIMIT 1;",
                         -1, &st, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(st, 1, userId);
  bool ok = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const char* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    if (t) {
      *out = t;
      ok = true;
    }
  }
  sqlite3_finalize(st);
  return ok;
}

bool SaveReaderDoc(int userId, const std::string& doc) {
  if (!gDb) return false;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO productivity_bible_reader(user_id, document_json, updated_at) "
                         "VALUES(?,?,?) ON CONFLICT(user_id) DO UPDATE SET "
                         "document_json=excluded.document_json, updated_at=excluded.updated_at;",
                         -1, &st, nullptr) != SQLITE_OK)
    return false;
  std::string now = IsoNow();
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, doc.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

std::string EmptyReaderDoc() {
  return "{\"morning_book\":\"\",\"morning_chapter\":0,\"afternoon_book\":\"Proverbs\","
         "\"afternoon_chapter\":0,\"evening_book\":\"Psalms\",\"evening_chapter\":0,"
         "\"morning_prayer\":\"\",\"afternoon_prayer\":\"\",\"evening_prayer\":\"\"}";
}

int FindPlanIndex(const std::vector<PlanChapter>& plan, const std::string& book, int chapter) {
  for (int i = 0; i < (int)plan.size(); ++i) {
    if (plan[i].book == book && plan[i].chapter == chapter) return i;
  }
  return -1;
}

PlanChapter NextMorningChapter(const std::vector<PlanChapter>& plan, const PlanChapter& cur) {
  if (plan.empty()) return cur;
  int idx = FindPlanIndex(plan, cur.book, cur.chapter);
  if (idx < 0) return plan[0];
  return plan[(idx + 1) % (int)plan.size()];
}

PlanChapter NextWrapped(const std::string& book, int chapter, int maxCh) {
  int n = chapter + 1;
  if (n < 1 || n > maxCh) n = 1;
  return {book, n};
}

PlanChapter AdvanceSlotChapter(const std::string& slot, const PlanChapter& cur,
                               const std::vector<PlanChapter>& plan) {
  if (slot == "morning") return NextMorningChapter(plan, cur);
  if (slot == "afternoon") {
    if (cur.book == "Proverbs" || cur.book == "proverbs") return NextWrapped("Proverbs", cur.chapter, 31);
    // Prefer staying in-book via full plan scan
    int idx = FindPlanIndex(plan, cur.book, cur.chapter);
    if (idx >= 0) {
      for (int i = 1; i < (int)plan.size(); ++i) {
        const auto& n = plan[(idx + i) % (int)plan.size()];
        if (n.book == cur.book) return n;
      }
    }
    return NextWrapped(cur.book.empty() ? "Proverbs" : cur.book, cur.chapter, 150);
  }
  // evening
  if (cur.book == "Psalms" || cur.book == "Psalm" || cur.book == "psalms")
    return NextWrapped("Psalms", cur.chapter, 150);
  int idx = FindPlanIndex(plan, cur.book, cur.chapter);
  if (idx >= 0) {
    for (int i = 1; i < (int)plan.size(); ++i) {
      const auto& n = plan[(idx + i) % (int)plan.size()];
      if (n.book == cur.book) return n;
    }
  }
  return NextWrapped(cur.book.empty() ? "Psalms" : cur.book, cur.chapter, 150);
}

void EnsureReaderSeeded(int userId, const std::vector<PlanChapter>& plan, std::string* reader) {
  if (!reader) return;
  if (reader->empty()) *reader = EmptyReaderDoc();
  std::string mb;
  int mc = 0;
  JsonGetStringLocal(*reader, "morning_book", &mb);
  JsonGetIntLocal(*reader, "morning_chapter", &mc);
  if (mb.empty() || mc <= 0) {
    PlanChapter a = plan.empty() ? PlanChapter{"Genesis", 1}
                                 : plan[(DayOfYear() - 1) % (int)plan.size()];
    SetJsonStringField(reader, "morning_book", a.book);
    SetJsonIntField(reader, "morning_chapter", a.chapter);
  }
  std::string ab;
  int ac = 0;
  JsonGetStringLocal(*reader, "afternoon_book", &ab);
  JsonGetIntLocal(*reader, "afternoon_chapter", &ac);
  if (ab.empty() || ac <= 0) {
    SetJsonStringField(reader, "afternoon_book", "Proverbs");
    SetJsonIntField(reader, "afternoon_chapter", (DayOfYear() % 31) + 1);
  }
  std::string eb;
  int ec = 0;
  JsonGetStringLocal(*reader, "evening_book", &eb);
  JsonGetIntLocal(*reader, "evening_chapter", &ec);
  if (eb.empty() || ec <= 0) {
    SetJsonStringField(reader, "evening_book", "Psalms");
    SetJsonIntField(reader, "evening_chapter", (DayOfYear() % 150) + 1);
  }
  SaveReaderDoc(userId, *reader);
}

PlanChapter ReaderSlot(const std::string& reader, const char* bookKey, const char* chKey,
                       const PlanChapter& fallback) {
  std::string b;
  int c = 0;
  if (JsonGetStringLocal(reader, bookKey, &b) && JsonGetIntLocal(reader, chKey, &c) && !b.empty() &&
      c > 0)
    return {b, c};
  return fallback;
}

std::string SlotPrayerText(const std::string& slot, const std::string& dayDoc,
                           const std::string& reader) {
  const char* dayKey = slot == "morning"     ? "morning_prayer"
                       : slot == "afternoon" ? "afternoon_prayer"
                                             : "evening_prayer";
  std::string custom;
  if (JsonGetStringLocal(dayDoc, dayKey, &custom) && !custom.empty()) return custom;
  if (JsonGetStringLocal(reader, dayKey, &custom) && !custom.empty()) return custom;
  if (slot == "morning") return kLordsPrayer;
  if (slot == "afternoon") return kAfternoonPrayerDefault;
  return kEveningPrayerDefault;
}

std::string SlotJournalTitle(const std::string& slot) {
  if (slot == "morning") return "Devotion · Morning";
  if (slot == "afternoon") return "Devotion · Afternoon";
  return "Devotion · Evening";
}

}  // namespace

void LifeEnsureTables() {
  if (!gDb) return;
  sqlite3_exec(gDb,
               "CREATE TABLE IF NOT EXISTS productivity_journal ("
               "  id INTEGER PRIMARY KEY,"
               "  user_id INTEGER NOT NULL,"
               "  entry_date TEXT NOT NULL,"
               "  title TEXT,"
               "  content TEXT NOT NULL,"
               "  created_at TEXT,"
               "  updated_at TEXT"
               ");"
               "CREATE INDEX IF NOT EXISTS ix_prod_journal_user_date "
               "  ON productivity_journal(user_id, entry_date);"
               "CREATE TABLE IF NOT EXISTS productivity_bible_day ("
               "  user_id INTEGER NOT NULL,"
               "  day TEXT NOT NULL,"
               "  document_json TEXT NOT NULL,"
               "  updated_at TEXT NOT NULL,"
               "  PRIMARY KEY (user_id, day)"
               ");"
               "CREATE TABLE IF NOT EXISTS productivity_bible_reader ("
               "  user_id INTEGER PRIMARY KEY,"
               "  document_json TEXT NOT NULL,"
               "  updated_at TEXT NOT NULL"
               ");",
               nullptr, nullptr, nullptr);
}

std::string LifeJournalSummaryJson(const std::string& dayYmd, int userId) {
  LifeEnsureTables();
  std::string day = dayYmd.empty() ? LocalDateYmd() : dayYmd;
  if (!gDb) {
    return std::string("{\"day\":\"") + day + "\",\"journal_written\":false,\"journal_entry\":null}";
  }
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT id, entry_date, title, content, updated_at FROM productivity_journal "
      "WHERE user_id=? AND entry_date=? ORDER BY id DESC LIMIT 1;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) {
    return std::string("{\"day\":\"") + day + "\",\"journal_written\":false,\"journal_entry\":null}";
  }
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, day.c_str(), -1, SQLITE_TRANSIENT);
  std::string out;
  if (sqlite3_step(st) == SQLITE_ROW) {
    long long id = sqlite3_column_int64(st, 0);
    const char* ed = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    const char* content = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
    const char* updated = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
    out = std::string("{\"day\":\"") + day + "\",\"journal_written\":true,\"journal_entry\":{"
          "\"id\":" + std::to_string(id) + ",\"entry_date\":\"" + EscapeJson(ed ? ed : day) +
          "\",\"title\":" + (title ? ("\"" + EscapeJson(title) + "\"") : "null") +
          ",\"content\":\"" + EscapeJson(content ? content : "") + "\",\"updated_at\":" +
          (updated ? ("\"" + EscapeJson(updated) + "\"") : "null") + "}}";
  } else {
    out = std::string("{\"day\":\"") + day + "\",\"journal_written\":false,\"journal_entry\":null}";
  }
  sqlite3_finalize(st);
  return out;
}

std::string LifeJournalLogJson(int limit, int userId) {
  LifeEnsureTables();
  if (limit <= 0) limit = 30;
  if (limit > 200) limit = 200;
  if (!gDb) return "[]";
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT id, entry_date, title, updated_at, content FROM productivity_journal "
      "WHERE user_id=? ORDER BY entry_date DESC, id DESC LIMIT ?;";
  if (sqlite3_prepare_v2(gDb, sql, -1, &st, nullptr) != SQLITE_OK) return "[]";
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_int(st, 2, limit);
  std::string arr = "[";
  bool first = true;
  while (sqlite3_step(st) == SQLITE_ROW) {
    long long id = sqlite3_column_int64(st, 0);
    const char* ed = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
    const char* updated = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
    const char* content = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
    std::string c = content ? content : "";
    int words = 0;
    bool in = false;
    for (char ch : c) {
      if (ch == ' ' || ch == '\n' || ch == '\t')
        in = false;
      else if (!in) {
        in = true;
        ++words;
      }
    }
    if (!first) arr += ",";
    first = false;
    arr += "{\"id\":" + std::to_string(id) + ",\"entry_date\":\"" + EscapeJson(ed ? ed : "") +
           "\",\"title\":" + (title ? ("\"" + EscapeJson(title) + "\"") : "null") +
           ",\"updated_at\":" + (updated ? ("\"" + EscapeJson(updated) + "\"") : "null") +
           ",\"content_length\":" + std::to_string((int)c.size()) + ",\"word_count\":" +
           std::to_string(words) + "}";
  }
  sqlite3_finalize(st);
  arr += "]";
  return arr;
}

bool LifeJournalUpsert(const std::string& payloadJson, int userId, std::string* outEntryJson) {
  LifeEnsureTables();
  if (!gDb) return false;
  std::string content, title, entryDate;
  if (!JsonGetStringLocal(payloadJson, "content", &content)) return false;
  JsonGetStringLocal(payloadJson, "title", &title);
  if (!JsonGetStringLocal(payloadJson, "entry_date", &entryDate) || entryDate.empty())
    entryDate = LocalDateYmd();
  std::string now = IsoNow();

  // Prefer same-day + same-title row (per-slot devotion journals).
  // If titled and missing → insert new. Untitled → update latest that day (legacy).
  sqlite3_stmt* find = nullptr;
  long long existingId = 0;
  if (!title.empty()) {
    if (sqlite3_prepare_v2(gDb,
                           "SELECT id FROM productivity_journal WHERE user_id=? AND entry_date=? "
                           "AND title=? ORDER BY id DESC LIMIT 1;",
                           -1, &find, nullptr) == SQLITE_OK) {
      sqlite3_bind_int(find, 1, userId);
      sqlite3_bind_text(find, 2, entryDate.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(find, 3, title.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(find) == SQLITE_ROW) existingId = sqlite3_column_int64(find, 0);
      sqlite3_finalize(find);
    }
  } else if (sqlite3_prepare_v2(gDb,
                                "SELECT id FROM productivity_journal WHERE user_id=? AND entry_date=? "
                                "ORDER BY id DESC LIMIT 1;",
                                -1, &find, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(find, 1, userId);
    sqlite3_bind_text(find, 2, entryDate.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(find) == SQLITE_ROW) existingId = sqlite3_column_int64(find, 0);
    sqlite3_finalize(find);
  }

  if (existingId > 0) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(gDb,
                           "UPDATE productivity_journal SET title=?, content=?, updated_at=? WHERE id=?;",
                           -1, &st, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_text(st, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, existingId);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    if (outEntryJson) {
      *outEntryJson = "{\"id\":" + std::to_string(existingId) + ",\"entry_date\":\"" +
                      EscapeJson(entryDate) + "\",\"title\":\"" + EscapeJson(title) +
                      "\",\"content\":\"" + EscapeJson(content) + "\",\"updated_at\":\"" +
                      EscapeJson(now) + "\"}";
    }
    PublishJournalMirrors(ProductivityCurrentBehaviorDir(), userId);
    return true;
  }

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(gDb,
                         "INSERT INTO productivity_journal(user_id, entry_date, title, content, "
                         "created_at, updated_at) VALUES(?,?,?,?,?,?);",
                         -1, &st, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_int(st, 1, userId);
  sqlite3_bind_text(st, 2, entryDate.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, now.c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  long long newId = sqlite3_last_insert_rowid(gDb);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(st);
    return false;
  }
  sqlite3_finalize(st);
  if (outEntryJson) {
    *outEntryJson = "{\"id\":" + std::to_string(newId) + ",\"entry_date\":\"" + EscapeJson(entryDate) +
                    "\",\"title\":\"" + EscapeJson(title) + "\",\"content\":\"" + EscapeJson(content) +
                    "\",\"updated_at\":\"" + EscapeJson(now) + "\"}";
  }
  PublishJournalMirrors(ProductivityCurrentBehaviorDir(), userId);
  return true;
}

bool PublishJournalMirrors(const std::wstring& behaviorDir, int userId) {
  if (behaviorDir.empty()) return false;
  LifeEnsureTables();
  const std::string day = LocalDateYmd();
  const std::string summary = LifeJournalSummaryJson(day, userId);
  const std::string log = LifeJournalLogJson(40, userId);
  const bool a = WriteBehaviorMirrorFile(behaviorDir, L"journal_today.json", summary);
  const bool b =
      WriteBehaviorMirrorFile(behaviorDir, L"journal_log.json",
                              std::string("{\"entries\":") + (log.empty() ? "[]" : log) + "}");
  return a && b;
}

void LifeBibleEnsureToday(int userId, const std::wstring& bibleDataDir) {
  LifeEnsureTables();
  std::string day = LocalDateYmd();
  auto plan = LoadPlan(bibleDataDir);
  std::string reader;
  if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();
  EnsureReaderSeeded(userId, plan, &reader);

  std::string doc;
  if (LoadDayRow(userId, day, &doc)) {
    // Backfill aft/eve assignment fields if older docs lack them
    std::string ab;
    int ac = 0;
    if (!JsonGetStringLocal(doc, "afternoon_book", &ab) || ab.empty() ||
        !JsonGetIntLocal(doc, "afternoon_chapter", &ac) || ac <= 0) {
      PlanChapter a =
          ReaderSlot(reader, "afternoon_book", "afternoon_chapter", {"Proverbs", (DayOfYear() % 31) + 1});
      SetJsonStringField(&doc, "afternoon_book", a.book);
      SetJsonIntField(&doc, "afternoon_chapter", a.chapter);
      SaveDayRow(userId, day, doc);
    }
    std::string eb;
    int ec = 0;
    if (!JsonGetStringLocal(doc, "evening_book", &eb) || eb.empty() ||
        !JsonGetIntLocal(doc, "evening_chapter", &ec) || ec <= 0) {
      PlanChapter e =
          ReaderSlot(reader, "evening_book", "evening_chapter", {"Psalms", (DayOfYear() % 150) + 1});
      SetJsonStringField(&doc, "evening_book", e.book);
      SetJsonIntField(&doc, "evening_chapter", e.chapter);
      SaveDayRow(userId, day, doc);
    }
    return;
  }

  PlanChapter morning =
      ReaderSlot(reader, "morning_book", "morning_chapter",
                 plan.empty() ? PlanChapter{"Genesis", 1} : plan[(DayOfYear() - 1) % (int)plan.size()]);
  PlanChapter afternoon =
      ReaderSlot(reader, "afternoon_book", "afternoon_chapter", {"Proverbs", (DayOfYear() % 31) + 1});
  PlanChapter evening =
      ReaderSlot(reader, "evening_book", "evening_chapter", {"Psalms", (DayOfYear() % 150) + 1});

  // Import legacy JSON if present (morning only)
  std::wstring legacy = bibleDataDir + L"\\day_" + std::to_wstring(userId) + L"_" +
                        std::wstring(day.begin(), day.end()) + L".json";
  std::string legacyBody = ReadFileUtf8(legacy);
  if (!legacyBody.empty()) {
    doc = legacyBody;
    std::string abook;
    int ach = 0;
    if (JsonGetStringLocal(doc, "assigned_book", &abook) &&
        JsonGetIntLocal(doc, "assigned_chapter", &ach) && !abook.empty() && ach > 0) {
      morning = {abook, ach};
    }
  } else {
    doc = EmptyDayDoc(day, morning);
  }
  SetJsonStringField(&doc, "assigned_book", morning.book);
  SetJsonIntField(&doc, "assigned_chapter", morning.chapter);
  SetJsonStringField(&doc, "afternoon_book", afternoon.book);
  SetJsonIntField(&doc, "afternoon_chapter", afternoon.chapter);
  SetJsonStringField(&doc, "evening_book", evening.book);
  SetJsonIntField(&doc, "evening_chapter", evening.chapter);
  SaveDayRow(userId, day, doc);
}

std::string LifeBibleStateJson(int userId, const std::wstring& bibleDataDir) {
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  LoadDayRow(userId, day, &doc);
  auto plan = LoadPlan(bibleDataDir);
  PlanChapter assigned = AssignedFromDoc(doc, plan);
  return BuildStateJson(doc, assigned);
}

std::string LifeBibleTodayJson(int userId, const std::wstring& bibleDataDir) {
  // Same as state — chapter body filled by Focus corpus loader
  return LifeBibleStateJson(userId, bibleDataDir);
}

bool LifeBibleTick(int userId, const std::string& book, int chapter, bool done,
                   const std::wstring& bibleDataDir, const std::wstring& behaviorDir,
                   std::string* outStateJson) {
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  auto plan = LoadPlan(bibleDataDir);
  PlanChapter assigned = AssignedFromDoc(doc, plan);
  std::string key = ChapterKey(assigned.book, assigned.chapter);
  std::string want = ChapterKey(book, chapter);
  if (want != key && !book.empty()) {
    // Only assigned chapter ticks morning goal
    if (outStateJson) *outStateJson = BuildStateJson(doc, assigned);
    return false;
  }
  // Rewrite chapters_completed
  if (done) {
    if (!ChaptersCompletedHas(doc, key)) {
      size_t p = doc.find("\"chapters_completed\"");
      if (p != std::string::npos) {
        size_t a = doc.find('[', p);
        size_t b = doc.find(']', a);
        if (a != std::string::npos && b != std::string::npos) {
          std::string arr = doc.substr(a, b - a + 1);
          if (arr == "[]")
            arr = "[\"" + EscapeJson(key) + "\"]";
          else
            arr = arr.substr(0, arr.size() - 1) + ",\"" + EscapeJson(key) + "\"]";
          doc.replace(a, b - a + 1, arr);
        }
      }
    }
  } else {
    // remove key — crude
    std::string needle = "\"" + key + "\"";
    size_t p = doc.find(needle);
    if (p != std::string::npos) {
      doc.erase(p, needle.size());
      // clean commas
      size_t cc = doc.find("\"chapters_completed\"");
      if (cc != std::string::npos) {
        size_t a = doc.find('[', cc);
        size_t b = doc.find(']', a);
        if (a != std::string::npos && b != std::string::npos) {
          std::string arr = doc.substr(a, b - a + 1);
          // collapse ,, and [, and ,]
          while (arr.find(",,") != std::string::npos) {
            size_t x = arr.find(",,");
            arr.replace(x, 2, ",");
          }
          if (arr.find("[,") == 0) arr.replace(1, 1, "");
          if (arr.size() >= 2 && arr[arr.size() - 2] == ',') arr.erase(arr.size() - 2, 1);
          if (arr == "[,]" || arr == "[, ]") arr = "[]";
          doc.replace(a, b - a + 1, arr);
        }
      }
    }
  }
  if (!SaveDayRow(userId, day, doc)) return false;
  bool met = done ? true : (CountCompleted(doc) >= 1);
  if (done) met = true;
  // Recompute from chapters_completed after rewrite
  met = CountCompleted(doc) >= 1 || (done && ChaptersCompletedHas(doc, key));
  // SoftLand goals must update or morning overlay never unlocks.
  if (!PublishBibleDone(done ? true : met, behaviorDir)) return false;
  PublishDevotionMirrorBestEffort(bibleDataDir, behaviorDir, userId);
  if (outStateJson) *outStateJson = BuildStateJson(doc, assigned);
  return true;
}

bool LifeBibleHeartbeat(int userId, const std::string& book, int chapter, bool focused,
                        const std::wstring& bibleDataDir, std::string* outStateJson) {
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  auto plan = LoadPlan(bibleDataDir);
  PlanChapter assigned = AssignedFromDoc(doc, plan);
  if (ChapterKey(book, chapter) != ChapterKey(assigned.book, assigned.chapter)) {
    if (outStateJson) *outStateJson = BuildStateJson(doc, assigned);
    return true;
  }
  if (focused) {
    int secs = 0;
    JsonGetIntLocal(doc, "bible_seconds", &secs);
    secs += 25;
    // replace bible_seconds value
    size_t p = doc.find("\"bible_seconds\"");
    if (p != std::string::npos) {
      size_t colon = doc.find(':', p);
      size_t i = colon + 1;
      while (i < doc.size() && (doc[i] == ' ' || doc[i] == '\t')) ++i;
      size_t j = i;
      while (j < doc.size() && (isdigit((unsigned char)doc[j]) || doc[j] == '-')) ++j;
      doc.replace(i, j - i, std::to_string(secs));
    }
  }
  SaveDayRow(userId, day, doc);
  if (outStateJson) *outStateJson = BuildStateJson(doc, assigned);
  return true;
}

std::string LifeBibleDevotionTodayJson(int userId, const std::wstring& bibleDataDir) {
  std::string state = LifeBibleStateJson(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  LoadDayRow(userId, day, &doc);
  std::string reader;
  if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();

  PlanChapter afternoon =
      ReaderSlot(doc, "afternoon_book", "afternoon_chapter",
                 ReaderSlot(reader, "afternoon_book", "afternoon_chapter",
                            {"Proverbs", (DayOfYear() % 31) + 1}));
  PlanChapter evening =
      ReaderSlot(doc, "evening_book", "evening_chapter",
                 ReaderSlot(reader, "evening_book", "evening_chapter",
                            {"Psalms", (DayOfYear() % 150) + 1}));

  std::string aftKey = ChapterKey(afternoon.book, afternoon.chapter);
  std::string eveKey = ChapterKey(evening.book, evening.chapter);
  bool aftDone = false;
  bool eveDone = false;
  {
    std::string flag;
    // true/false tokens
    size_t p = doc.find("\"afternoon_done\"");
    if (p != std::string::npos) {
      size_t colon = doc.find(':', p);
      if (colon != std::string::npos && doc.find("true", colon) < doc.find(',', colon)) aftDone = true;
    }
    p = doc.find("\"evening_done\"");
    if (p != std::string::npos) {
      size_t colon = doc.find(':', p);
      if (colon != std::string::npos && doc.find("true", colon) < doc.find(',', colon)) eveDone = true;
    }
  }

  std::string mNotes, aNotes, eNotes;
  JsonGetStringLocal(doc, "morning_notes", &mNotes);
  JsonGetStringLocal(doc, "afternoon_notes", &aNotes);
  JsonGetStringLocal(doc, "evening_notes", &eNotes);

  std::string mPrayer = SlotPrayerText("morning", doc, reader);
  std::string aPrayer = SlotPrayerText("afternoon", doc, reader);
  std::string ePrayer = SlotPrayerText("evening", doc, reader);

  auto extractObj = [&](const std::string& blob, const char* key) -> std::string {
    size_t p = blob.find(std::string("\"") + key + "\"");
    if (p == std::string::npos) return "null";
    size_t b = blob.find('{', p);
    if (b == std::string::npos) return "null";
    int depth = 0;
    size_t e = b;
    for (; e < blob.size(); ++e) {
      if (blob[e] == '{') ++depth;
      else if (blob[e] == '}') {
        --depth;
        if (depth == 0) {
          ++e;
          break;
        }
      }
    }
    return blob.substr(b, e - b);
  };

  return std::string("{") + "\"day\":\"" + day + "\"," +
         "\"today_chapter\":" + extractObj(state, "today_chapter") + "," +
         "\"chapter_goal\":" + extractObj(state, "chapter_goal") + "," +
         "\"morning\":{\"slot\":\"morning\",\"lords_prayer\":\"" + EscapeJson(mPrayer) +
         "\",\"prayer\":\"" + EscapeJson(mPrayer) +
         "\",\"note\":\"Read today's chapter, pray, then journal. Mark done for SoftLand bible_done.\","
         "\"notes\":\"" +
         EscapeJson(mNotes) + "\",\"journal\":\"" + EscapeJson(mNotes) + "\"}," +
         "\"afternoon\":{\"book\":\"" + EscapeJson(afternoon.book) + "\",\"chapter\":" +
         std::to_string(afternoon.chapter) + ",\"key\":\"" + EscapeJson(aftKey) + "\",\"label\":\"" +
         EscapeJson(afternoon.book) + " " + std::to_string(afternoon.chapter) +
         "\",\"slot\":\"afternoon\",\"done\":" + (aftDone ? "true" : "false") + ",\"notes\":\"" +
         EscapeJson(aNotes) + "\",\"journal\":\"" + EscapeJson(aNotes) + "\",\"prayer\":\"" +
         EscapeJson(aPrayer) + "\",\"note\":\"Afternoon reading (usually Proverbs).\"}," +
         "\"evening\":{\"book\":\"" + EscapeJson(evening.book) + "\",\"chapter\":" +
         std::to_string(evening.chapter) + ",\"key\":\"" + EscapeJson(eveKey) + "\",\"label\":\"" +
         EscapeJson(evening.book) + " " + std::to_string(evening.chapter) +
         "\",\"slot\":\"evening\",\"done\":" + (eveDone ? "true" : "false") + ",\"notes\":\"" +
         EscapeJson(eNotes) + "\",\"journal\":\"" + EscapeJson(eNotes) + "\",\"prayer\":\"" +
         EscapeJson(ePrayer) + "\",\"lords_prayer\":\"" + EscapeJson(ePrayer) +
         "\",\"note\":\"Evening reading (usually Psalms).\",\"worship_title\":\"Evening prayer\","
         "\"worship_opening\":\"" +
         EscapeJson(ePrayer) + "\",\"hymn_id\":\"\"}," +
         "\"morning_chapter\":null,\"afternoon_chapter\":null,\"evening_chapter\":null,"
         "\"hymns_catalog\":[],\"gate\":{}}";
}

bool PublishBibleDevotionMirror(const std::wstring& behaviorDir, const std::wstring& bibleDataDir,
                                int userId) {
  if (behaviorDir.empty() || bibleDataDir.empty()) return false;
  LifeEnsureTables();
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string body = LifeBibleDevotionTodayJson(userId, bibleDataDir);
  if (body.empty()) return false;
  // Stamp for FE cache / debugging (mirror is SQLite projection, not a second SoT).
  if (body.front() == '{') {
    body.insert(1, "\"updated_at\":\"" + EscapeJson(IsoNow()) + "\",");
  }
  return WriteBehaviorMirrorFile(behaviorDir, L"bible_devotion.json", body);
}

bool LifeBibleDevotionDone(int userId, const std::string& slot, bool done,
                           const std::wstring& bibleDataDir, const std::wstring& behaviorDir,
                           std::string* outJson) {
  auto plan = LoadPlan(bibleDataDir);
  if (slot == "morning") {
    LifeBibleEnsureToday(userId, bibleDataDir);
    std::string day = LocalDateYmd();
    std::string doc;
    LoadDayRow(userId, day, &doc);
    PlanChapter a = AssignedFromDoc(doc, plan);
    std::string state;
    if (!LifeBibleTick(userId, a.book, a.chapter, done, bibleDataDir, behaviorDir, &state))
      return false;
    if (done) {
      std::string reader;
      if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();
      PlanChapter next = AdvanceSlotChapter("morning", a, plan);
      SetJsonStringField(&reader, "morning_book", next.book);
      SetJsonIntField(&reader, "morning_chapter", next.chapter);
      SaveReaderDoc(userId, reader);
    }
    if (outJson) *outJson = LifeBibleDevotionTodayJson(userId, bibleDataDir);
    PublishDevotionMirrorBestEffort(bibleDataDir, behaviorDir, userId);
    return true;
  }
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  std::string flag = slot == "afternoon" ? "afternoon_done" : "evening_done";
  SetJsonBoolField(&doc, flag.c_str(), done);
  SaveDayRow(userId, day, doc);

  if (done) {
    std::string reader;
    if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();
    const char* bk = slot == "afternoon" ? "afternoon_book" : "evening_book";
    const char* ck = slot == "afternoon" ? "afternoon_chapter" : "evening_chapter";
    PlanChapter cur = ReaderSlot(doc, bk, ck, ReaderSlot(reader, bk, ck, slot == "afternoon"
                                                                             ? PlanChapter{"Proverbs", 1}
                                                                             : PlanChapter{"Psalms", 1}));
    PlanChapter next = AdvanceSlotChapter(slot, cur, plan);
    SetJsonStringField(&reader, bk, next.book);
    SetJsonIntField(&reader, ck, next.chapter);
    SaveReaderDoc(userId, reader);
  }
  if (outJson) *outJson = LifeBibleDevotionTodayJson(userId, bibleDataDir);
  PublishDevotionMirrorBestEffort(bibleDataDir, behaviorDir, userId);
  return true;
}

bool LifeBibleDevotionNotes(int userId, const std::string& slot, const std::string& notes,
                            const std::wstring& bibleDataDir, std::string* outJson) {
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  std::string field = slot == "morning"     ? "morning_notes"
                      : slot == "afternoon" ? "afternoon_notes"
                                            : "evening_notes";
  SetJsonStringField(&doc, field.c_str(), notes.substr(0, 8000));
  SaveDayRow(userId, day, doc);

  // Mirror into Focus Journal (per-slot titled entry)
  std::string title = SlotJournalTitle(slot);
  std::string payload = std::string("{\"entry_date\":\"") + day + "\",\"title\":\"" +
                        EscapeJson(title) + "\",\"content\":\"" + EscapeJson(notes.substr(0, 8000)) +
                        "\"}";
  LifeJournalUpsert(payload, userId, nullptr);

  if (outJson) *outJson = LifeBibleDevotionTodayJson(userId, bibleDataDir);
  PublishDevotionMirrorBestEffort(bibleDataDir, L"", userId);
  return true;
}

bool LifeBibleDevotionAssign(int userId, const std::string& slot, const std::string& book,
                             int chapter, const std::wstring& bibleDataDir, std::string* outJson) {
  if (book.empty() || chapter <= 0) return false;
  if (slot != "morning" && slot != "afternoon" && slot != "evening") return false;
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  std::string reader;
  if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();

  if (slot == "morning") {
    SetJsonStringField(&doc, "assigned_book", book);
    SetJsonIntField(&doc, "assigned_chapter", chapter);
    SetJsonStringField(&reader, "morning_book", book);
    SetJsonIntField(&reader, "morning_chapter", chapter);
  } else if (slot == "afternoon") {
    SetJsonStringField(&doc, "afternoon_book", book);
    SetJsonIntField(&doc, "afternoon_chapter", chapter);
    SetJsonStringField(&reader, "afternoon_book", book);
    SetJsonIntField(&reader, "afternoon_chapter", chapter);
    SetJsonBoolField(&doc, "afternoon_done", false);
  } else {
    SetJsonStringField(&doc, "evening_book", book);
    SetJsonIntField(&doc, "evening_chapter", chapter);
    SetJsonStringField(&reader, "evening_book", book);
    SetJsonIntField(&reader, "evening_chapter", chapter);
    SetJsonBoolField(&doc, "evening_done", false);
  }
  SaveDayRow(userId, day, doc);
  SaveReaderDoc(userId, reader);
  if (outJson) *outJson = LifeBibleDevotionTodayJson(userId, bibleDataDir);
  PublishDevotionMirrorBestEffort(bibleDataDir, L"", userId);
  return true;
}

bool LifeBibleDevotionStep(int userId, const std::string& slot, int delta,
                           const std::wstring& bibleDataDir, std::string* outJson) {
  if (delta == 0) return false;
  if (slot != "morning" && slot != "afternoon" && slot != "evening") return false;
  LifeBibleEnsureToday(userId, bibleDataDir);
  auto plan = LoadPlan(bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;

  PlanChapter cur;
  if (slot == "morning") {
    cur = AssignedFromDoc(doc, plan);
  } else if (slot == "afternoon") {
    cur = ReaderSlot(doc, "afternoon_book", "afternoon_chapter", {"Proverbs", 1});
  } else {
    cur = ReaderSlot(doc, "evening_book", "evening_chapter", {"Psalms", 1});
  }

  PlanChapter next = cur;
  if (delta > 0) {
    next = AdvanceSlotChapter(slot, cur, plan);
  } else if (slot == "morning") {
    int idx = FindPlanIndex(plan, cur.book, cur.chapter);
    if (idx < 0) idx = 0;
    int n = (int)plan.size();
    next = n ? plan[(idx - 1 + n) % n] : cur;
  } else if (slot == "afternoon" && (cur.book == "Proverbs" || cur.book == "proverbs")) {
    int c = cur.chapter - 1;
    if (c < 1) c = 31;
    next = {"Proverbs", c};
  } else if (slot == "evening" &&
             (cur.book == "Psalms" || cur.book == "Psalm" || cur.book == "psalms")) {
    int c = cur.chapter - 1;
    if (c < 1) c = 150;
    next = {"Psalms", c};
  } else {
    int c = cur.chapter - 1;
    if (c < 1) c = 1;
    next = {cur.book, c};
  }
  return LifeBibleDevotionAssign(userId, slot, next.book, next.chapter, bibleDataDir, outJson);
}

bool LifeBibleDevotionPrayer(int userId, const std::string& slot, const std::string& text,
                             bool reset, const std::wstring& bibleDataDir, std::string* outJson) {
  if (slot != "morning" && slot != "afternoon" && slot != "evening") return false;
  LifeBibleEnsureToday(userId, bibleDataDir);
  std::string day = LocalDateYmd();
  std::string doc;
  if (!LoadDayRow(userId, day, &doc)) return false;
  std::string reader;
  if (!LoadReaderDoc(userId, &reader)) reader = EmptyReaderDoc();

  const char* key = slot == "morning"     ? "morning_prayer"
                    : slot == "afternoon" ? "afternoon_prayer"
                                          : "evening_prayer";
  std::string body;
  if (reset) {
    body = slot == "morning"       ? kLordsPrayer
           : slot == "afternoon"   ? kAfternoonPrayerDefault
                                   : kEveningPrayerDefault;
  } else {
    body = text.substr(0, 8000);
  }
  SetJsonStringField(&doc, key, body);
  SetJsonStringField(&reader, key, body);
  SaveDayRow(userId, day, doc);
  SaveReaderDoc(userId, reader);
  if (outJson) *outJson = LifeBibleDevotionTodayJson(userId, bibleDataDir);
  PublishDevotionMirrorBestEffort(bibleDataDir, L"", userId);
  return true;
}
