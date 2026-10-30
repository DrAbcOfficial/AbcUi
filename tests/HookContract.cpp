// T02.a hook-contract tests. Drives the real VGUI2Extension dispatcher
// (thirdparty/VGUI2Extension/src/VGUI2ExtensionInternal.cpp, submodule pin
// b290930) through proxy harnesses that mirror the shipped IBaseUI/IGameUI
// wrapper bodies one-to-one. Each harness names its origin file and line
// range; if the extension proxy changes, re-diff the harness bodies against
// src/BaseUI.cpp / src/GameUI.cpp before trusting a PASS.
//
// What is proven here (static, in-process):
//   - int and bool return values travel through the proxy-owned fake_ret
//     storage and back out with the right type and value;
//   - a pre-phase SUPERCEDE skips the original but still dispatches post,
//     and the post phase starts with Result reset to UNSET - a pre SUPERCEDE
//     that does not re-assert in post silently falls back to real_ret;
//   - HANDLED aborts the remaining plugins of the current phase only, the
//     original still runs, and the post phase is a fresh loop;
//   - a plugin guarding its side effects with !IsPost executes them exactly
//     once across the pre+post pair (ActivateGameUI shape);
//   - ActivateDemoUI ignores SUPERCEDE_SKIP_PLUGINS for the post phase;
//   - HideGameUI sets IsPost=false in its post phase at this pin, so IsPost
//     is not a reliable phase discriminator there (documented trap);
//   - higher altitude is called first; equal-altitude plugins are all called;
//   - Key_Event reference parameters are mutable: a pre-phase rewrite of
//     keynum reaches the original function.
#include <VGUI2ExtensionInternal.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
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

struct CallLog {
  std::vector<std::string> entries;

  void add(const char *plugin, const std::string &phase) {
    entries.push_back(std::string(plugin) + ":" + phase);
  }
  int count(const char *plugin, const std::string &phase) const {
    const std::string key = std::string(plugin) + ":" + phase;
    return (int)std::count(entries.begin(), entries.end(), key);
  }
};

// Fake plugin whose pre/post behavior is scripted per test. The only legal
// way for a plugin to answer a return-value callback is writing through
// CallbackContext.pPluginReturnValue - the proxy reads its own storage.
// actWhenNotPost=true models the required side-effect guard: the action runs
// in the pre phase only, so a pre+post pair must yield exactly one action.
struct ScriptedGameUI final : public IVGUI2Extension_GameUICallbacks {
  int altitude = 0;
  const char *name = "?";
  CallLog *log = nullptr;

  bool preSupercede = false;   // set SUPERCEDE in pre
  bool preHandled = false;     // set HANDLED in pre
  bool preSkipPlugins = false; // set SUPERCEDE_SKIP_PLUGINS in pre
  bool preWritesInt = false;   // write preIntValue in pre
  int preIntValue = 0;
  bool preWritesBool = false; // write preBoolValue in pre
  bool preBoolValue = false;
  bool actWhenNotPost = false; // false: unguarded sample; true: !IsPost guard
  int actionCount = 0;
  bool postOverride = false;  // set OVERRIDE in post
  bool postSupercede = false; // set SUPERCEDE in post
  bool postWritesInt = false; // write postIntValue in post
  int postIntValue = 0;
  bool postWritesBool = false; // write postBoolValue in post
  bool postBoolValue = false;
  bool postSawResultReset = true; // observed Result==UNSET at post entry
  int *realIntSink = nullptr;     // receives *(int*)pRealReturnValue in post
  bool *realBoolSink = nullptr;   // receives *(bool*)pRealReturnValue in post

  int GetAltitude() const override {
    return altitude;
  }
  const char *GetControlModuleName() const override {
    return name;
  }

  void Initialize(CreateInterfaceFn *, int) override {}
  void PreStart(cl_enginefuncs_s *, int, void *) override {}
  void Start(cl_enginefuncs_s *, int, void *) override {}
  void Shutdown() override {}
  void PostShutdown() override {}

  void hit(VGUI2Extension_CallbackContext *ctx, const char *callback) {
    const std::string key =
        std::string(callback) + (ctx->IsPost ? ":post" : ":pre");
    log->add(name, key);
    if (ctx->IsPost) {
      if (ctx->Result != VGUI2Extension_Result::UNSET)
        postSawResultReset = false;
      if (!actWhenNotPost)
        actionCount++;
      if (realIntSink && ctx->pRealReturnValue)
        *realIntSink = *(int *)ctx->pRealReturnValue;
      if (realBoolSink && ctx->pRealReturnValue)
        *realBoolSink = *(bool *)ctx->pRealReturnValue;
      if (postOverride)
        ctx->Result = VGUI2Extension_Result::OVERRIDE;
      if (postSupercede)
        ctx->Result = VGUI2Extension_Result::SUPERCEDE;
      if (postWritesInt)
        *(int *)ctx->pPluginReturnValue = postIntValue;
      if (postWritesBool)
        *(bool *)ctx->pPluginReturnValue = postBoolValue;
    } else {
      actionCount++;
      if (preSkipPlugins)
        ctx->Result = VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;
      else if (preSupercede)
        ctx->Result = VGUI2Extension_Result::SUPERCEDE;
      else if (preHandled)
        ctx->Result = VGUI2Extension_Result::HANDLED;
      if (preWritesInt)
        *(int *)ctx->pPluginReturnValue = preIntValue;
      if (preWritesBool)
        *(bool *)ctx->pPluginReturnValue = preBoolValue;
    }
  }

  void ActivateGameUI(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "ActivateGameUI");
  }
  void ActivateDemoUI(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "ActivateDemoUI");
  }
  void HasExclusiveInput(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "HasExclusiveInput");
  }
  void RunFrame(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "RunFrame");
  }
  void ConnectToServer(const char *&, int &, int &,
                       VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "ConnectToServer");
  }
  void DisconnectFromServer(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "DisconnectFromServer");
  }
  void HideGameUI(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "HideGameUI");
  }
  void IsGameUIActive(VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "IsGameUIActive");
  }
  void LoadingStarted(const char *&, const char *&,
                      VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "LoadingStarted");
  }
  void LoadingFinished(const char *&, const char *&,
                       VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "LoadingFinished");
  }
  void StartProgressBar(const char *&, int &,
                        VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "StartProgressBar");
  }
  void ContinueProgressBar(int &, float &,
                           VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "ContinueProgressBar");
  }
  void StopProgressBar(bool &, const char *&, const char *&,
                       VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "StopProgressBar");
  }
  void SetProgressBarStatusText(const char *&,
                                VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "SetProgressBarStatusText");
  }
  void SetSecondaryProgressBar(float &,
                               VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "SetSecondaryProgressBar");
  }
  void
  SetSecondaryProgressBarText(const char *&,
                              VGUI2Extension_CallbackContext *ctx) override {
    hit(ctx, "SetSecondaryProgressBarText");
  }
};

struct ScriptedBaseUI final : public IVGUI2Extension_BaseUICallbacks {
  int altitude = 0;
  const char *name = "?";
  CallLog *log = nullptr;
  bool preSupercede = false;

  int GetAltitude() const override {
    return altitude;
  }
  void Initialize(CreateInterfaceFn *, int) override {}
  void Start(cl_enginefuncs_s *, int) override {}
  void Shutdown() override {}

  void Key_Event(int &down, int &keynum, const char *&,
                 VGUI2Extension_CallbackContext *ctx) override {
    if (!ctx->IsPost) {
      log->add(name, "Key_Event:pre");
      if (preSupercede)
        ctx->Result = VGUI2Extension_Result::SUPERCEDE;
      keynum = 0x2f; // rewrite a mutable ref param, as a real filter would
    } else {
      log->add(name, "Key_Event:post");
    }
  }
  void CallEngineSurfaceAppProc(void *&, void *&,
                                VGUI2Extension_CallbackContext *) override {}
  void CallEngineSurfaceWndProc(void *&, unsigned int &, unsigned int &, long &,
                                VGUI2Extension_CallbackContext *) override {}
  void Paint(int &, int &, int &, int &,
             VGUI2Extension_CallbackContext *) override {}
  void HideGameUI(VGUI2Extension_CallbackContext *) override {}
  void ActivateGameUI(VGUI2Extension_CallbackContext *) override {}
  void HideConsole(VGUI2Extension_CallbackContext *) override {}
  void ShowConsole(VGUI2Extension_CallbackContext *) override {}
};

// Mirrors CGameUIProxy::ActivateGameUI
// (thirdparty/VGUI2Extension/src/GameUI.cpp:1445-1490, pin b290930).
int proxyActivateGameUI(int &realCalls, int realReturnValue) {
  int fake_ret = 0;
  int real_ret = 0;
  int ret = 0;

  VGUI2Extension_CallbackContext CallbackContext;

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;
  CallbackContext.pPluginReturnValue = &fake_ret;

  VGUI2ExtensionInternal()->GameUI_ActivateGameUI(&CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE) {
    ++realCalls;
    real_ret = realReturnValue;
  }

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = true;
    CallbackContext.pRealReturnValue = &real_ret;

    VGUI2ExtensionInternal()->GameUI_ActivateGameUI(&CallbackContext);
  }

  switch (CallbackContext.Result) {
  case VGUI2Extension_Result::OVERRIDE:
  case VGUI2Extension_Result::SUPERCEDE:
  case VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS:
    ret = fake_ret;
    break;
  default:
    ret = real_ret;
    break;
  }

  return ret;
}

// Mirrors CGameUIProxy::IsGameUIActive
// (thirdparty/VGUI2Extension/src/GameUI.cpp:1675-1720, pin b290930).
bool proxyIsGameUIActive(int &realCalls, bool realReturnValue) {
  bool fake_ret = 0;
  bool real_ret = 0;
  bool ret = 0;

  VGUI2Extension_CallbackContext CallbackContext;

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;
  CallbackContext.pPluginReturnValue = &fake_ret;

  VGUI2ExtensionInternal()->GameUI_IsGameUIActive(&CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE) {
    ++realCalls;
    real_ret = realReturnValue;
  }

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = true;
    CallbackContext.pRealReturnValue = &real_ret;

    VGUI2ExtensionInternal()->GameUI_IsGameUIActive(&CallbackContext);
  }

  switch (CallbackContext.Result) {
  case VGUI2Extension_Result::OVERRIDE:
  case VGUI2Extension_Result::SUPERCEDE:
  case VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS:
    ret = fake_ret;
    break;
  default:
    ret = real_ret;
    break;
  }

  return ret;
}

// Mirrors CGameUIProxy::ActivateDemoUI
// (thirdparty/VGUI2Extension/src/GameUI.cpp:1492-1534, pin b290930): the post
// phase has no SUPERCEDE_SKIP_PLUGINS guard - it is dispatched unconditionally.
int proxyActivateDemoUI(int &realCalls, int realReturnValue) {
  int fake_ret = 0;
  int real_ret = 0;
  int ret = 0;

  VGUI2Extension_CallbackContext CallbackContext;

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;
  CallbackContext.pPluginReturnValue = &fake_ret;

  VGUI2ExtensionInternal()->GameUI_ActivateDemoUI(&CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE) {
    ++realCalls;
    real_ret = realReturnValue;
  }

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = true;
  CallbackContext.pRealReturnValue = &real_ret;

  VGUI2ExtensionInternal()->GameUI_ActivateDemoUI(&CallbackContext);

  switch (CallbackContext.Result) {
  case VGUI2Extension_Result::OVERRIDE:
  case VGUI2Extension_Result::SUPERCEDE:
  case VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS:
    ret = fake_ret;
    break;
  default:
    ret = real_ret;
    break;
  }

  return ret;
}

// Mirrors CGameUIProxy::HideGameUI
// (thirdparty/VGUI2Extension/src/GameUI.cpp:1652-1673, pin b290930) including
// its quirk: the post phase sets IsPost back to false.
void proxyHideGameUI(int &realCalls) {
  VGUI2Extension_CallbackContext CallbackContext;

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;

  VGUI2ExtensionInternal()->GameUI_HideGameUI(&CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE) {
    ++realCalls;
  }

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = false;

    VGUI2ExtensionInternal()->GameUI_HideGameUI(&CallbackContext);
  }
}

// Mirrors CBaseUIProxy::Key_Event
// (thirdparty/VGUI2Extension/src/BaseUI.cpp:178-223, pin b290930).
int proxyBaseUIKeyEvent(int &realCalls, int realReturnValue,
                        int &observedKeynum, int keynum) {
  int down = 1;
  int key = keynum;
  const char *binding = "bind";

  int fake_ret = 0;
  int real_ret = 0;
  int ret = 0;

  VGUI2Extension_CallbackContext CallbackContext;

  CallbackContext.Result = VGUI2Extension_Result::UNSET;
  CallbackContext.IsPost = false;
  CallbackContext.pPluginReturnValue = &fake_ret;

  VGUI2ExtensionInternal()->BaseUI_Key_Event(down, key, binding,
                                             &CallbackContext);

  if (CallbackContext.Result < VGUI2Extension_Result::SUPERCEDE) {
    ++realCalls;
    real_ret = realReturnValue;
    observedKeynum = key;
  }

  if (CallbackContext.Result != VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS) {
    CallbackContext.Result = VGUI2Extension_Result::UNSET;
    CallbackContext.IsPost = true;
    CallbackContext.pRealReturnValue = &real_ret;

    VGUI2ExtensionInternal()->BaseUI_Key_Event(down, key, binding,
                                               &CallbackContext);
  }

  switch (CallbackContext.Result) {
  case VGUI2Extension_Result::OVERRIDE:
  case VGUI2Extension_Result::SUPERCEDE:
  case VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS:
    ret = fake_ret;
    break;
  default:
    ret = real_ret;
    break;
  }

  return ret;
}

ScriptedGameUI *reg(ScriptedGameUI *plugin) {
  VGUI2ExtensionInternal()->RegisterGameUICallbacks(plugin);
  return plugin;
}

void unreg(ScriptedGameUI *plugin) {
  VGUI2ExtensionInternal()->UnregisterGameUICallbacks(plugin);
}

} // namespace

int main() {
  // 1. Without plugin writes, the int wrapper falls back to the real return.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    reg(&a);
    int realCalls = 0;
    const int ret = proxyActivateGameUI(realCalls, 7);
    unreg(&a);
    CHECK(realCalls == 1);
    CHECK(ret == 7);
    CHECK(a.actionCount == 2); // unguarded sample: pre + post
  }

  // 2. Pre SUPERCEDE skips the original but the post phase still runs with
  //    Result reset - and without a post re-assert the value falls back to
  //    real_ret (the trap behind "keep override semantics in post").
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.preSupercede = true;
    a.preWritesInt = true;
    a.preIntValue = 5;
    a.actWhenNotPost = true;
    reg(&a);
    int realCalls = 0;
    const int ret = proxyActivateGameUI(realCalls, 9);
    unreg(&a);
    CHECK(realCalls == 0); // original skipped
    CHECK(log.count("a", "ActivateGameUI:pre") == 1);
    CHECK(log.count("a", "ActivateGameUI:post") == 1); // post still dispatched
    CHECK(a.postSawResultReset); // post entered with Result==UNSET
    CHECK(ret == 0);             // fake value lost: real_ret(0) wins
    CHECK(a.actionCount == 1);   // !IsPost guard: action exactly once
  }

  // 3. Pre SUPERCEDE + post re-assert keeps the plugin value.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.preSupercede = true;
    a.preWritesInt = true;
    a.preIntValue = 5;
    a.postSupercede = true;
    a.postWritesInt = true;
    a.postIntValue = 5;
    reg(&a);
    int realCalls = 0;
    const int ret = proxyActivateGameUI(realCalls, 9);
    unreg(&a);
    CHECK(realCalls == 0);
    CHECK(ret == 5);
  }

  // 4. Post OVERRIDE after the original ran: real executes once, post wins.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.postOverride = true;
    a.postWritesInt = true;
    a.postIntValue = 3;
    reg(&a);
    int realCalls = 0;
    const int ret = proxyActivateGameUI(realCalls, 8);
    unreg(&a);
    CHECK(realCalls == 1);
    CHECK(ret == 3);
  }

  // 5. bool returns travel through bool storage, and post sees the real value.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.preSupercede = true;
    a.preWritesBool = true;
    a.preBoolValue = true;
    a.postSupercede = true;
    a.postWritesBool = true;
    a.postBoolValue = true;
    bool realBool = true;
    a.realBoolSink = &realBool;
    reg(&a);
    int realCalls = 0;
    const bool ret = proxyIsGameUIActive(realCalls, false);
    unreg(&a);
    CHECK(realCalls == 0);
    CHECK(ret == true);
    CHECK(realBool ==
          false); // *(bool*)pRealReturnValue of the skipped real_ret
  }

  // 6. HANDLED aborts the remaining pre-phase plugins, still runs the real
  //    function, and does NOT suppress the post phase of lower plugins.
  {
    CallLog log;
    ScriptedGameUI high;
    high.name = "high";
    high.log = &log;
    high.altitude = 10;
    high.preHandled = true;
    ScriptedGameUI low;
    low.name = "low";
    low.log = &log;
    reg(&high);
    reg(&low);
    int realCalls = 0;
    const int ret = proxyActivateGameUI(realCalls, 6);
    unreg(&high);
    unreg(&low);
    CHECK(log.count("high", "ActivateGameUI:pre") == 1);
    CHECK(log.count("low", "ActivateGameUI:pre") == 0); // phase aborted
    CHECK(realCalls == 1); // HANDLED < SUPERCEDE: real runs
    CHECK(ret == 6);       // no override: real value
    CHECK(log.count("high", "ActivateGameUI:post") == 1);
    CHECK(log.count("low", "ActivateGameUI:post") == 1); // post is a fresh loop
  }

  // 7. ActivateDemoUI: SUPERCEDE_SKIP_PLUGINS does not stop the post phase.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.preSkipPlugins = true;
    a.preWritesInt = true;
    a.preIntValue = 4;
    a.postSupercede = true;
    a.postWritesInt = true;
    a.postIntValue = 4;
    reg(&a);
    int realCalls = 0;
    const int ret = proxyActivateDemoUI(realCalls, 2);
    unreg(&a);
    CHECK(realCalls == 0); // original skipped
    CHECK(log.count("a", "ActivateDemoUI:post") ==
          1); // dispatched despite SKIP_PLUGINS
    CHECK(ret == 4);
  }

  // 8. HideGameUI at this pin sets IsPost=false in its post phase: a
  //    !IsPost-guarded plugin acts twice. Documented trap - do not use
  //    IsPost to deduplicate work inside HideGameUI.
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.actWhenNotPost = true;
    reg(&a);
    int realCalls = 0;
    proxyHideGameUI(realCalls);
    unreg(&a);
    CHECK(realCalls == 1);
    CHECK(a.actionCount == 2); // both dispatches observed as "not post"
  }

  // 9. Higher altitude is called first in both phases.
  {
    CallLog log;
    ScriptedGameUI high;
    high.name = "high";
    high.log = &log;
    high.altitude = 10;
    ScriptedGameUI low;
    low.name = "low";
    low.log = &log;
    reg(&low);
    reg(&high); // registered after, must still run first
    int realCalls = 0;
    proxyActivateGameUI(realCalls, 1);
    unreg(&high);
    unreg(&low);
    const auto highIt = std::find(log.entries.begin(), log.entries.end(),
                                  std::string("high:ActivateGameUI:pre"));
    const auto lowIt = std::find(log.entries.begin(), log.entries.end(),
                                 std::string("low:ActivateGameUI:pre"));
    CHECK(highIt != log.entries.end() && lowIt != log.entries.end() &&
          highIt < lowIt);
    const auto highPostIt = std::find(log.entries.begin(), log.entries.end(),
                                      std::string("high:ActivateGameUI:post"));
    const auto lowPostIt = std::find(log.entries.begin(), log.entries.end(),
                                     std::string("low:ActivateGameUI:post"));
    CHECK(highPostIt != log.entries.end() && lowPostIt != log.entries.end() &&
          highPostIt < lowPostIt);
  }

  // 10. Equal-altitude plugins are all called (order intentionally unasserted;
  //     std::sort is not stable at this pin).
  {
    CallLog log;
    ScriptedGameUI a;
    a.name = "a";
    a.log = &log;
    a.altitude = 5;
    ScriptedGameUI b;
    b.name = "b";
    b.log = &log;
    b.altitude = 5;
    reg(&a);
    reg(&b);
    int realCalls = 0;
    proxyActivateGameUI(realCalls, 1);
    unreg(&a);
    unreg(&b);
    CHECK(log.count("a", "ActivateGameUI:pre") == 1 &&
          log.count("b", "ActivateGameUI:pre") == 1);
    CHECK(log.count("a", "ActivateGameUI:post") == 1 &&
          log.count("b", "ActivateGameUI:post") == 1);
  }

  // 11. BaseUI Key_Event: SUPERCEDE in pre suppresses the original but the
  //     post phase still runs; the wrapper value falls back to real_ret.
  {
    CallLog log;
    ScriptedBaseUI a;
    a.name = "a";
    a.log = &log;
    a.preSupercede = true;
    int observedKeynum = -1;
    int realCalls = 0;
    VGUI2ExtensionInternal()->RegisterBaseUICallbacks(&a);
    const int ret = proxyBaseUIKeyEvent(realCalls, 1, observedKeynum, 0x1b);
    VGUI2ExtensionInternal()->UnregisterBaseUICallbacks(&a);
    CHECK(realCalls == 0);
    CHECK(ret == 0); // post left UNSET: real_ret(0) wins
    CHECK(log.count("a", "Key_Event:pre") == 1 &&
          log.count("a", "Key_Event:post") == 1);
  }

  // 12. Same shape without SUPERCEDE: a pre-phase rewrite of keynum reaches
  //     the original function (mutable reference parameters).
  {
    CallLog log;
    ScriptedBaseUI a;
    a.name = "a";
    a.log = &log;
    int observedKeynum = -1;
    int realCalls = 0;
    VGUI2ExtensionInternal()->RegisterBaseUICallbacks(&a);
    const int ret = proxyBaseUIKeyEvent(realCalls, 1, observedKeynum, 0x1b);
    VGUI2ExtensionInternal()->UnregisterBaseUICallbacks(&a);
    CHECK(realCalls == 1);
    CHECK(observedKeynum == 0x2f); // pre-phase rewrite propagated
    CHECK(ret == 1);               // real value
  }

  std::cout << "hook contract: all checks passed\n";
  return abctest::finish("abc_ui_hook_contract");
}
