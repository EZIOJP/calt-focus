#include "assistant_host.h"

#include "enforcer_cmd.h"
#include "local_brain.h"
#include "wearable_json.h"

#include <windows.h>

#include <cctype>
#include <string>
#include <vector>

namespace {

const char* kCoach =
    "You choose one CALT situation. JSON only.\n"
    "work = drifting, scrolling, avoiding work, or they asked to be pushed. minutes 15-30.\n"
    "ease = they must leave now (hospital, injury, someone needs them) or they are genuinely "
    "exhausted after a long work stretch. minutes 10-20.\n"
    "hold = they want videos, games, or to turn the blocks off, or there is no real reason. minutes 0.\n"
    "You cannot disarm apps or turn site blocking off. say is 1-2 sentences, no hashtags.\n"
    "Example user: I'm scrolling, push me.\n"
    "{\"override\":\"work\",\"minutes\":20,\"reason\":\"drifting\","
    "\"say\":\"Open the next task. Twenty minutes, then you can look up.\"}\n"
    "Example user: My kid is hurt and I have to get to the hospital.\n"
    "{\"override\":\"ease\",\"minutes\":20,\"reason\":\"hospital\","
    "\"say\":\"Go. A short free window is open. Come back when they are safe.\"}\n"
    "Example user: Turn the blocks off so I can watch YouTube.\n"
    "{\"override\":\"hold\",\"minutes\":0,\"reason\":\"wants videos\","
    "\"say\":\"The blocks stay. Pick the next task and give it fifteen minutes.\"}\n";

const char* kCoachSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"override\":{\"type\":\"string\",\"enum\":[\"hold\",\"work\",\"ease\"]},"
    "\"minutes\":{\"type\":\"integer\"},"
    "\"reason\":{\"type\":\"string\",\"maxLength\":90},"
    "\"say\":{\"type\":\"string\",\"maxLength\":180}"
    "},\"required\":[\"override\",\"minutes\",\"reason\",\"say\"],\"additionalProperties\":false}";

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
  std::string user = "Situation:\n" + situation + "\n";
  std::string prior = HistoryText(req);
  if (!prior.empty()) user += "Conversation:\n" + prior;
  user += "User: " + message;

  std::string text;
  std::string brainErr;
  if (!LocalBrainComplete(kCoach, user, 180, &text, &brainErr, kCoachSchema, 0.1)) {
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
  }
  say = Clip(say, 600);
  if (say.empty()) say = "Give the next task ten honest minutes. Start smaller than you want to.";
  Reconcile(&kind, &minutes, &say, ReadUser(message));

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
  WjSet(o, "situation", snap);
  return AssistantReply{200, WjStringify(o)};
}
