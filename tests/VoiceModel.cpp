// abc_ui_voice_model - T13b acceptance for the voice hardware model.
// Links the real VoiceModel.cpp; the IVoiceTweak surface is an in-memory
// fake with the pinned semantics (GetControlFloat/SetControlFloat on
// MicrophoneVolume=0 / MicBoost=2, StartVoiceTweakMode returns 0 on a
// missing device, GetSpeakingVolume as the live level). Pins: the
// fail-closed null-interface path, the draft/apply/read-back lifecycle,
// the start/stop pairing (every stop path is idempotent), the level read
// being the engine's answer (and only while testing), and the device
// floats carrying no persistence beyond the model.
#include "services/settings/VoiceModel.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include "framework/TestHarness.h"

namespace {

struct FakeTweak {
  void *interface = this; // null => the engine provides no interface
  float micVolume = 1.0f;
  float boost = 0.0f;
  int modeDepth = 0; // >0 while a test session is up
  int startResult = 1;
  bool failSet = false;
  float corruptReadback = -1.0f; // <0: disabled
  int speaking = 0;
  int startCalls = 0;
  int stopCalls = 0;

  float getControl(int which) const {
    if (corruptReadback >= 0.0f)
      return corruptReadback;
    return which == 0 ? micVolume : boost;
  }
  bool setControl(int which, float value) {
    if (failSet)
      return false;
    if (which == 0)
      micVolume = value;
    else
      boost = value;
    return true;
  }

  services::settings::VoiceModel::Seams seams() {
    services::settings::VoiceModel::Seams seams;
    seams.tweakInterface = []() -> void * { return Current->interface; };
    seams.startTest = []() -> int {
      ++Current->startCalls;
      return Current->startResult != 0 && Current->interface ? 1 : 0;
    };
    seams.stopTest = []() {
      ++Current->stopCalls;
      --Current->modeDepth;
    };
    seams.getControl = [](int which) { return Current->getControl(which); };
    seams.setControl = [](int which, float value) {
      return Current->setControl(which, value);
    };
    seams.speakingVolume = []() -> int { return Current->speaking; };
    return seams;
  }

  static FakeTweak *Current;
};

FakeTweak *FakeTweak::Current = nullptr;

services::settings::VoiceModel makeModel(FakeTweak &tweak) {
  services::settings::VoiceModel model;
  model.setSeams(tweak.seams());
  return model;
}

} // namespace

int main() {
  using namespace services::settings;

  // -- a missing interface fail-closes the section ------------------------
  {
    TEST_CASE("no-interface");
    FakeTweak tweak;
    tweak.interface = nullptr;
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    CHECK(!model.available());
    CHECK(!model.startTest());     // no pretend session
    model.setMicVolumeDraft(0.5f); // refused: no draft, no dirty
    CHECK(!model.isDirty());
    CHECK(!model.applyMicVolume());
    CHECK(model.speakingLevel() == 0.0f);
  }

  // -- snapshot reads the device truth ------------------------------------
  {
    TEST_CASE("snapshot");
    FakeTweak tweak;
    tweak.micVolume = 0.65f;
    tweak.boost = 1.0f;
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    CHECK(model.available());
    CHECK(std::fabs(model.micVolume() - 0.65f) < 1e-4f);
    CHECK(model.micBoost());
    CHECK(!model.isDirty());
  }

  // -- drafts and apply: write + read-back, refusal keeps the draft -------
  {
    TEST_CASE("draft-apply");
    FakeTweak tweak;
    tweak.micVolume = 1.0f;
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    model.setMicVolumeDraft(0.25f);
    CHECK(model.micDirty());
    CHECK(std::fabs(model.micVolumeDraft() - 0.25f) < 1e-4f);
    // Out-of-domain input clamps (the 0..1 control domain).
    model.setMicVolumeDraft(5.0f);
    CHECK(std::fabs(model.micVolumeDraft() - 1.0f) < 1e-4f);
    model.setMicVolumeDraft(-1.0f);
    CHECK(std::fabs(model.micVolumeDraft() - 0.0f) < 1e-4f);
    model.setMicVolumeDraft(0.4f);
    model.setMicBoostDraft(true);
    CHECK(model.micBoostDirty());
    CHECK(model.isDirty());
    CHECK(model.applyMicVolume());
    CHECK(model.applyMicBoost());
    CHECK(std::fabs(tweak.micVolume - 0.4f) < 1e-4f);
    CHECK(std::fabs(tweak.boost - 1.0f) < 1e-4f);
    CHECK(!model.isDirty());

    // A refused write keeps the draft dirty (retryable, no fake success).
    tweak.failSet = true;
    model.setMicBoostDraft(false);
    CHECK(!model.applyMicBoost());
    CHECK(model.micBoostDirty());
    tweak.failSet = false;

    // A disagreeing read-back is a failure too (the device dropped it).
    tweak.corruptReadback = 0.9f;
    model.setMicVolumeDraft(0.1f);
    CHECK(!model.applyMicVolume());
    CHECK(model.micDirty());
    tweak.corruptReadback = -1.0f;
  }

  // -- the test session is a paired resource ------------------------------
  {
    TEST_CASE("test-session");
    FakeTweak tweak;
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    CHECK(model.speakingLevel() == 0.0f); // no session, no level
    CHECK(model.startTest());
    CHECK(model.testing());
    CHECK(tweak.startCalls == 1);
    // A second start is a no-op (one session, one resource).
    CHECK(model.startTest());
    CHECK(tweak.startCalls == 1);
    // The level is the engine's answer, scaled 0..255 -> 0..1.
    tweak.speaking = 128;
    CHECK(std::fabs(model.speakingLevel() - 128.0f / 255.0f) < 1e-4f);
    tweak.speaking = 300; // out of range clamps
    CHECK(std::fabs(model.speakingLevel() - 1.0f) < 1e-4f);
    tweak.speaking = 0;
    CHECK(model.speakingLevel() == 0.0f);
    model.stopTest();
    CHECK(!model.testing());
    CHECK(model.speakingLevel() == 0.0f); // silence after the session
    model.stopTest();                     // idempotent
    CHECK(tweak.stopCalls == 1);
  }

  // -- a refused start leaves the state untouched --------------------------
  {
    TEST_CASE("test-refused");
    FakeTweak tweak;
    tweak.startResult = 0; // the engine refuses (no device)
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    CHECK(!model.startTest());
    CHECK(!model.testing());
    CHECK(model.speakingLevel() == 0.0f);
    // A stop without a session is a no-op on the engine side too.
    model.stopTest();
    CHECK(tweak.stopCalls == 0);
  }

  // -- a fresh snapshot resets stale drafts (the device is the truth) -----
  {
    TEST_CASE("snapshot-resets-drafts");
    FakeTweak tweak;
    tweak.micVolume = 1.0f;
    FakeTweak::Current = &tweak;
    auto model = makeModel(tweak);
    model.snapshot();
    model.setMicVolumeDraft(0.2f);
    CHECK(model.isDirty());
    tweak.micVolume = 0.8f; // the device moved underneath
    model.snapshot();
    CHECK(!model.isDirty()); // the stale draft is gone
    CHECK(std::fabs(model.micVolumeDraft() - 0.8f) < 1e-4f);
  }

  return abctest::finish("abc_ui_voice_model");
}
