// abc_ui_keybind_model - T11 acceptance for the production key-bind model.
// Links the real KeyBindModel.cpp (no model copy); the backend is an
// in-memory binding table that emulates the engine facts pinned in
// execution/T11.md: Key_BindingForKey reads the same table `bind` writes,
// clientCmd is buffered until flush() unless immediate mode is on, ESCAPE
// binds are refused, and malformed keynames never reach the seam.
#include "services/settings/KeyBindModel.h"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "framework/TestHarness.h"

namespace {

// Minimal engine emulation: keynum -> binding table, a keyname table with
// the shapes Key_NameForKey really produces (single printable ASCII chars
// and [A-Za-z0-9_]+ names), clientCmd with Cbuf-buffering and fault hooks.
struct FakeEngine {
  std::map<int, std::string>
      bindings; // keynum -> command (the 0x08447988 table)
  std::map<std::string, int> keynums;  // keyname -> keynum
  std::vector<std::string> issued;     // commands handed to the seam
  bool buffered = false;               // clientCmd defers until flush()
  bool failCmd = false;                // clientCmd returns false
  const char *poisonKeyName = nullptr; // Key_NameForKey returns this

  // Keys the tests use: 'a'=97 'b'=98 'q'=113 's'=115 'w'=119 '`'=96,
  // ESCAPE=27, MOUSE1..3=241..243, MOUSE4=244, MWHEELUP=240, MWHEELDOWN=239.
  FakeEngine() {
    keynums["a"] = 97;
    keynums["b"] = 98;
    keynums["q"] = 113;
    keynums["s"] = 115;
    keynums["w"] = 119;
    keynums["`"] = 96;
    keynums["ESCAPE"] = 27;
    keynums["MOUSE1"] = 241;
    keynums["MOUSE2"] = 242;
    keynums["MOUSE3"] = 243;
    keynums["MOUSE4"] = 244;
    keynums["MWHEELUP"] = 240;
    keynums["MWHEELDOWN"] = 239;
  }

  const char *nameFor(int keynum) const {
    if (poisonKeyName && keynum == 97)
      return poisonKeyName;
    for (const auto &entry : keynums)
      if (entry.second == keynum)
        return entry.first.c_str();
    return "";
  }

  // The engine-side bind/unbind handler (hw.dll 0x01d71ae0 semantics).
  bool applyCommand(const std::string &command) {
    auto apply = [&](std::string key, std::string rest, bool set) {
      if (key.empty())
        return;
      int keynum = -1;
      if (key.size() == 1)
        keynum = static_cast<unsigned char>(key[0]);
      else {
        const auto itor = keynums.find(key);
        if (itor == keynums.end())
          return;
        keynum = itor->second;
      }
      if (keynum == 27 && set)
        return; // "Can't unbind ESCAPE key" - the engine refuses
      if (set)
        bindings[keynum] = rest;
      else
        bindings.erase(keynum);
    };
    if (command.rfind("bind \"", 0) == 0) {
      const auto closeKey = command.find('"', 6);
      if (closeKey == std::string::npos)
        return true;
      const std::string key = command.substr(6, closeKey - 6);
      const auto openCmd = command.find('"', closeKey + 1);
      if (openCmd == std::string::npos)
        return true;
      const std::string rest =
          command.substr(openCmd + 1, command.size() - openCmd - 2);
      apply(key, rest, true);
      return true;
    }
    if (command.rfind("unbind \"", 0) == 0) {
      const auto closeKey = command.find('"', 8);
      if (closeKey == std::string::npos)
        return true;
      const std::string key = command.substr(8, closeKey - 8);
      apply(key, "", false);
      return true;
    }
    return true;
  }

  bool execute(const std::string &command) {
    issued.push_back(command);
    if (failCmd)
      return false;
    if (buffered) {
      Pending.push_back(command); // Cbuf holds it until the next frame
      return true;
    }
    applyCommand(command);
    return true;
  }

  void flush() {
    for (const auto &command : Pending)
      applyCommand(command);
    Pending.clear();
  }

  std::vector<std::string> Pending;

  services::settings::KeyBindModel::Seams seams() {
    services::settings::KeyBindModel::Seams seams;
    seams.bindingForKey = [](int keynum) -> const char * {
      const auto itor = Current->bindings.find(keynum);
      return itor == Current->bindings.end() ? "" : itor->second.c_str();
    };
    seams.keyNameForKey = [](int keynum) -> const char * {
      return Current->nameFor(keynum);
    };
    seams.keyStringToKeyNum = [](const char *name) -> int {
      const auto itor = Current->keynums.find(name);
      return itor == Current->keynums.end() ? -1 : itor->second;
    };
    seams.clientCmd = [](const char *command) -> bool {
      return Current->execute(command);
    };
    return seams;
  }

  static FakeEngine *Current;
};

FakeEngine *FakeEngine::Current = nullptr;

services::settings::KeyBindModel makeModel(FakeEngine &engine) {
  services::settings::KeyBindModel model;
  model.setSeams(engine.seams());
  return model;
}

// The production kb_act.lst shape (extract: headers, tokens, literals,
// semicolon compound commands).
const char *kKbAct =
    "\"blank\"\t\t\t\t\"===========================\"\n"
    "\"blank\"\t\t\t\t\"#Valve_Movement_Title\"\n"
    "\"+forward\"\t\t\t\"#Valve_Move_Forward\"\n"
    "\"+back\"\t\t\t\t\"#Valve_Move_Back\"\n"
    "\"impulse 100\"\t\t\t\"#Valve_Flashlight\"\n"
    "\"votemenu\"\t\t\t\"Voting menu\"\n"
    "\"grenade;npc_findcover\"\t\"Yell to warn other players\"\n";

const char *kKbDef = "\"w\"\t\t\t\"+forward\"\n"
                     "\"s\"\t\t\t\"+back\"\n"
                     "\"u\"\t\t\t\"impulse 201\"\n"
                     "\"u\"\t\t\t\"messagemode2\"\n"
                     "\"ESCAPE\"\t\t\"escape\"\n"
                     "\"`\"\t\t\t\"toggleconsole\"\n"
                     "\"MOUSE1\"\t\t\"+attack\"\n";

const char *kValveTxt = "\"Valve_Move_Forward\"\t\"Move forward\"\n"
                        "// comment line\n"
                        "\"Valve_Move_Back\"\t\"Move back\"\n";

int findAction(const services::settings::KeyBindModel &model,
               const char *command) {
  for (int i = 0; i < model.actionCount(); ++i) {
    auto view = model.find(i);
    if (view && view->action && view->action->Command == command)
      return i;
  }
  return -1;
}

} // namespace

int main() {
  using namespace services::settings;

  // -- kb_act.lst parsing: headers, tokens, literals, compound commands ---
  {
    TEST_CASE("actions-parse");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    auto model = makeModel(engine);
    CHECK(model.loadActions(kKbAct));
    CHECK(model.actionCount() == 7);
    auto view = model.find(0);
    CHECK(view && view->header && view->action->Command.empty());
    view = model.find(2);
    CHECK(view && !view->header && view->action->Command == "+forward");
    CHECK(view->action->Label == "#Valve_Move_Forward");
    view = model.find(5);
    CHECK(view && view->action->Command == "votemenu");
    CHECK(view->action->Label == "Voting menu"); // literal label kept verbatim
    view = model.find(6);
    CHECK(view && view->action->Command == "grenade;npc_findcover");

    CHECK(model.loadLabels(kValveTxt));
    std::string label;
    CHECK(model.lookupLabel("Valve_Move_Forward", label) &&
          label == "Move forward");
    CHECK(!model.lookupLabel("Valve_Nope", label));
    CHECK(model.loadDefaults(kKbDef)); // parsed; ordering checked via reset
  }

  // -- resource text decoding: UTF-16LE BOM, ASCII passthrough ------------
  {
    TEST_CASE("decode-resource-text");
    // "A\"B" UTF-16LE with BOM: 22 00 41 00 22 00 42 00
    const char utf16le[] = {'\xff', '\xfe', 'A', '\x00',
                            '"',    '\x00', 'B', '\x00'};
    CHECK(KeyBindModel::decodeResourceText(utf16le, sizeof(utf16le)) == "A\"B");
    // Surrogate pair: U+1F600 as D83D DE00 (LE).
    const char utf16emoji[] = {'\xff', '\xfe', '\x3d', '\xd8', '\x00', '\xde'};
    CHECK(KeyBindModel::decodeResourceText(utf16emoji, sizeof(utf16emoji)) ==
          "\xf0\x9f\x98\x80");
    // BOM-less input passes through byte-for-byte.
    const char ascii[] = "\"Valve_X\" \"Move\"\n";
    CHECK(KeyBindModel::decodeResourceText(ascii, sizeof(ascii) - 1) == ascii);
    CHECK(KeyBindModel::decodeResourceText(nullptr, 4).empty());
    CHECK(KeyBindModel::decodeResourceText("", 0).empty());
  }

  // -- fail-closed: no reads, empty files ---------------------------------
  {
    TEST_CASE("fail-closed");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    KeyBindModel bare; // no seams wired: fail-closed everywhere
    CHECK(!bare.readsAvailable());
    CHECK(!bare.beginCapture(0));
    CHECK(bare.apply().empty());
    CHECK(!bare.isDirty());
    auto model = makeModel(engine);
    model.setSeams({});
    CHECK(!model.beginCapture(0));
    CHECK(model.apply().empty());
    CHECK(model.captureKey(97) == KeyBindModel::CaptureResult::kNotCapturing);
    model.setSeams(engine.seams());
    CHECK(!model.loadActions("no quotes here\n")); // malformed: refused
    CHECK(model.actionCount() == 0);
    CHECK(model.loadActions(""));
    CHECK(model.actionCount() == 0);
    model.snapshot();
    CHECK(model.rows().empty());
    CHECK(!model.isDirty());
  }

  // -- snapshot: multi-key actions, empty bindings, unlisted commands -----
  {
    TEST_CASE("snapshot-truth");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward";     // w
    engine.bindings[98] = "+forward";      // b  (two keys, one action)
    engine.bindings[241] = "+attack";      // MOUSE1 -> NOT in the action list
    engine.bindings[96] = "toggleconsole"; // ` -> unlisted
    auto model = makeModel(engine);
    CHECK(model.loadActions(kKbAct));
    model.snapshot();
    const auto forward = model.find(findAction(model, "+forward"));
    CHECK(forward && forward->state == BindRowState::kClean);
    CHECK(forward->draftKeyNames.size() == 2);
    // Ascending keynum order: b(98) first, w(119) second.
    CHECK(forward->draftKeyNames[0] == "b" && forward->draftKeyNames[1] == "w");
    const auto votemenu = model.find(findAction(model, "votemenu"));
    CHECK(votemenu && votemenu->state == BindRowState::kClean);
    CHECK(votemenu->draftKeyNames.empty()); // unbound action: empty, not broken
    CHECK(!model.isDirty());
    // MOUSE1 stays +attack and invisible; nothing rewritten anywhere.
    CHECK(engine.bindings[241] == "+attack");
    CHECK(engine.issued.empty());
    CHECK(model.apply().empty());
    CHECK(engine.issued.empty());
  }

  // -- capture: accept, duplicate, ESC, unknown key ------------------------
  {
    TEST_CASE("capture-accept");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward";
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int back = findAction(model, "+back");
    CHECK(model.beginCapture(back));
    CHECK(model.capturing() && model.captureRow() == back);
    CHECK(model.captureKey(97) == KeyBindModel::CaptureResult::kAccepted); // a
    CHECK(!model.capturing());
    CHECK(model.find(back)->state == BindRowState::kDirty);
    CHECK(model.find(back)->draftKeyNames.size() == 1);
    CHECK(model.find(back)->draftKeyNames[0] == "a");
    CHECK(model.isDirty());
    CHECK(engine.issued.empty()); // drafts only: zero writes

    CHECK(model.beginCapture(back));
    CHECK(model.captureKey(27) ==
          KeyBindModel::CaptureResult::kRejected); // ESC never binds
    CHECK(!model.capturing());
    CHECK(model.find(back)->draftKeyNames.size() == 1);

    CHECK(model.beginCapture(back));
    CHECK(model.captureKey(240) ==
          KeyBindModel::CaptureResult::kAccepted); // MWHEELUP
    CHECK(model.find(back)->draftKeyNames.size() == 2);

    CHECK(model.beginCapture(back));
    CHECK(model.captureKey(240) ==
          KeyBindModel::CaptureResult::kDuplicate); // already there
    CHECK(!model.capturing());
    CHECK(model.find(back)->draftKeyNames.size() == 2);

    CHECK(model.beginCapture(back));
    model.cancelCapture();
    CHECK(!model.capturing());
    CHECK(model.find(back)->state ==
          BindRowState::kDirty); // earlier drafts kept

    CHECK(model.beginCapture(back));
    CHECK(model.captureKey(200) ==
          KeyBindModel::CaptureResult::kRejected); // nameless keynum
  }

  // -- conflict: confirm replaces (both rows dirty), decline is a no-op ---
  {
    TEST_CASE("conflict");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward"; // w -> +forward
    engine.bindings[97] = "+back";     // a -> +back
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int forward = findAction(model, "+forward");
    const int back = findAction(model, "+back");

    CHECK(model.beginCapture(forward));
    CHECK(model.captureKey(97) ==
          KeyBindModel::CaptureResult::kConflict); // a is +back
    CHECK(!model.capturing() && model.conflictPending());
    CHECK(model.conflictKeyName() == "a");
    CHECK(model.conflictOtherLabel() == "#Valve_Move_Back");
    CHECK(model.find(forward)->state ==
          BindRowState::kClean); // nothing moved yet
    CHECK(model.find(back)->state == BindRowState::kClean);
    CHECK(!model.isDirty());

    CHECK(model.resolveConflict(false)); // decline: zero change anywhere
    CHECK(!model.conflictPending());
    CHECK(model.find(back)->draftKeyNames.size() == 1);
    CHECK(!model.isDirty());

    CHECK(model.beginCapture(forward));
    CHECK(model.captureKey(97) == KeyBindModel::CaptureResult::kConflict);
    CHECK(model.resolveConflict(true));
    CHECK(model.find(forward)->state == BindRowState::kDirty);
    CHECK(model.find(forward)->draftKeyNames.size() == 2); // w, a
    CHECK(model.find(back)->state == BindRowState::kDirty);
    CHECK(model.find(back)->draftKeyNames.empty()); // a dropped
    CHECK(model.isDirty());

    // One key move = ONE bind command (never bind-then-unbind).
    const auto pending = model.pendingCommands();
    CHECK(pending.size() == 1);
    CHECK(pending[0] == "bind \"a\" \"+forward\"");
  }

  // -- conflict with an unlisted command: confirmed overwrite -------------
  {
    TEST_CASE("conflict-unlisted");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[113] = "my_custom_cmd"; // q -> user command not in kb_act
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int back = findAction(model, "+back");
    CHECK(model.beginCapture(back));
    CHECK(model.captureKey(113) == KeyBindModel::CaptureResult::kConflict);
    CHECK(model.conflictOtherLabel() == "my_custom_cmd"); // raw command shown
    CHECK(model.resolveConflict(true));
    CHECK(model.find(back)->state == BindRowState::kDirty);

    const auto results = model.apply();
    CHECK(results.size() == 1 &&
          results[0].outcome == BindApplyOutcome::kApplied);
    CHECK(engine.bindings[113] == "+back"); // user confirmed the overwrite
    CHECK(!model.isDirty());
  }

  // -- apply: only changed keys, buffered engine, verify lifecycle --------
  {
    TEST_CASE("apply-buffered");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.buffered = true; // pfnClientCmd rides the command buffer
    engine.bindings[119] = "+forward";
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int back = findAction(model, "+back");
    model.beginCapture(back);
    model.captureKey(97); // a
    const int votemenu = findAction(model, "votemenu");
    model.beginCapture(votemenu);
    model.captureKey(242); // MOUSE2

    const auto results = model.apply();
    CHECK(results.size() == 2); // both touched rows reported
    for (const auto &result : results)
      CHECK(result.outcome ==
            BindApplyOutcome::kQueued); // Cbuf has not flushed
    CHECK(engine.issued.size() == 2);
    CHECK(engine.issued[0] == "bind \"a\" \"+back\"");
    CHECK(engine.issued[1] == "bind \"MOUSE2\" \"votemenu\"");
    CHECK(model.isDirty()); // queued still counts

    engine.flush(); // the frame boundary
    model.verify(); // next tick: truth landed
    CHECK(model.find(back)->state == BindRowState::kClean);
    CHECK(model.find(votemenu)->state == BindRowState::kClean);
    CHECK(!model.isDirty());
    // The untouched +forward binding was never part of any command.
    CHECK(engine.bindings[119] == "+forward");
  }

  // -- verify mismatch: reported, retryable, never a silent success -------
  {
    TEST_CASE("verify-failed");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int back = findAction(model, "+back");
    model.beginCapture(back);
    model.captureKey(98); // b
    // The engine "swallows" this one: the fake refuses the write, the
    // truth never moves - exactly the readback-mismatch signature.
    engine.failCmd = true;
    const auto results = model.apply();
    CHECK(results.size() == 1 &&
          results[0].outcome == BindApplyOutcome::kWriteRefused);
    engine.failCmd = false;
    CHECK(model.find(back)->state == BindRowState::kApplyFailed);
    CHECK(model.isDirty());
    CHECK(model.apply().size() == 1); // retry touches only the failed row
    CHECK(engine.bindings[98] == "+back");
    model.verify();
    CHECK(model.find(back)->state == BindRowState::kClean);
    CHECK(!model.isDirty());
  }

  // -- injection: malformed names/commands never reach the seam -----------
  {
    TEST_CASE("injection-refused");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int forward = findAction(model, "+forward");
    engine.poisonKeyName = "a\";say pwned"; // Key_NameForKey shape abuse
    CHECK(model.beginCapture(forward));
    CHECK(model.captureKey(97) == KeyBindModel::CaptureResult::kRejected);
    engine.poisonKeyName = nullptr;
    CHECK(model.find(forward)->state == BindRowState::kClean);
    CHECK(engine.issued.empty());
    CHECK(model.pendingCommands().empty());
    CHECK(!model.removeKeyFromAction(forward, "a\";drop")); // hostile keyname
    CHECK(engine.issued.empty());
  }

  // -- unbind drafts: clear + single-key removal --------------------------
  {
    TEST_CASE("unbind-drafts");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward";
    engine.bindings[98] = "+forward";
    engine.bindings[241] = "+attack"; // unlisted command: must survive
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int forward = findAction(model, "+forward");
    CHECK(model.removeKeyFromAction(forward, "w"));
    CHECK(model.find(forward)->state == BindRowState::kDirty);
    CHECK(model.find(forward)->draftKeyNames.size() == 1);
    CHECK(!model.removeKeyFromAction(forward, "zz")); // not there: refused
    auto pending = model.pendingCommands();
    CHECK(pending.size() == 1 && pending[0] == "unbind \"w\"");

    model.cancel(); // cancel drops the draft, writes nothing
    CHECK(!model.isDirty());
    CHECK(engine.issued.empty());
    CHECK(model.find(forward)->state == BindRowState::kClean);
    CHECK(model.find(forward)->draftKeyNames.size() == 2);

    CHECK(model.clearActionDraft(forward));
    CHECK(model.find(forward)->draftKeyNames.empty());
    pending = model.pendingCommands();
    CHECK(pending.size() == 2); // unbind w, unbind b
    const auto results = model.apply();
    CHECK(results.size() == 1 &&
          results[0].outcome == BindApplyOutcome::kApplied);
    CHECK(engine.bindings.count(119) == 0 && engine.bindings.count(98) == 0);
    // MOUSE1's unlisted +attack binding survived untouched.
    CHECK(engine.bindings.count(241) == 1);
  }

  // -- reset defaults: only listed commands, engine order, drafts only ----
  {
    TEST_CASE("reset-defaults");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "votemenu"; // user moved w somewhere else
    engine.bindings[96] = "toggleconsole";
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.loadDefaults(kKbDef);
    model.snapshot();
    model.resetDefaults();
    // "+forward" gains w (the user's votemenu key moves back), "+back"
    // gains s; the "u" duplicate resolves to messagemode2 (not listed);
    // escape/toggleconsole entries point at unlisted commands: skipped.
    const auto forward = model.find(findAction(model, "+forward"));
    CHECK(forward->state == BindRowState::kDirty);
    CHECK(forward->draftKeyNames.size() == 1 &&
          forward->draftKeyNames[0] == "w");
    const auto back = model.find(findAction(model, "+back"));
    CHECK(back->state == BindRowState::kDirty);
    CHECK(back->draftKeyNames.size() == 1 && back->draftKeyNames[0] == "s");
    CHECK(engine.bindings[96] == "toggleconsole"); // untouched
    CHECK(engine.issued.empty());                  // drafts only

    const auto results = model.apply();
    CHECK(results.size() ==
          3); // forward gains w, votemenu loses it, back gains s
    CHECK(engine.bindings[119] == "+forward");
    CHECK(engine.bindings[115] == "+back");
    CHECK(!model.isDirty());
  }

  // -- cancel: provably zero writes across the whole transaction ----------
  {
    TEST_CASE("cancel-zero-write");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward";
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.loadDefaults(kKbDef);
    model.snapshot();
    const int forward = findAction(model, "+forward");
    model.beginCapture(forward);
    model.captureKey(97);
    model.beginCapture(forward);
    model.captureKey(113); // q unbound: accepted straight into the draft
    CHECK(model.find(forward)->draftKeyNames.size() == 3); // w, a, q
    model.resetDefaults();
    model.cancel();
    CHECK(!model.isDirty());
    CHECK(!model.conflictPending());
    CHECK(engine.issued.empty());
    CHECK(engine.bindings[119] == "+forward");
    CHECK(model.find(forward)->state == BindRowState::kClean);
    CHECK(model.find(forward)->draftKeyNames.size() ==
          1); // w, the engine truth
  }

  // -- reopen keeps drafts (T10 rule) but clean rows follow the engine ----
  {
    TEST_CASE("reopen-keeps-drafts");
    FakeEngine engine;
    FakeEngine::Current = &engine;
    engine.bindings[119] = "+forward";
    auto model = makeModel(engine);
    model.loadActions(kKbAct);
    model.snapshot();
    const int forward = findAction(model, "+forward");
    const int back = findAction(model, "+back");
    model.beginCapture(back);
    model.captureKey(97); // pending a-draft on +back

    engine.bindings[98] = "votemenu"; // external change on a clean row
    model.snapshot();
    CHECK(model.find(back)->state == BindRowState::kDirty); // draft kept
    CHECK(model.find(back)->draftKeyNames.size() == 1);
    CHECK(model.find(findAction(model, "votemenu"))->state ==
          BindRowState::kClean);
    CHECK(model.find(findAction(model, "votemenu"))->draftKeyNames.size() == 1);
    // Only the pending a-draft produces traffic; b already matches truth.
    CHECK(model.apply().size() == 1);
  }

  return abctest::finish("abc_ui_keybind_model");
}
