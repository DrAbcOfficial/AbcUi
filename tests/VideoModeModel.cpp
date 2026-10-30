// T12.b: video-mode transaction acceptance. Links the REAL
// services::settings::VideoModeModel.cpp; the engine is an in-memory fake
// emulating the pinned hw.dll facts (T12 execution record a-gate):
//   * _setvideomode/_setrenderer write ScreenWidth/ScreenHeight/ScreenWindowed
//     (the "current mode" the GetCurrentVideoMode slot reads back),
//   * _restart is a no-op here (the fake keeps the written values: that is
//     exactly the in-process restart semantics the engine exhibits),
//   * the pending record is an in-memory slot (production rides
//     VideoPendingStore/abc::Config; the adapter's JSON schema is pinned by
//     the JSON round-trip test in this file).
// Pins: stage validation (enumerated modes only), records-before-writes,
// change-only command batch, restart application, the monotonic confirmation
// window (no extension on focus loss / no frames), confirm/reject/duplicate
// rules, the three startup recovery branches (applied/pending, not-applied,
// stale), the crash-convergence branches, and fail-closed unavailable seams.

#include "../src/services/settings/VideoModeModel.h"
#include "../src/services/settings/VideoPendingStore.h" // the pinned JSON schema

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "framework/TestHarness.h"

using services::settings::VideoMode;
using services::settings::VideoModeModel;
using services::settings::VideoPendingRecord;

namespace {

// --- Fake engine ----------------------------------------------------------

struct FakeEngine {
  VideoMode Current{1920, 1080, 32, true};
  std::vector<VideoMode> List{
      {1920, 1080, 32, false},
      {1600, 900, 32, false},
      {1280, 720, 32, false},
  };
  bool ModeListAvailable = true;
  std::vector<std::string> Issued;
  bool RefuseNextCommand = false;
  bool RefusePersistence = false;

  // persistence slot
  VideoPendingRecord Record;
  bool HasRecord = false;

  // clocks
  double Mono = 100.0;
  long long Wall = 1700000000000LL;

  bool actual(VideoMode &out) {
    out = Current;
    return true;
  }
  int list(VideoMode *out, int max) {
    if (!ModeListAvailable)
      return 0;
    int n = static_cast<int>(List.size());
    for (int i = 0; i < n && i < max; ++i)
      out[i] = List[i];
    return n;
  }
  bool cmd(const char *command) {
    if (RefuseNextCommand) {
      RefuseNextCommand = false; // one-shot fault injection
      return false;
    }
    Issued.push_back(command);
    // Apply the pinned handler semantics (a "restart" keeps the values:
    // this fake IS the post-restart process view).
    int w = 0, h = 0;
    if (std::sscanf(command, "_setvideomode %d %d", &w, &h) == 2) {
      Current.Width = w;
      Current.Height = h;
    } else if (std::strstr(command, "_setrenderer gl windowed")) {
      Current.Windowed = true;
    } else if (std::strstr(command, "_setrenderer gl fullscreen")) {
      Current.Windowed = false;
    }
    return true;
  }
  bool save(const VideoPendingRecord &record) {
    if (RefusePersistence)
      return false;
    Record = record;
    HasRecord = record.Pending != 0;
    return true;
  }
  bool load(VideoPendingRecord &out) {
    if (!HasRecord)
      return false;
    out = Record;
    return true;
  }
  static FakeEngine *Active;

  VideoModeModel::Seams seams() {
    Active = this;
    VideoModeModel::Seams seams;
    // Captureless thunks over the Current case (the T11 fake pattern -
    // the model's seams are plain function pointers, no captures).
    seams.actualMode = [](VideoMode &out) { return Active->actual(out); };
    seams.modeList = [](VideoMode *out, int max) {
      return Active->list(out, max);
    };
    seams.clientCmd = [](const char *command) { return Active->cmd(command); };
    seams.savePending = [](const VideoPendingRecord &record) {
      return Active->save(record);
    };
    seams.loadPending = [](VideoPendingRecord &out) {
      return Active->load(out);
    };
    seams.monotonicSeconds = []() { return Active->mono(); };
    seams.wallMs = []() { return Active->wall(); };
    return seams;
  }
  double mono() const {
    return Mono;
  }
  long long wall() const {
    return Wall;
  }
};

FakeEngine *FakeEngine::Active = nullptr;

VideoModeModel MakeModel(FakeEngine &engine) {
  VideoModeModel model;
  model.setSeams(engine.seams());
  return model;
}

std::string Join(const std::vector<std::string> &commands) {
  std::string out;
  for (const auto &command : commands)
    out += command;
  return out;
}

// --- tests ----------------------------------------------------------------

void TestStageValidation() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.available());
  CHECK(model.actualKnown());
  CHECK(model.actual() == engine.Current);

  // Enumerated mode: accepted.
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.hasStaged());
  CHECK(model.stagedTarget().Width == 1600 &&
        model.stagedTarget().Height == 900);
  // Not enumerated / invalid: refused, no state change.
  CHECK(!model.stageResolution(1024, 768));
  CHECK(!model.stageResolution(100, 100));
  CHECK(!model.stageResolution(-1600, 900));
  model.cancelStage();
  CHECK(!model.hasStaged());
  CHECK(model.state() == VideoModeModel::State::kIdle);

  // Windowed toggle stages independently.
  CHECK(model.stageWindowed(false));
  CHECK(model.stagedTarget().Windowed == false);
  CHECK(model.pendingApplyCommands().size() == 2); // renderer + restart
  model.cancelStage();

  // Staging the current actual value is a no-op batch at apply time.
  CHECK(model.stageResolution(1920, 1080));
  CHECK(!model.apply());
  CHECK(engine.Issued.empty());
}

void TestApplyBatchAndRecoveryRecord() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.apply());
  // Record landed BEFORE the writes (a crash after any command still has it).
  CHECK(engine.HasRecord);
  CHECK(engine.Record.Pending == 1);
  CHECK(engine.Record.OldWidth == 1920 && engine.Record.OldHeight == 1080);
  CHECK(engine.Record.OldWindowed == 1);
  CHECK(engine.Record.NewWidth == 1600 && engine.Record.NewHeight == 900);
  CHECK(Join(engine.Issued) == "_setvideomode 1600 900\n_restart\n");
  CHECK(model.awaitingConfirm());
  // The change-only rule: windowed untouched -> no renderer command.
  CHECK(model.confirmKind() ==
        VideoModeModel::ConfirmKind::kApplied); // fake applied it
}

void TestWindowedOnlyBatch() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageWindowed(false));
  CHECK(model.apply());
  CHECK(Join(engine.Issued) == "_setrenderer gl fullscreen\n_restart\n");
  CHECK(model.confirmKind() == VideoModeModel::ConfirmKind::kApplied);
}

void TestConfirmClearsRecord() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.apply());
  CHECK(model.awaitingConfirm());
  CHECK(model.confirm());
  CHECK(!engine.HasRecord); // cleared (Pending=0)
  CHECK(model.state() == VideoModeModel::State::kIdle);
  // Duplicate confirmation: inert.
  CHECK(!model.confirm());
  // Late revert after confirm: inert (generation gates + state).
  CHECK(!model.revert());
}

void TestRejectRestoresOldMode() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.apply());
  CHECK(model.awaitingConfirm());
  engine.Issued.clear();
  CHECK(model.revert());
  CHECK(Join(engine.Issued) == "_setvideomode 1920 1080\n_restart\n");
  CHECK(engine.Current.Width == 1920 && engine.Current.Height == 1080);
  CHECK(!engine.HasRecord);
  CHECK(model.state() == VideoModeModel::State::kIdle);
  CHECK(!model.revert()); // second revert: inert
}

void TestTimeoutIsMonotonicAndUnboundedByFrames() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageWindowed(false));
  CHECK(model.apply());
  CHECK(model.awaitingConfirm());
  CHECK(model.remainingSeconds() == 15);
  // First tick arms the deadline.
  CHECK(!model.tick());
  engine.Mono += 14.9;
  CHECK(!model.tick()); // not yet
  CHECK(model.remainingSeconds() == 1);
  // No frames for an hour: the deadline still fires on the next tick (and
  // never extended because the deadline is monotonic, not per-frame).
  engine.Mono += 3600.0;
  engine.Issued.clear();
  CHECK(model.tick());
  CHECK(Join(engine.Issued) == "_setrenderer gl windowed\n_restart\n");
  CHECK(!engine.HasRecord);
  CHECK(model.state() == VideoModeModel::State::kIdle);
}

void TestConfirmRefusedWithoutPersistence() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.apply());
  engine.RefusePersistence = true;
  CHECK(!model.confirm()); // stays awaiting; record still there
  CHECK(model.awaitingConfirm());
  engine.RefusePersistence = false;
  CHECK(model.confirm());
  CHECK(model.state() == VideoModeModel::State::kIdle);
}

void TestApplyRefusedLetsStageStay() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  engine.RefusePersistence = true;
  CHECK(!model.apply()); // cannot promise recovery: nothing written
  CHECK(engine.Issued.empty());
  CHECK(engine.Current.Width == 1920); // untouched
  CHECK(model.state() == VideoModeModel::State::kStaged);
}

void TestStartupPendingBranch() {
  FakeEngine engine;
  // A crash left the record behind; the process restarted INTO the new
  // mode (the fake Current equals the record's new values).
  engine.Record.Pending = 1;
  engine.Record.NewWidth = 1600;
  engine.Record.NewHeight = 900;
  engine.Record.NewWindowed = 1;
  engine.Record.OldWidth = 1920;
  engine.Record.OldHeight = 1080;
  engine.Record.OldWindowed = 1;
  engine.Record.AppliedUnixMs = engine.Wall - 1000;
  engine.Record.Generation = 4;
  engine.HasRecord = true;
  engine.Current = VideoMode{1600, 900, 32, true};

  auto model = MakeModel(engine);
  CHECK(model.resolveRecovery() == VideoModeModel::RecoveryResult::kPending);
  CHECK(model.awaitingConfirm());
  CHECK(model.confirmKind() == VideoModeModel::ConfirmKind::kApplied);
  CHECK(engine.Issued.empty()); // no writes on the pending path
  CHECK(model.confirm());
  CHECK(!engine.HasRecord);
}

void TestStartupNotAppliedBranch() {
  FakeEngine engine;
  // Crash before/video-init-not-taken: the session came up with the OLD
  // mode while the record says a switch was requested.
  engine.Record.Pending = 1;
  engine.Record.NewWidth = 1600;
  engine.Record.NewHeight = 900;
  engine.Record.NewWindowed = 1;
  engine.Record.OldWidth = 1920;
  engine.Record.OldHeight = 1080;
  engine.Record.OldWindowed = 1;
  engine.Record.AppliedUnixMs = engine.Wall - 1000;
  engine.HasRecord = true;
  engine.Current = VideoMode{1920, 1080, 32, true};

  auto model = MakeModel(engine);
  CHECK(model.resolveRecovery() ==
        VideoModeModel::RecoveryResult::kAutoReverted);
  // Write-back of the old values happened (no restart: the session is
  // already on the old mode).
  CHECK(Join(engine.Issued) ==
        "_setvideomode 1920 1080\n_setrenderer gl windowed\n");
  CHECK(!engine.HasRecord);
  CHECK(model.state() == VideoModeModel::State::kIdle);
}

void TestStartupStaleAppliedBranch() {
  FakeEngine engine;
  engine.Record.Pending = 1;
  engine.Record.NewWidth = 1600;
  engine.Record.NewHeight = 900;
  engine.Record.NewWindowed = 1;
  engine.Record.OldWidth = 1920;
  engine.Record.OldHeight = 1080;
  engine.Record.OldWindowed = 1;
  engine.Record.AppliedUnixMs =
      engine.Wall - (VideoModeModel::kStaleRecordMs + 1000);
  engine.HasRecord = true;
  engine.Current = VideoMode{1600, 900, 32, true};

  auto model = MakeModel(engine);
  // Wall-clock age includes time after a crash, not time the user accepted
  // the mode. An expired pending record must restore the old mode.
  CHECK(model.resolveRecovery() ==
        VideoModeModel::RecoveryResult::kAutoReverted);
  CHECK(Join(engine.Issued) ==
        "_setvideomode 1920 1080\n_setrenderer gl windowed\n_restart\n");
  CHECK(!engine.HasRecord);
  CHECK(model.state() == VideoModeModel::State::kIdle);
  // A stale record that is NOT applied: auto-revert (no confirmation).
  engine.Record.Pending = 1;
  engine.Record.AppliedUnixMs =
      engine.Wall - (VideoModeModel::kStaleRecordMs + 1000);
  engine.HasRecord = true;
  engine.Current = VideoMode{1920, 1080, 32, true};
  auto model2 = MakeModel(engine);
  CHECK(model2.resolveRecovery() ==
        VideoModeModel::RecoveryResult::kAutoReverted);
  CHECK(!engine.HasRecord);
}

void TestStartupUnknownModeBranch() {
  FakeEngine engine;
  engine.Record.Pending = 1;
  engine.Record.NewWidth = 1600;
  engine.Record.NewHeight = 900;
  engine.Record.NewWindowed = 1;
  engine.Record.OldWidth = 1920;
  engine.Record.OldHeight = 1080;
  engine.Record.OldWindowed = 1;
  engine.Record.AppliedUnixMs = engine.Wall - 1000;
  engine.HasRecord = true;
  engine.Current = VideoMode{1280, 720, 32, true}; // neither old nor new

  auto model = MakeModel(engine);
  CHECK(model.resolveRecovery() ==
        VideoModeModel::RecoveryResult::kAutoReverted);
  // Unknown mode: old values written AND a restart issued to reinitialise.
  CHECK(Join(engine.Issued) ==
        "_setvideomode 1920 1080\n_setrenderer gl windowed\n_restart\n");
  CHECK(!engine.HasRecord);
}

void TestStartupDeferredWithoutReads() {
  FakeEngine engine;
  engine.Record.Pending = 1;
  engine.Record.AppliedUnixMs = engine.Wall - 1000;
  engine.HasRecord = true;
  auto model = MakeModel(engine);
  VideoModeModel::Seams seams = engine.seams();
  seams.actualMode = nullptr; // extension missing
  model.setSeams(seams);
  CHECK(model.resolveRecovery() == VideoModeModel::RecoveryResult::kDeferred);
  CHECK(engine.HasRecord); // kept for a later session
  CHECK(engine.Issued.empty());
}

void TestRevertWriteRefusedKeepsRecord() {
  FakeEngine engine;
  auto model = MakeModel(engine);
  model.snapshot();
  CHECK(model.stageResolution(1600, 900));
  CHECK(model.apply());
  // The revert write is refused the first time: awaiting state and record
  // survive for a retry.
  engine.RefuseNextCommand = true;
  CHECK(!model.revert());
  CHECK(model.awaitingConfirm());
  CHECK(engine.HasRecord);
  CHECK(model.revert()); // retry succeeds
  CHECK(!engine.HasRecord);
}

void TestDescribe() {
  CHECK(VideoModeModel::describe({1920, 1080, 32, true}) ==
        "1920 x 1080 windowed");
  CHECK(VideoModeModel::describe({800, 600, 32, false}) ==
        "800 x 600 fullscreen");
}

// The persisted-record schema (new contract - pinned HERE before anything
// writes it): exact key names through aigc::JsonHelper, plus a round trip.
void TestPendingJsonSchema() {
  services::settings::VideoPendingJson json;
  json.Pending = 1;
  json.NewWidth = 1600;
  json.NewHeight = 900;
  json.NewWindowed = 0;
  json.OldWidth = 1920;
  json.OldHeight = 1080;
  json.OldWindowed = 1;
  json.AppliedUnixMs = 1700000000123LL;
  json.Generation = 7;

  std::string text;
  CHECK(aigc::JsonHelper::ObjectToJson(json, text));
  for (const char *key :
       {"\"pending\"", "\"new_width\"", "\"new_height\"", "\"new_windowed\"",
        "\"old_width\"", "\"old_height\"", "\"old_windowed\"",
        "\"applied_unix_ms\"", "\"generation\""}) {
    if (text.find(key) == std::string::npos) {
      std::printf("FAIL schema key missing: %s (json: %s)\n", key,
                  text.c_str());
      ::abctest::fail(__LINE__, key);
    }
  }

  services::settings::VideoPendingJson parsed;
  CHECK(aigc::JsonHelper::JsonToObject(parsed, text));
  CHECK(parsed.Pending == 1);
  CHECK(parsed.NewWidth == 1600 && parsed.NewHeight == 900 &&
        parsed.NewWindowed == 0);
  CHECK(parsed.OldWidth == 1920 && parsed.OldHeight == 1080 &&
        parsed.OldWindowed == 1);
  CHECK(parsed.AppliedUnixMs == 1700000000123LL);
  CHECK(parsed.Generation == 7);
}

} // namespace

int main() {
  TestStageValidation();
  TestApplyBatchAndRecoveryRecord();
  TestWindowedOnlyBatch();
  TestConfirmClearsRecord();
  TestRejectRestoresOldMode();
  TestTimeoutIsMonotonicAndUnboundedByFrames();
  TestConfirmRefusedWithoutPersistence();
  TestApplyRefusedLetsStageStay();
  TestStartupPendingBranch();
  TestStartupNotAppliedBranch();
  TestStartupStaleAppliedBranch();
  TestStartupUnknownModeBranch();
  TestStartupDeferredWithoutReads();
  TestRevertWriteRefusedKeepsRecord();
  TestDescribe();
  TestPendingJsonSchema();

  return abctest::finish("abc_ui_video_mode");
}
