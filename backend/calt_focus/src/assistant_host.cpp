#include "assistant_host.h"

#include "enforcer_cmd.h"
#include "local_brain.h"
#include "wearable_json.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kCoach =
    "You are Qwen, the CALT coach. You talk to this person and you can change their day. JSON only.\n"
    "work = drifting, scrolling, avoiding work, or they asked to be pushed. minutes 15-30.\n"
    "ease = they must leave now (hospital, injury, someone needs them) or they are genuinely "
    "exhausted after a long work stretch. minutes 10-20.\n"
    "hold = words, or a plan/task/journal change with the rules left as they are. minutes 0.\n"
    "act is none, add_task, done_task, add_plan, confirm_plan, journal, start_session, end_session, "
    "or apply_routines. title is the task, plan block, or journal sentence. Use none when they only "
    "need words or when the prompt says you already did the change.\n"
    "You cannot disarm apps, turn site blocking off, or grant a day pass. say is 1-2 sentences, no hashtags.\n"
    "Example user: Add a task to finish the enforcer notes.\n"
    "{\"override\":\"hold\",\"minutes\":0,\"reason\":\"new task\",\"act\":\"add_task\","
    "\"title\":\"Finish the enforcer notes\","
    "\"say\":\"Finish the enforcer notes is on today's list. Start with that.\"}\n"
    "Example user: Plan math for 40 minutes.\n"
    "{\"override\":\"work\",\"minutes\":40,\"reason\":\"planned block\",\"act\":\"add_plan\","
    "\"title\":\"Math\",\"say\":\"Math is on the plan for forty minutes. Open it now.\"}\n"
    "Example user: I'm scrolling, push me.\n"
    "{\"override\":\"work\",\"minutes\":20,\"reason\":\"drifting\",\"act\":\"none\",\"title\":\"\","
    "\"say\":\"Open the next task. Twenty minutes, then you can look up.\"}\n";

const char* kCoachSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"override\":{\"type\":\"string\",\"enum\":[\"hold\",\"work\",\"ease\"]},"
    "\"minutes\":{\"type\":\"integer\"},"
    "\"reason\":{\"type\":\"string\",\"maxLength\":90},"
    "\"act\":{\"type\":\"string\",\"enum\":[\"none\",\"add_task\",\"done_task\",\"add_plan\","
    "\"confirm_plan\",\"journal\",\"start_session\",\"end_session\",\"apply_routines\"]},"
    "\"title\":{\"type\":\"string\",\"maxLength\":140},"
    "\"say\":{\"type\":\"string\",\"maxLength\":180}"
    "},\"required\":[\"override\",\"minutes\",\"reason\",\"act\",\"say\"],\"additionalProperties\":false}";

const char* kHelp =
    "I can talk through the day and change it. "
    "add task <name> · done <name> · plan <name> for <minutes> · journal <sentence> · "
    "confirm plan · start session · end session · apply routines. "
    "I will not turn Arm off, turn SoftLand off, or open a day pass.";

std::string TodayYmd();
std::string PlanLines(const std::string& date);

std::string LowerCopy(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool Has(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

struct SituationRead {
  bool emergency = false;
  bool exhausted = false;
  bool dodge = false;
  bool push = false;
};

SituationRead ReadUser(const std::string& message) {
  const std::string m = LowerCopy(message);
  SituationRead r;
  r.emergency = Has(m, "hospital") || Has(m, "ambulance") || Has(m, "injured") || Has(m, "injury") ||
                Has(m, "bleeding") || Has(m, "collapsed") || Has(m, "unconscious") ||
                Has(m, "emergency") || Has(m, "accident") || Has(m, " hurt") || Has(m, "i'm sick") ||
                Has(m, "i am sick") || Has(m, "feel sick");
  r.exhausted = Has(m, "exhausted") || Has(m, "burnout") || Has(m, "head is gone") ||
                Has(m, "real break") || Has(m, "need a break") || Has(m, "can't keep going") ||
                Has(m, "for hours") || Has(m, "been at this") || Has(m, "six hours") ||
                Has(m, "five hours") || Has(m, "four hours");
  r.dodge = Has(m, "youtube") || Has(m, "netflix") || Has(m, "tiktok") || Has(m, "instagram") ||
            Has(m, "twitch") || Has(m, "watch tv") || Has(m, "turn off") || Has(m, "blocks off") ||
            Has(m, "unarm") || Has(m, "disarm") || Has(m, "disable the");
  r.push = Has(m, "push me") || Has(m, "make me start") || Has(m, "make me work");
  return r;
}

void Reconcile(std::string* kind, int* minutes, std::string* say, const SituationRead& r) {
  const std::string model = *kind;
  if (r.dodge && !r.emergency) {
    *kind = "hold";
    *minutes = 0;
    if (model != "hold")
      *say = "The blocks stay. Pick the next task and give it fifteen honest minutes.";
    return;
  }
  if (r.push && !r.emergency) {
    *kind = "work";
    if (*minutes < 10 || *minutes > 45) *minutes = 20;
    return;
  }
  if (r.emergency || (r.exhausted && !r.dodge)) {
    *kind = "ease";
    if (*minutes < 5 || *minutes > 25) *minutes = 15;
    if (model == "work" &&
        (say->find("Open the") != std::string::npos || say->find("back to work") != std::string::npos)) {
      *say = "Go handle that. A short free window is open, and the work will be here when you are back.";
    }
    return;
  }
  if (*kind == "hold") *minutes = 0;
}

bool ReadFileCap(const std::wstring& path, std::string* out, size_t maxBytes) {
  out->clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || (unsigned long long)sz.QuadPart > maxBytes) {
    CloseHandle(h);
    return false;
  }
  out->resize((size_t)sz.QuadPart);
  DWORD read = 0;
  BOOL ok = TRUE;
  if (!out->empty()) ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr);
  CloseHandle(h);
  return ok && read == out->size();
}

std::string ChildStr(const Wj& obj, const char* key) {
  const Wj* v = WjGet(obj, key);
  std::string s;
  if (v && WjAsString(*v, &s)) return s;
  return "";
}

bool ChildBool(const Wj& obj, const char* key, bool fallback) {
  const Wj* v = WjGet(obj, key);
  if (!v || v->type != Wj::kBool) return fallback;
  return v->b;
}

int ChildInt(const Wj& obj, const char* key) {
  int n = 0;
  const Wj* v = WjGet(obj, key);
  if (v && WjAsInt(*v, &n)) return n;
  return 0;
}

std::string Clip(std::string s, size_t n) {
  if (s.size() > n) s.resize(n);
  return s;
}

bool Ask(const char* op, Wj* out) {
  std::string resp;
  const std::string req = std::string("{\"op\":\"") + op + "\",\"id\":\"coach\",\"v\":1,\"payload\":{}}";
  if (!EnforcerSendCommand(req, resp, 4000)) return false;
  std::string err;
  return WjParse(resp, out, &err) && out->type == Wj::kObj && ChildBool(*out, "ok", false);
}

std::string TaskLine(const Wj& tasks) {
  if (tasks.type != Wj::kArr) return "";
  std::string line;
  int n = 0;
  for (const Wj& task : tasks.arr) {
    if (task.type != Wj::kObj) continue;
    if (ChildBool(task, "done", false)) continue;
    std::string title = ChildStr(task, "title");
    if (title.empty()) title = ChildStr(task, "name");
    title = Clip(title, 80);
    if (title.empty()) continue;
    if (!line.empty()) line += "; ";
    line += title;
    if (++n >= 6) break;
  }
  return line;
}

Wj Situation(const std::wstring& behaviorDir, std::string* forModel) {
  Wj loopAsk;
  Wj dayAsk;
  const bool loopOk = Ask("day.loop_snapshot", &loopAsk);
  const bool dayOk = Ask("day.status", &dayAsk);
  const Wj* loop = loopOk ? WjGet(loopAsk, "loop") : nullptr;
  if (loop && loop->type != Wj::kObj) loop = nullptr;

  bool armed = false;
  bool armedKnown = false;
  std::string policy;
  std::string err;
  Wj pol;
  if (ReadFileCap(behaviorDir + L"\\enforcer_policy.json", &policy, 64 * 1024) &&
      WjParse(policy, &pol, &err) && pol.type == Wj::kObj && WjGet(pol, "hard_block_armed") &&
      WjGet(pol, "hard_block_armed")->type == Wj::kBool) {
    armed = WjGet(pol, "hard_block_armed")->b;
    armedKnown = true;
  }

  auto fieldStr = [&](const char* key) -> std::string {
    if (loop) {
      std::string s = ChildStr(*loop, key);
      if (!s.empty()) return s;
    }
    if (dayOk) return ChildStr(dayAsk, key);
    return "";
  };
  auto fieldBool = [&](const char* key, bool fb) -> bool {
    if (loop && WjGet(*loop, key)) return ChildBool(*loop, key, fb);
    if (dayOk && WjGet(dayAsk, key)) return ChildBool(dayAsk, key, fb);
    return fb;
  };

  const bool site = fieldBool("softland_enabled", false);
  const bool plan = fieldBool("plan_confirmed", false);
  const bool bible = fieldBool("bible_done", false);
  const bool reward = dayOk ? ChildBool(dayAsk, "reward_day_active", false) : false;
  const int planned = loop ? ChildInt(*loop, "planned_minutes") : 0;
  const int tracked = loop ? ChildInt(*loop, "tracked_productive_minutes") : 0;
  const int tasksDone = loop ? ChildInt(*loop, "tasks_done") : 0;
  const int tasksTotal = loop ? ChildInt(*loop, "tasks_total") : 0;
  const std::string freeUntil = fieldStr("free_until");
  const std::string incub = dayOk ? ChildStr(dayAsk, "incubation_until") : "";
  const std::string bed = loop ? ChildStr(*loop, "bedtime_hm") : "";
  const bool bedOn = loop ? ChildBool(*loop, "bedtime_active", false) : false;
  const std::string date = loop ? ChildStr(*loop, "date") : "";
  const Wj* taskArr = loop ? WjGet(*loop, "tasks") : nullptr;
  const std::string openTasks = (taskArr && taskArr->type == Wj::kArr) ? TaskLine(*taskArr) : "";

  Wj tasksOut;
  tasksOut.type = Wj::kArr;
  if (loop) {
    if (const Wj* tasks = WjGet(*loop, "tasks")) {
      if (tasks->type == Wj::kArr) {
        for (const Wj& task : tasks->arr) {
          if (tasksOut.arr.size() >= 6 || task.type != Wj::kObj) continue;
          if (ChildBool(task, "done", false)) continue;
          std::string title = ChildStr(task, "title");
          if (title.empty()) title = ChildStr(task, "name");
          if (!title.empty()) tasksOut.arr.push_back(WjStr(Clip(title, 80)));
        }
      }
    }
  }

  std::string summary;
  if (!loopOk && !dayOk) {
    summary = "The enforcer is not reachable, so the day picture is missing.";
  } else {
    summary = site ? "Site block is on. " : "Site block is off. ";
    if (armedKnown) summary += armed ? "Apps are armed. " : "Apps are not armed. ";
    if (!incub.empty()) summary += "Study pressure is on until " + incub + ". ";
    else if (!freeUntil.empty()) summary += "A free window runs until " + freeUntil + ". ";
    else if (reward) summary += "A reward day is open. ";
    summary += "Plan " + std::string(plan ? "confirmed" : "not confirmed") + ", Bible " +
               (bible ? "done" : "not done") + ". ";
    summary += std::to_string(tracked) + " of " + std::to_string(planned) + " planned minutes, " +
               std::to_string(tasksDone) + "/" + std::to_string(tasksTotal) + " tasks.";
  }

  if (forModel) {
    *forModel = "Date: " + (date.empty() ? "unknown" : date) + "\n" + summary + "\n";
    if (!openTasks.empty()) *forModel += "Open tasks: " + openTasks + "\n";
    const std::string day = date.empty() ? TodayYmd() : date;
    const std::string planLine = PlanLines(day);
    if (!planLine.empty()) *forModel += "Plan blocks: " + planLine + "\n";
    if (!bed.empty()) *forModel += std::string("Bedtime ") + bed + (bedOn ? " active" : "") + "\n";
  }

  Wj out;
  out.type = Wj::kObj;
  WjSet(out, "ok", WjBool(true));
  WjSet(out, "enforcer_ok", WjBool(loopOk || dayOk));
  WjSet(out, "armed", armedKnown ? WjBool(armed) : WjNull());
  WjSet(out, "site_block", WjBool(site));
  WjSet(out, "free_until", freeUntil.empty() ? WjNull() : WjStr(freeUntil));
  WjSet(out, "incubation_until", incub.empty() ? WjNull() : WjStr(incub));
  WjSet(out, "reward_day", WjBool(reward));
  WjSet(out, "plan_confirmed", WjBool(plan));
  WjSet(out, "bible_done", WjBool(bible));
  WjSet(out, "planned_minutes", WjInt(planned));
  WjSet(out, "tracked_minutes", WjInt(tracked));
  WjSet(out, "tasks_done", WjInt(tasksDone));
  WjSet(out, "tasks_total", WjInt(tasksTotal));
  WjSet(out, "tasks", std::move(tasksOut));
  WjSet(out, "bedtime_hm", bed.empty() ? WjNull() : WjStr(bed));
  WjSet(out, "summary", WjStr(summary));
  return out;
}

std::string HistoryText(const Wj& req) {
  const Wj* hist = WjGet(req, "history");
  if (!hist || hist->type != Wj::kArr) return "";
  std::string out;
  size_t start = hist->arr.size() > 6 ? hist->arr.size() - 6 : 0;
  for (size_t i = start; i < hist->arr.size(); ++i) {
    const Wj& turn = hist->arr[i];
    if (turn.type != Wj::kObj) continue;
    std::string role = ChildStr(turn, "role");
    std::string text = Clip(ChildStr(turn, "text"), 400);
    if (text.empty()) continue;
    if (role != "coach") role = "User";
    else role = "Coach";
    out += role + ": " + text + "\n";
  }
  return out;
}

bool ApplyOverride(const std::string& kind, int* minutes, const std::string& reason, std::string* until,
                   std::string* err) {
  until->clear();
  Wj payload;
  payload.type = Wj::kObj;
  WjSet(payload, "kind", WjStr(kind));
  WjSet(payload, "minutes", WjInt(*minutes));
  WjSet(payload, "reason", WjStr(Clip(reason, 160)));
  const std::string req = std::string("{\"op\":\"assistant.override\",\"id\":\"coach\",\"v\":1,\"payload\":") +
                           WjStringify(payload) + "}";
  std::string resp;
  if (!EnforcerSendCommand(req, resp, 4000)) {
    *err = "enforcer_unreachable";
    return false;
  }
  Wj doc;
  std::string perr;
  if (!WjParse(resp, &doc, &perr) || !ChildBool(doc, "ok", false)) {
    *err = ChildStr(doc, "error");
    if (err->empty()) *err = "override_failed";
    return false;
  }
  *until = ChildStr(doc, "assistant_until");
  const int appliedMin = ChildInt(doc, "assistant_minutes");
  if (appliedMin > 0) *minutes = appliedMin;
  return true;
}

std::string TodayYmd() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::string NowLocalIso() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond);
  return buf;
}

bool StartsWith(const std::string& text, const char* prefix) {
  const size_t n = std::strlen(prefix);
  if (text.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower((unsigned char)text[i]) != prefix[i]) return false;
  }
  return true;
}

std::string After(const std::string& text, size_t n) {
  while (n < text.size() && text[n] == ' ') ++n;
  std::string s = text.substr(n);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return Clip(s, 160);
}

bool AskOp(const std::string& op, const std::string& payload, Wj* out, std::string* err) {
  const std::string req =
      std::string("{\"op\":\"") + op + "\",\"id\":\"coach\",\"v\":1,\"payload\":" + payload + "}";
  std::string resp;
  if (!EnforcerSendCommand(req, resp, 4000)) {
    *err = "enforcer_unreachable";
    return false;
  }
  std::string perr;
  if (!WjParse(resp, out, &perr) || !ChildBool(*out, "ok", false)) {
    *err = ChildStr(*out, "error");
    if (err->empty()) *err = "failed";
    return false;
  }
  return true;
}

struct TaskRef {
  int id = 0;
  std::string title;
  bool done = false;
};

std::vector<TaskRef> LoadTasks(const std::string& date) {
  std::vector<TaskRef> out;
  Wj doc;
  std::string err;
  const std::string payload = std::string("{\"date\":\"") + date + "\"}";
  if (!AskOp("day.task_list", payload, &doc, &err)) return out;
  const Wj* tasks = WjGet(doc, "tasks");
  if (!tasks || tasks->type != Wj::kArr) return out;
  for (const Wj& task : tasks->arr) {
    if (task.type != Wj::kObj) continue;
    TaskRef ref;
    ref.id = ChildInt(task, "id");
    ref.title = ChildStr(task, "title");
    ref.done = ChildBool(task, "done", false);
    if (ref.id > 0 && !ref.title.empty()) out.push_back(std::move(ref));
  }
  return out;
}

std::string PlanLines(const std::string& date) {
  Wj doc;
  std::string err;
  const std::string payload =
      std::string("{\"from\":\"") + date + "T00:00:00\",\"to\":\"" + date + "T23:59:59\"}";
  if (!AskOp("plan.list", payload, &doc, &err)) return "";
  const Wj* blocks = WjGet(doc, "blocks");
  if (!blocks || blocks->type != Wj::kArr) return "";
  std::string line;
  int n = 0;
  for (const Wj& block : blocks->arr) {
    if (block.type != Wj::kObj || n >= 6) continue;
    std::string title = ChildStr(block, "title");
    if (title.empty()) continue;
    std::string start = ChildStr(block, "start_at");
    if (start.size() >= 16) start = start.substr(11, 5);
    if (!line.empty()) line += "; ";
    line += start + " " + Clip(title, 60) + " " + std::to_string(ChildInt(block, "planned_minutes")) + "m";
    ++n;
  }
  return line;
}

int FindTask(const std::vector<TaskRef>& tasks, const std::string& title) {
  const std::string want = LowerCopy(title);
  int fallback = 0;
  for (const TaskRef& task : tasks) {
    const std::string have = LowerCopy(task.title);
    if (have == want) return task.id;
    if (!fallback && have.find(want) != std::string::npos) fallback = task.id;
  }
  return fallback;
}

void StripPlanTail(std::string* title, int* minutes, std::string* startIso, const std::string& date) {
  if (*minutes < 10 || *minutes > 180) *minutes = 25;
  *startIso = NowLocalIso();
  std::string lower = LowerCopy(*title);
  auto cut = [&](size_t at) {
    *title = After(*title, 0);
    if (at < title->size()) title->resize(at);
    while (!title->empty() && (title->back() == ' ' || title->back() == ',')) title->pop_back();
  };
  const size_t forAt = lower.rfind(" for ");
  if (forAt != std::string::npos) {
    int n = 0;
    const char* p = lower.c_str() + forAt + 5;
    if (std::strncmp(p, "an hour", 7) == 0 || std::strncmp(p, "one hour", 8) == 0) n = 60;
    else n = std::atoi(p);
    if (lower.find("hour", forAt) != std::string::npos && n > 0 && n <= 6) n *= 60;
    if (n >= 10 && n <= 180) *minutes = n;
    cut(forAt);
    lower = LowerCopy(*title);
  }
  const size_t at = lower.rfind(" at ");
  if (at != std::string::npos) {
    const char* p = lower.c_str() + at + 4;
    int h = std::atoi(p);
    int m = 0;
    const char* colon = std::strchr(p, ':');
    if (colon && colon < p + 6) m = std::atoi(colon + 1);
    const bool pm = std::strstr(p, "pm") != nullptr;
    const bool am = std::strstr(p, "am") != nullptr;
    if (pm && h < 12) h += 12;
    if (am && h == 12) h = 0;
    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%sT%02d:%02d:00", date.c_str(), h, m);
      *startIso = buf;
    }
    cut(at);
  }
}

struct DoneAction {
  std::string kind;
  std::string title;
  bool ok = false;
  std::string detail;
};

DoneAction RunAction(const std::string& kind, const std::string& title, int minutes, const std::string& date) {
  DoneAction done;
  done.kind = kind;
  done.title = title;
  Wj doc;
  std::string err;
  if (kind == "add_task") {
    if (title.empty()) {
      done.detail = "Say the task name.";
      return done;
    }
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "title", WjStr(title));
    WjSet(payload, "task_date", WjStr(date));
    WjSet(payload, "done", WjBool(false));
    done.ok = AskOp("day.task_upsert", WjStringify(payload), &doc, &err);
    done.detail = done.ok ? "Added task " + title + "." : err;
  } else if (kind == "done_task") {
    const int id = FindTask(LoadTasks(date), title);
    if (id <= 0) {
      done.detail = "I couldn't find that task.";
      return done;
    }
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "id", WjInt(id));
    WjSet(payload, "done", WjBool(true));
    done.ok = AskOp("day.task_set_done", WjStringify(payload), &doc, &err);
    done.detail = done.ok ? "Marked that task done." : err;
  } else if (kind == "add_plan") {
    if (title.empty()) {
      done.detail = "Say what the block is for.";
      return done;
    }
    int mins = minutes;
    std::string start;
    std::string name = title;
    StripPlanTail(&name, &mins, &start, date);
    if (name.empty()) name = title;
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "title", WjStr(name));
    WjSet(payload, "category", WjStr("study"));
    WjSet(payload, "start_at", WjStr(start));
    WjSet(payload, "duration_minutes", WjInt(mins));
    done.ok = AskOp("plan.upsert", WjStringify(payload), &doc, &err);
    done.title = name;
    done.detail = done.ok ? "Planned " + name + " for " + std::to_string(mins) + " minutes." : err;
  } else if (kind == "journal") {
    if (title.empty()) {
      done.detail = "Say the journal line.";
      return done;
    }
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "content", WjStr(title));
    WjSet(payload, "day", WjStr(date));
    done.ok = AskOp("journal.upsert", WjStringify(payload), &doc, &err);
    done.detail = done.ok ? "Wrote that in the journal." : err;
  } else if (kind == "confirm_plan") {
    done.ok = AskOp("day.confirm_plan", "{}", &doc, &err);
    done.detail = done.ok ? "Plan confirmed." : err;
  } else if (kind == "start_session" || kind == "end_session") {
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "active", WjBool(kind == "start_session"));
    done.ok = AskOp("session.set", WjStringify(payload), &doc, &err);
    done.detail = done.ok ? (kind == "start_session" ? "Work session started." : "Work session ended.") : err;
  } else if (kind == "apply_routines") {
    Wj payload;
    payload.type = Wj::kObj;
    WjSet(payload, "date", WjStr(date));
    WjSet(payload, "skip_overlaps", WjBool(true));
    done.ok = AskOp("routine.apply", WjStringify(payload), &doc, &err);
    done.detail = done.ok ? "Applied today's routines." : err;
  } else {
    done.detail = "Nothing to change.";
  }
  return done;
}

DoneAction ParseDirect(const std::string& message) {
  DoneAction none;
  const std::string m = message;
  auto take = [&](const char* prefix, const char* kind) -> DoneAction {
    DoneAction a;
    a.kind = kind;
    a.title = After(m, std::strlen(prefix));
    return a;
  };
  const std::string low = LowerCopy(m);
  if (low == "help" || low == "?" || low == "/help") {
    none.kind = "help";
    return none;
  }
  if (StartsWith(m, "softland") || StartsWith(m, "sl ") || low == "pass" || low == "daypass" ||
      StartsWith(m, "unarm") || StartsWith(m, "disarm")) {
    none.kind = "refused";
    none.detail =
        "Arm, SoftLand, and the day pass stay in your hands. I can change the plan, tasks, journal, and the work session.";
    return none;
  }
  auto early = [&](const char* phrase, const char* kind) -> DoneAction {
    const std::string low = LowerCopy(m);
    const std::string needle = phrase;
    size_t at = low.find(needle);
    DoneAction miss;
    if (at == std::string::npos || at > 24) return miss;
    DoneAction a;
    a.kind = kind;
    a.title = After(m, at + needle.size());
    return a;
  };
  if (DoneAction a = early("add a task to ", "add_task"); !a.kind.empty()) return a;
  if (DoneAction a = early("add a task called ", "add_task"); !a.kind.empty()) return a;
  if (DoneAction a = early("add a task ", "add_task"); !a.kind.empty()) return a;
  if (DoneAction a = early("write in the journal that ", "journal"); !a.kind.empty()) return a;
  if (DoneAction a = early("write in the journal ", "journal"); !a.kind.empty()) return a;
  if (DoneAction a = early("write in my journal ", "journal"); !a.kind.empty()) return a;
  if (StartsWith(m, "add task ")) return take("add task ", "add_task");
  if (StartsWith(m, "add todo ")) return take("add todo ", "add_task");
  if (StartsWith(m, "new task ")) return take("new task ", "add_task");
  if (StartsWith(m, "todo ")) return take("todo ", "add_task");
  if (StartsWith(m, "mark ")) {
    DoneAction a = take("mark ", "done_task");
    const std::string low = LowerCopy(a.title);
    const size_t cut = low.rfind(" done");
    if (cut != std::string::npos) a.title = After(a.title.substr(0, cut), 0);
    return a;
  }
  if (StartsWith(m, "done ")) return take("done ", "done_task");
  if (StartsWith(m, "finish ")) return take("finish ", "done_task");
  if (StartsWith(m, "plan ")) return take("plan ", "add_plan");
  if (StartsWith(m, "schedule ")) return take("schedule ", "add_plan");
  if (StartsWith(m, "journal ")) return take("journal ", "journal");
  if (StartsWith(m, "note ")) return take("note ", "journal");
  if (StartsWith(m, "confirm plan") || StartsWith(m, "confirm my plan")) {
    none.kind = "confirm_plan";
    return none;
  }
  if (StartsWith(m, "start session") || StartsWith(m, "start a work session") ||
      StartsWith(m, "start work session")) {
    none.kind = "start_session";
    return none;
  }
  if (StartsWith(m, "end session") || StartsWith(m, "end the session") ||
      StartsWith(m, "session end")) {
    none.kind = "end_session";
    return none;
  }
  if (StartsWith(m, "apply routines") || StartsWith(m, "apply my routines")) {
    none.kind = "apply_routines";
    return none;
  }
  return none;
}

}  // namespace

AssistantReply AssistantHandle(const std::wstring& behaviorDir, const std::string& method,
                               const std::string& path, const std::string& body) {
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  if (method == "GET" && (p == "/api/assistant" || p == "/api/assistant/situation")) {
    std::string modelText;
    return AssistantReply{200, WjStringify(Situation(behaviorDir, &modelText))};
  }
  if (!(method == "POST" && p == "/api/assistant/talk")) {
    return AssistantReply{404, "{\"ok\":false,\"detail\":\"unknown assistant route\"}"};
  }
  Wj req;
  std::string perr;
  if (!WjParse(body, &req, &perr) || req.type != Wj::kObj) {
    return AssistantReply{400, "{\"ok\":false,\"detail\":\"invalid JSON\"}"};
  }
  std::string message = Clip(ChildStr(req, "message"), 2000);
  if (message.empty()) return AssistantReply{400, "{\"ok\":false,\"detail\":\"message required\"}"};

  std::string situation;
  Wj snap = Situation(behaviorDir, &situation);
  const std::string date = TodayYmd();
  DoneAction direct = ParseDirect(message);
  DoneAction ran;
  if (direct.kind == "help") {
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(true));
    WjSet(o, "say", WjStr(kHelp));
    WjSet(o, "override", WjStr("hold"));
    WjSet(o, "applied", WjBool(true));
    WjSet(o, "situation", snap);
    return AssistantReply{200, WjStringify(o)};
  }
  if (direct.kind == "refused") {
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(true));
    WjSet(o, "say", WjStr(direct.detail));
    WjSet(o, "override", WjStr("hold"));
    WjSet(o, "applied", WjBool(false));
    WjSet(o, "situation", snap);
    return AssistantReply{200, WjStringify(o)};
  }
  if (!direct.kind.empty()) ran = RunAction(direct.kind, direct.title, 0, date);

  std::string user = "Situation:\n" + situation + "\n";
  std::string prior = HistoryText(req);
  if (!prior.empty()) user += "Conversation:\n" + prior;
  if (ran.kind.empty()) user += "User: " + message;
  else user += "You already did this: " + ran.detail + "\nSet act to none.\nUser: " + message;

  std::string text;
  std::string brainErr;
  if (!LocalBrainComplete(kCoach, user, 220, &text, &brainErr, kCoachSchema, 0.1)) {
    if (!ran.kind.empty()) {
      Wj o;
      o.type = Wj::kObj;
      WjSet(o, "ok", WjBool(true));
      WjSet(o, "say", WjStr(ran.detail));
      WjSet(o, "override", WjStr("hold"));
      WjSet(o, "applied", WjBool(ran.ok));
      Wj acts;
      acts.type = Wj::kArr;
      Wj one;
      one.type = Wj::kObj;
      WjSet(one, "op", WjStr(ran.kind));
      WjSet(one, "ok", WjBool(ran.ok));
      WjSet(one, "detail", WjStr(ran.detail));
      acts.arr.push_back(std::move(one));
      WjSet(o, "actions", std::move(acts));
      WjSet(o, "situation", snap);
      return AssistantReply{200, WjStringify(o)};
    }
    Wj o;
    o.type = Wj::kObj;
    WjSet(o, "ok", WjBool(false));
    WjSet(o, "ready", WjBool(false));
    WjSet(o, "detail", WjStr(brainErr.empty() ? "Coach is not loaded" : brainErr));
    WjSet(o, "situation", snap);
    return AssistantReply{503, WjStringify(o)};
  }

  std::string say = text;
  std::string kind = "hold";
  int minutes = 0;
  std::string reason;
  const size_t a = text.find('{');
  const size_t b = text.rfind('}');
  Wj decision;
  if (a != std::string::npos && b != std::string::npos && b >= a &&
      WjParse(text.substr(a, b - a + 1), &decision, &perr) && decision.type == Wj::kObj) {
    std::string fromModel = ChildStr(decision, "say");
    if (!fromModel.empty()) say = fromModel;
    std::string overrideKind = ChildStr(decision, "override");
    if (overrideKind == "work" || overrideKind == "ease" || overrideKind == "hold") kind = overrideKind;
    minutes = ChildInt(decision, "minutes");
    reason = ChildStr(decision, "reason");
    if (ran.kind.empty()) {
      std::string act = ChildStr(decision, "act");
      if (act != "none" && !act.empty()) ran = RunAction(act, Clip(ChildStr(decision, "title"), 140), minutes, date);
    }
  }
  say = Clip(say, 600);
  if (say.empty()) say = ran.detail.empty() ? "Give the next task ten honest minutes. Start smaller than you want to."
                                            : ran.detail;
  const std::string said = say;
  Reconcile(&kind, &minutes, &say, ReadUser(message));
  if (ran.ok && say != said) say = Clip(ran.detail + " " + say, 600);

  bool applied = kind == "hold";
  std::string until;
  std::string applyErr;
  if (kind == "work" || kind == "ease") {
    applied = ApplyOverride(kind, &minutes, reason.empty() ? message : reason, &until, &applyErr);
  }

  Wj o;
  o.type = Wj::kObj;
  WjSet(o, "ok", WjBool(true));
  WjSet(o, "say", WjStr(say));
  WjSet(o, "override", WjStr(kind));
  WjSet(o, "minutes", WjInt(minutes));
  WjSet(o, "reason", WjStr(Clip(reason, 160)));
  WjSet(o, "applied", WjBool(applied));
  WjSet(o, "until", until.empty() ? WjNull() : WjStr(until));
  WjSet(o, "error", applyErr.empty() ? WjNull() : WjStr(applyErr));
  if (!ran.kind.empty()) {
    Wj acts;
    acts.type = Wj::kArr;
    Wj one;
    one.type = Wj::kObj;
    WjSet(one, "op", WjStr(ran.kind));
    WjSet(one, "ok", WjBool(ran.ok));
    WjSet(one, "detail", WjStr(ran.detail));
    acts.arr.push_back(std::move(one));
    WjSet(o, "actions", std::move(acts));
    if (!ran.ok && applyErr.empty()) WjSet(o, "error", WjStr(ran.detail));
  }
  WjSet(o, "situation", snap);
  return AssistantReply{200, WjStringify(o)};
}
