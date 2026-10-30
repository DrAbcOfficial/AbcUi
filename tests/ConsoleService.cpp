// T15 console-service acceptance. Links the REAL production translation
// units (src/services/console/ConsoleBuffer.cpp, ConsoleBackend.cpp); the
// dispatcher section additionally compiles the real VGUI2Extension
// dispatcher (thirdparty/VGUI2Extension/src/VGUI2ExtensionInternal.cpp, pin
// b290930) and drives it through a harness mirroring
// CGameConsoleProxy::Printf/DPrintf/Clear one-to-one (thirdparty/
// VGUI2Extension/src/GameUI.cpp, cited at each harness). A competitor plugin
// in the shape of the ABCEnchance console takeover (pre-phase SUPERCEDE)
// proves the coexistence rule: a pre SUPERCEDE never reaches later plugins
// in that phase, the post phase is a fresh dispatch, and the capture must
// still store each engine message exactly once and must never truncate the
// phase for other plugins (Result stays UNSET).
//
// What is pinned here (05 doc tier 1 + tier 2):
//   - fragment joining across calls, embedded newlines, CRLF, empty lines;
//   - the fixed byte/line caps with exact drop counters and line sequence
//     numbers (monotonic across Clear);
//   - UTF-8-safe truncation of over-long lines;
//   - Clear semantics (lines + pending dropped, generation bumps, one clear
//     per pre/post pair);
//   - producer/consumer concurrency (mutex; snapshots are consistent);
//   - shutdown: producers must be stopped first (unregister), the cache
//     clears, and later pushes are refused;
//   - the submission channel: one engine command per submit, validation
//     fail-closed, bounded history with sensitive commands excluded;
//   - read-only bounded enumeration/completion.
#include <VGUI2ExtensionInternal.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../src/services/console/ConsoleBackend.h"
#include "../src/services/console/ConsoleBuffer.h"
#include "framework/TestHarness.h"

// VGUI2ExtensionInternal.cpp forwards its GetCurrentLanguage passthrough to
// these two; the game install provides them at runtime, tests stub them.
const char *GetBaseDirectory() {
  return "";
}
const char *GetCurrentGameLanguage() {
  return "english";
}

namespace {

using services::console::ConsoleBuffer;
using services::console::ConsoleCapture;
using services::console::ConsoleLine;

// --- helpers ---------------------------------------------------------------

std::string letters(std::size_t count, char c) {
  return std::string(count, c);
}

// The production capture fed by scripted wrapper calls (section 10). The
// real wrapper keeps ONE CVGUI2Extension_String across pre and post
// (GameUI.cpp Printf body) - the pair helper mirrors that identity.
void callPrintfPair(ConsoleCapture &capture, const std::string &text) {
  CVGUI2Extension_String str;
  str.assign2(text.data(), text.size());
  capture.onPrintf(&str, str.data(), str.length(), false);
  capture.onPrintf(&str, str.data(), str.length(), true);
}

void callDPrintfPair(ConsoleCapture &capture, const std::string &text) {
  CVGUI2Extension_String str;
  str.assign2(text.data(), text.size());
  capture.onDPrintf(&str, str.data(), str.length(), false);
  capture.onDPrintf(&str, str.data(), str.length(), true);
}

void callPrintf(ConsoleCapture &capture, const std::string &text, bool post) {
  CVGUI2Extension_String str;
  str.assign2(text.data(), text.size());
  capture.onPrintf(&str, str.data(), str.length(), post);
}

// --- real-dispatcher harness (section 11) ----------------------------------
// Mirrors CGameConsoleProxy::Printf (GameUI.cpp:2143-2187), DPrintf
// (:2189-2233) and Clear (:2073-2096) wrapper bodies at pin b290930:
// pre dispatch -> real function unless Result >= SUPERCEDE -> post dispatch
// (fresh loop, Result reset, skipped only on SUPERCEDE_SKIP_PLUGINS).

class CapturingConsole final : public IVGUI2Extension_GameConsoleCallbacks {
public:
  explicit CapturingConsole(ConsoleCapture *capture) : Capture(capture) {}

  int GetAltitude() const override {
    return Altitude;
  }

  void Activate(VGUI2Extension_CallbackContext *) override {}
  void Initialize(VGUI2Extension_CallbackContext *) override {}
  void Hide(VGUI2Extension_CallbackContext *) override {
    // Irrelevant to T15; counted so "no phase truncation" is observable.
    ++HideCalls;
  }
  void Clear(VGUI2Extension_CallbackContext *ctx) override {
    ++ClearCalls;
    Capture->onClear(ctx->IsPost);
  }
  void IsConsoleVisible(VGUI2Extension_CallbackContext *) override {}
  void Printf(IVGUI2Extension_String *str,
              VGUI2Extension_CallbackContext *ctx) override {
    Capture->onPrintf(str, str->data(), str->length(), ctx->IsPost);
  }
  void DPrintf(IVGUI2Extension_String *str,
               VGUI2Extension_CallbackContext *ctx) override {
    Capture->onDPrintf(str, str->data(), str->length(), ctx->IsPost);
  }
  void SetParent(vgui::VPANEL, VGUI2Extension_CallbackContext *) override {}

  int Altitude = 0;
  int HideCalls = 0;
  int ClearCalls = 0;

private:
  ConsoleCapture *Capture;
};

// The ABCEnchance takeover shape: SUPERCEDE the pre phase (so later plugins
// in that phase never run), do nothing in post (Result left UNSET).
class SupersedingConsole final : public IVGUI2Extension_GameConsoleCallbacks {
public:
  explicit SupersedingConsole(int altitude) : Altitude(altitude) {}

  int GetAltitude() const override {
    return Altitude;
  }

  void Activate(VGUI2Extension_CallbackContext *) override {}
  void Initialize(VGUI2Extension_CallbackContext *) override {}
  void Hide(VGUI2Extension_CallbackContext *) override {}
  void Clear(VGUI2Extension_CallbackContext *ctx) override {
    ++ClearCalls;
    if (!ctx->IsPost)
      ctx->Result = VGUI2Extension_Result::SUPERCEDE;
  }
  void IsConsoleVisible(VGUI2Extension_CallbackContext *) override {}
  void Printf(IVGUI2Extension_String *,
              VGUI2Extension_CallbackContext *ctx) override {
    ++PrintfCalls;
    if (!ctx->IsPost)
      ctx->Result = VGUI2Extension_Result::SUPERCEDE;
  }
  void DPrintf(IVGUI2Extension_String *,
               VGUI2Extension_CallbackContext *ctx) override {
    ++DPrintfCalls;
    if (!ctx->IsPost)
      ctx->Result = VGUI2Extension_Result::SUPERCEDE;
  }
  void SetParent(vgui::VPANEL, VGUI2Extension_CallbackContext *) override {}

  int Altitude = 0;
  int PrintfCalls = 0;
  int DPrintfCalls = 0;
  int ClearCalls = 0;
};

void proxyConsolePrintf(int &realCalls, const std::string &text) {
  CVGUI2Extension_String str;
  str.assign2(text.data(), text.size());

  VGUI2Extension_CallbackContext CallbackContext;
  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;

  VGUI2ExtensionInternal()->GameConsole_Printf(&str, &CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE)
    ++realCalls;

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = true;
    VGUI2ExtensionInternal()->GameConsole_Printf(&str, &CallbackContext);
  }
}

void proxyConsoleClear(int &realCalls) {
  VGUI2Extension_CallbackContext CallbackContext;
  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;

  VGUI2ExtensionInternal()->GameConsole_Clear(&CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE)
    ++realCalls;

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = true;
    VGUI2ExtensionInternal()->GameConsole_Clear(&CallbackContext);
  }
}

// --- backend seams (section 12) --------------------------------------------

std::vector<std::string> RecordedCommands;
bool RecordedOk = true;

bool recordClientCmd(const char *command) {
  RecordedCommands.emplace_back(command ? command : "<null>");
  return RecordedOk;
}

const char *const kFakeCvars[] = {"sv_gravity", "cl_showfps", "sensitivity",
                                  "password", "PasswordEntry"};
const char *const kFakeCommands[] = {"say",   "connect", "disconnect",
                                     "clear", "status",  "Sensitivity"};

std::size_t listFakeCvars(const char **out, std::size_t max) {
  std::size_t count = 0;
  for (const char *name : kFakeCvars) {
    if (count >= max)
      break;
    out[count++] = name;
  }
  return count;
}

std::size_t listFakeCommands(const char **out, std::size_t max) {
  std::size_t count = 0;
  for (const char *name : kFakeCommands) {
    if (count >= max)
      break;
    out[count++] = name;
  }
  return count;
}

} // namespace

int main() {
  using services::console::ConsoleBackend;

  // 0. The frozen caps this record documents.
  static_assert(ConsoleBuffer::kMaxLines == 1024, "line cap pinned");
  static_assert(ConsoleBuffer::kMaxLineBytes == 1024, "byte cap pinned");
  static_assert(ConsoleBackend::kMaxHistory == 64, "history cap pinned");

  // 1. Fragment joining, embedded newlines, CRLF, empty lines.
  {
    ConsoleBuffer buffer;
    buffer.pushText("abc", 3, false);
    CHECK(buffer.lineCount() == 0); // nothing complete yet
    buffer.pushText("def\n", 4, false);
    buffer.pushText("x\r\ny\r", 5, false);
    buffer.pushText("\n", 1, false); // flushes the CR-terminated "y"
    buffer.pushText("\n", 1, false); // bare newline: an empty line

    std::vector<ConsoleLine> lines;
    CHECK(buffer.lineCount() == 4);
    CHECK(buffer.copySince(0, lines, 100) == 4);
    CHECK(lines[0].Text == "abcdef" && lines[0].Sequence == 1);
    CHECK(lines[1].Text == "x" && lines[1].Sequence == 2);
    CHECK(lines[2].Text == "y" && lines[2].Sequence == 3);
    CHECK(lines[3].Text.empty() && lines[3].Sequence == 4);
  }

  // 2. Multi-line call + pending fragment + UTF-8 passthrough.
  {
    ConsoleBuffer buffer;
    const std::string blob = "line one\nline two\nh\xC3\xA9llo";
    buffer.pushText(blob.data(), blob.size(), false);
    std::vector<ConsoleLine> lines;
    CHECK(buffer.copySince(0, lines, 100) == 2);
    CHECK(lines[0].Text == "line one");
    CHECK(lines[1].Text == "line two");
    buffer.pushText(" tail\n", 6, false);
    CHECK(buffer.copySince(0, lines, 100) == 3);
    CHECK(lines[2].Text == "h\xC3\xA9llo tail"); // bytes untouched
  }

  // 3. copySince windows and sequences: consumer lagging past the window
  //    gets the NEWEST maxLines (a bounded cache is a tail, not a queue).
  {
    ConsoleBuffer buffer;
    for (int i = 0; i < 10; ++i) {
      const std::string line = "l" + std::to_string(i) + "\n";
      buffer.pushText(line.data(), line.size(), false);
    }
    std::vector<ConsoleLine> lines;
    CHECK(buffer.copySince(0, lines, 3) == 3);
    CHECK(lines[0].Text == "l7" && lines[2].Text == "l9");
    CHECK(buffer.copySince(8, lines, 10) == 2); // seq 9, 10
    CHECK(lines[0].Text == "l8" && lines[1].Text == "l9");
    CHECK(buffer.latestSequence() == 10);
  }

  // 4. Clear: everything drops, pending dies, generation bumps, sequence
  //    keeps counting (never reused).
  {
    ConsoleBuffer buffer;
    buffer.pushText("gone\npending", 12, false);
    const uint64_t gen0 = buffer.generation();
    buffer.clear();
    CHECK(buffer.lineCount() == 0);
    CHECK(buffer.generation() == gen0 + 1);
    std::vector<ConsoleLine> lines;
    buffer.pushText("\n", 1, false); // would join "pending" if it survived
    CHECK(buffer.copySince(0, lines, 10) == 1);
    CHECK(lines[0].Text.empty() && lines[0].Sequence == 2);
  }

  // 5. Over-long line: hard cut at the byte cap, remainder of THAT line
  //    discarded until the newline, exact counters, Truncated flag.
  {
    ConsoleBuffer buffer;
    const std::string huge = letters(3000, 'a');
    buffer.pushText(huge.data(), huge.size(), false);
    buffer.pushText("\n", 1, false);
    buffer.pushText("next\n", 5, false);
    std::vector<ConsoleLine> lines;
    CHECK(buffer.copySince(0, lines, 10) == 2);
    CHECK(lines[0].Text.size() == ConsoleBuffer::kMaxLineBytes);
    CHECK(lines[0].Truncated);
    CHECK(lines[1].Text == "next" && !lines[1].Truncated);
    CHECK(buffer.stats().DroppedBytes == 3000 - ConsoleBuffer::kMaxLineBytes);
    CHECK(buffer.stats().DroppedLines == 0);
  }

  // 6. UTF-8 boundary: the cut never splits a multi-byte sequence, and the
  //    split-off bytes count as dropped.
  {
    ConsoleBuffer buffer;
    // 1023 ASCII bytes + 3-byte U+20AC + tail: the byte cap lands inside
    // the sequence (lead + first continuation fit, the tail does not).
    std::string blob =
        letters(1023, 'a') + "\xE2\x82\xAC" + letters(100, 'b') + "\n";
    buffer.pushText(blob.data(), blob.size(), false);
    std::vector<ConsoleLine> lines;
    CHECK(buffer.copySince(0, lines, 10) == 1);
    CHECK(lines[0].Truncated);
    CHECK(lines[0].Text.size() == 1023); // the partial sequence was cut off
    CHECK(buffer.stats().DroppedBytes == 1 + 2 + 100); // lead + tail + rest
  }

  // 7. Line cap: oldest evicted, DroppedLines counts them exactly.
  {
    ConsoleBuffer buffer;
    for (int i = 0; i < ConsoleBuffer::kMaxLines + 5; ++i) {
      const std::string line = "n" + std::to_string(i) + "\n";
      buffer.pushText(line.data(), line.size(), false);
    }
    std::vector<ConsoleLine> lines;
    CHECK(buffer.lineCount() == ConsoleBuffer::kMaxLines);
    CHECK(buffer.copySince(0, lines, 2) == 2);
    CHECK(lines[1].Text == "n" + std::to_string(ConsoleBuffer::kMaxLines + 4));
    CHECK(buffer.stats().DroppedLines == 5);
  }

  // 8. Developer flag: any dev fragment marks the assembled line.
  {
    ConsoleBuffer buffer;
    buffer.pushText("dev ", 4, true);
    buffer.pushText("line\n", 5, false);
    buffer.pushText("plain\n", 6, false);
    std::vector<ConsoleLine> lines;
    CHECK(buffer.copySince(0, lines, 10) == 2);
    CHECK(lines[0].Developer && !lines[1].Developer);
  }

  // 9. Producer/consumer concurrency: exact final bookkeeping and
  //    consistent snapshots while threads push.
  {
    ConsoleBuffer buffer;
    std::atomic<bool> stop{false};
    std::thread reader([&] {
      std::vector<ConsoleLine> lines;
      while (!stop.load()) {
        buffer.copySince(0, lines, 128);
        for (std::size_t i = 1; i < lines.size(); ++i)
          CHECK(lines[i].Sequence > lines[i - 1].Sequence);
        (void)buffer.latestSequence();
      }
    });
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
      writers.emplace_back([&buffer, t] {
        for (int i = 0; i < 500; ++i) {
          const std::string line =
              "t" + std::to_string(t) + "-" + std::to_string(i) + "\n";
          buffer.pushText(line.data(), line.size(), false);
        }
      });
    }
    for (auto &writer : writers)
      writer.join();
    stop = true;
    reader.join();
    CHECK(buffer.stats().Fragments == 2000);
    CHECK(buffer.lineCount() == ConsoleBuffer::kMaxLines);
    CHECK(buffer.stats().DroppedLines == 2000 - ConsoleBuffer::kMaxLines);
  }

  // 10. Shutdown: refused pushes, cleared cache, idempotent.
  {
    ConsoleBuffer buffer;
    buffer.pushText("a\n", 2, false);
    buffer.shutdown();
    CHECK(buffer.stopped());
    CHECK(buffer.lineCount() == 0);
    const uint64_t gen = buffer.generation();
    buffer.pushText("b\n", 2, false);
    buffer.shutdown();
    CHECK(buffer.lineCount() == 0);
    CHECK(buffer.generation() == gen); // second shutdown is a no-op
    CHECK(buffer.stats().RejectedAfterShutdown == 1);
  }

  // 11. ConsoleCapture direct: each engine message once, whatever the
  //     phase combination; Clear once per pair.
  {
    ConsoleBuffer buffer;
    ConsoleCapture capture(buffer);

    callPrintfPair(capture, "full pair\n");
    CHECK(buffer.lineCount() == 1);

    callPrintf(capture, "post only\n", true); // pre was SUPERCEDE'd upstream
    CHECK(buffer.lineCount() == 2);

    callPrintf(capture, "pre only\n", false); // post never dispatched upstream
    CHECK(buffer.lineCount() == 3);

    callDPrintfPair(capture, "dev pair\n");
    CHECK(buffer.lineCount() == 4);
    {
      std::vector<ConsoleLine> lines;
      buffer.copySince(0, lines, 10);
      CHECK(lines[3].Developer);
    }

    capture.onClear(false);
    capture.onClear(true);
    CHECK(buffer.lineCount() == 0);
    CHECK(buffer.generation() == 2); // cleared exactly once

    // Back-to-back identical messages are distinct (identity is the
    // wrapper's string object, not the text).
    callPrintfPair(capture, "same\n");
    callPrintfPair(capture, "same\n");
    CHECK(buffer.lineCount() == 2);
  }

  // 12. Real dispatcher coexistence (tier 2).
  {
    // 12a. Alone: pre captured, post echo deduplicated.
    {
      ConsoleBuffer buffer;
      ConsoleCapture capture(buffer);
      CapturingConsole plugin(&capture);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&plugin);
      int realCalls = 0;
      proxyConsolePrintf(realCalls, "hello\n");
      CHECK(realCalls == 1);
      CHECK(buffer.lineCount() == 1);
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&plugin);
    }

    // 12b. Competitor SUPERCEDE's the pre phase BEFORE us (higher
    // altitude, ABCEnchance shape): our pre never runs, the post phase is
    // a fresh loop and the capture takes the message exactly once.
    {
      ConsoleBuffer buffer;
      ConsoleCapture capture(buffer);
      SupersedingConsole competitor(10);
      CapturingConsole plugin(&capture);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&competitor);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&plugin);
      int realCalls = 0;
      proxyConsolePrintf(realCalls, "taken over\n");
      CHECK(realCalls == 0);              // SUPERCEDE skipped the original
      CHECK(competitor.PrintfCalls == 2); // pre + post both dispatched to it
      CHECK(buffer.lineCount() == 1);     // ours caught the post
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&competitor);
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&plugin);
    }

    // 12c. We run FIRST (higher altitude) and must not truncate the
    // phase: our callbacks leave Result UNSET, so the competitor's pre
    // still executes, and the post echo of our message is deduplicated.
    {
      ConsoleBuffer buffer;
      ConsoleCapture capture(buffer);
      CapturingConsole plugin(&capture);
      plugin.Altitude = 10;
      SupersedingConsole competitor(0);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&plugin);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&competitor);
      int realCalls = 0;
      proxyConsolePrintf(realCalls, "coexist\n");
      CHECK(competitor.PrintfCalls == 2); // pre still reached it
      CHECK(realCalls == 0);              // its SUPERCEDE skipped the original
      CHECK(buffer.lineCount() == 1);     // exactly once, no double
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&plugin);
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&competitor);
    }

    // 12d. Clear pair: one generation bump, and the competitor's pre
    // SUPERCEDE with us later still clears once via our post.
    {
      ConsoleBuffer buffer;
      ConsoleCapture capture(buffer);
      buffer.pushText("before\n", 7, false);
      CapturingConsole plugin(&capture);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&plugin);
      int realCalls = 0;
      proxyConsoleClear(realCalls);
      CHECK(buffer.lineCount() == 0);
      CHECK(buffer.generation() == 2);
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&plugin);

      SupersedingConsole competitor(10);
      CapturingConsole later(&capture);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&competitor);
      VGUI2ExtensionInternal()->RegisterGameConsoleCallbacks(&later);
      proxyConsoleClear(realCalls);
      CHECK(buffer.lineCount() == 0);
      CHECK(buffer.generation() == 3); // still exactly one per pair
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&competitor);
      VGUI2ExtensionInternal()->UnregisterGameConsoleCallbacks(&later);
    }
  }

  // 13. Submission channel: exactly one engine command per submit,
  //     fail-closed validation, verbatim payload.
  {
    RecordedCommands.clear();
    RecordedOk = true;
    ConsoleBackend backend;
    ConsoleBackend::Seams seams;
    seams.clientCmd = &recordClientCmd;
    seams.listCvars = &listFakeCvars;
    seams.listCommands = &listFakeCommands;
    backend.setSeams(seams);

    CHECK(backend.submit("say hello") ==
          ConsoleBackend::SubmitResult::kExecuted);
    CHECK(RecordedCommands.size() == 1);
    CHECK(RecordedCommands[0] == "say hello\n"); // one command, one newline

    CHECK(backend.submit("  status  ") ==
          ConsoleBackend::SubmitResult::kExecuted);
    CHECK(RecordedCommands.size() == 2);
    CHECK(RecordedCommands[1] == "status\n"); // trimmed ends only

    const std::size_t before = RecordedCommands.size();
    CHECK(backend.submit("") == ConsoleBackend::SubmitResult::kRejectedEmpty);
    CHECK(backend.submit("   \t ") ==
          ConsoleBackend::SubmitResult::kRejectedEmpty);
    CHECK(backend.submit("say a\nsay b") ==
          ConsoleBackend::SubmitResult::kRejectedInvalid);
    CHECK(backend.submit("say a\rsay b") ==
          ConsoleBackend::SubmitResult::kRejectedInvalid);
    CHECK(backend.submit(
              letters(ConsoleBackend::kMaxCommandBytes + 1, 'x').c_str()) ==
          ConsoleBackend::SubmitResult::kRejectedInvalid);
    CHECK(RecordedCommands.size() == before); // nothing reached the engine

    RecordedOk = false;
    CHECK(backend.submit("status") ==
          ConsoleBackend::SubmitResult::kUnavailable);
    RecordedOk = true;

    const auto counters = backend.counters();
    CHECK(counters.Executed == 2);
    CHECK(counters.RejectedEmpty == 2);
    CHECK(counters.RejectedInvalid == 3);
  }

  // 14. History: bounded ring, sensitive commands never stored (they still
  //     execute), case-insensitive classification.
  {
    RecordedCommands.clear();
    ConsoleBackend backend;
    ConsoleBackend::Seams seams;
    seams.clientCmd = &recordClientCmd;
    backend.setSeams(seams);

    CHECK(backend.submit("password hunter2") ==
          ConsoleBackend::SubmitResult::kExecuted);
    CHECK(backend.submit("PASSWORD hunter2") ==
          ConsoleBackend::SubmitResult::kExecuted);
    CHECK(backend.submit("sv_password secret") ==
          ConsoleBackend::SubmitResult::kExecuted);
    CHECK(RecordedCommands.size() == 3); // executed once each
    CHECK(backend.history().empty());    // never stored
    CHECK(backend.counters().HistorySkippedSensitive == 3);

    for (int i = 0; i < 100; ++i) {
      const std::string command = "cmd" + std::to_string(i);
      CHECK(backend.submit(command.c_str()) ==
            ConsoleBackend::SubmitResult::kExecuted);
    }
    CHECK(backend.history().size() == ConsoleBackend::kMaxHistory);
    CHECK(backend.history().front() ==
          "cmd" + std::to_string(100 - ConsoleBackend::kMaxHistory));
    CHECK(backend.history().back() == "cmd99");
  }

  // 15. Enumeration/completion: read-only, deduplicated, sorted, capped.
  {
    ConsoleBackend backend;
    ConsoleBackend::Seams seams;
    seams.listCvars = &listFakeCvars;
    seams.listCommands = &listFakeCommands;
    backend.setSeams(seams);

    const auto all = backend.complete("", 100);
    CHECK(std::is_sorted(all.begin(), all.end()));
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end()); // dedup
    // "password" appears in both lists (cvar + the fake "PasswordEntry"
    // command) yet only once per exact name; case-insensitive prefix.
    const auto pw = backend.complete("pass", 100);
    CHECK(pw.size() == 2); // "password" + "PasswordEntry"
    const auto s = backend.complete("S", 100);
    CHECK(std::find(s.begin(), s.end(), "say") != s.end());
    CHECK(std::find(s.begin(), s.end(), "sensitivity") != s.end());
    CHECK(std::find(s.begin(), s.end(), "Sensitivity") != s.end());
    CHECK(backend.complete("", 3).size() == 3); // capped
    CHECK(backend.complete("zzz", 10).empty());
  }

  std::cout << "console service: all checks passed\n";
  return abctest::finish("abc_ui_console");
}
