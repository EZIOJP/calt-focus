#include "wearable_json.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int gFail = 0;

static void Expect(bool cond, const char* msg) {
  if (cond) return;
  std::fprintf(stderr, "FAIL %s\n", msg);
  ++gFail;
}

int main() {
  std::string err;
  Wj v;
  Expect(WjParse("{\"a\":1,\"b\":\"x\\\"y\",\"u\":\"\\u00e9\",\"n\":null}", &v, &err), "parse");
  Expect(err.empty(), "no err");
  int n = 0;
  Expect(WjGet(v, "a") && WjAsInt(*WjGet(v, "a"), &n) && n == 1, "int");
  std::string s;
  Expect(WjAsString(*WjGet(v, "b"), &s) && s == "x\"y", "escape");
  Expect(WjAsString(*WjGet(v, "u"), &s) && s == "\xC3\xA9", "unicode");
  Expect(WjGet(v, "n") && WjGet(v, "n")->type == Wj::kNull, "null");

  Wj again;
  Expect(WjParse(WjStringify(v), &again, &err), "roundtrip");

  Wj prior;
  Expect(WjParse(
             "{\"sleep\":{\"total_min\":400,\"score\":80},\"activity\":{\"steps\":10,\"zones\":{"
             "\"a\":1}},\"source\":\"old\"}",
             &prior, &err),
         "prior");
  Wj incoming;
  Expect(WjParse(
             "{\"sleep\":{\"total_min\":0,\"score\":1},\"activity\":{\"steps\":20,\"zones\":{"
             "\"b\":2}},\"keep\":null,\"source\":\"mini_program\"}",
             &incoming, &err),
         "incoming");
  WjMergeDay(&prior, incoming);
  const Wj* sleep = WjGet(prior, "sleep");
  int total = 0;
  int score = 0;
  Expect(sleep && WjAsInt(*WjGet(*sleep, "total_min"), &total) && total == 400, "sleep kept");
  Expect(WjAsInt(*WjGet(*sleep, "score"), &score) && score == 80, "score kept");
  int steps = 0;
  Expect(WjAsInt(*WjGet(*WjGet(prior, "activity"), "steps"), &steps) && steps == 20, "steps");
  Expect(WjGet(*WjGet(prior, "activity"), "zones") &&
             WjGet(*WjGet(*WjGet(prior, "activity"), "zones"), "b"),
         "zones replaced");
  Expect(!WjGet(*WjGet(*WjGet(prior, "activity"), "zones"), "a"), "zones not deep-merged");
  Expect(!WjGet(prior, "keep"), "null skipped");
  std::string source;
  Expect(WjAsString(*WjGet(prior, "source"), &source) && source == "mini_program", "source");

  Wj bad;
  Expect(!WjParse("{\"a\":1} trailing", &bad, &err), "trailing rejected");
  Expect(!WjParse("", &bad, &err), "empty rejected");

  if (gFail) {
    std::fprintf(stderr, "%d failed\n", gFail);
    return 1;
  }
  std::printf("wearable_json ok\n");
  return 0;
}
