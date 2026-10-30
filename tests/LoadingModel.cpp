// abc_ui_loading_model - T14.a acceptance for the loading presentation model
// and the copied engine feed. Links the real LoadingModel.cpp and the real
// services::LoadingFeed (GameUiBridge.cpp) - no model copy. Pins:
//   - the strict address grammar (fail-closed on every separator/port error),
//   - the generation filter (retired/superseded attempts drop every late
//     event - the cancel-race rule),
//   - the two independent progress channels (unknown totals stay unknown),
//   - the once-only cancel latch,
//   - the bounded feed (oldest drop, copied strings).
#include "services/loading/LoadingModel.h"
#include "services/GameUiBridge.h"

#include <cstdio>
#include <iostream>
#include <string>
#include "framework/TestHarness.h"

using services::loading::LoadingModel;

namespace {

bool parses(const char *address, const char *host, int port) {
  LoadingModel model;
  std::string outHost;
  int outPort = 0;
  if (!LoadingModel::parseAddress(address, outHost, outPort))
    return false;
  return outHost == host && outPort == port;
}

} // namespace

int main() {
  // -- address grammar ----------------------------------------------------
  {
    TEST_CASE("address-valid");
    CHECK(parses("127.0.0.1", "127.0.0.1", 0));
    CHECK(parses("example.com", "example.com", 0));
    CHECK(parses("sv_example-1.net:27015", "sv_example-1.net", 27015));
    CHECK(parses("localhost:1", "localhost", 1));
    CHECK(parses("a.b.c.d:65535", "a.b.c.d", 65535));
  }
  {
    TEST_CASE("address-invalid");
    std::string host;
    int port = 0;
    const char *bad[] = {"",       ";quit",  "a b",        "\"quoted\"",
                         "a:b:c",  "host:0", "host:65536", "host:x",
                         ":27015", "host:",  "host:1x",    "a\\b",
                         "a\nb",   nullptr};
    for (const char *candidate : bad)
      CHECK(!LoadingModel::parseAddress(candidate, host, port));
  }

  // -- attempt lifecycle + generation filter ------------------------------
  {
    TEST_CASE("generation-filter");
    LoadingModel model;
    model.startAttempt(7, "example.com:27015");
    CHECK(model.generation() == 7 && model.address() == "example.com:27015");

    model.setStatusText(7, "#GameUI_EstablishingConnection");
    CHECK(model.statusText() == "#GameUI_EstablishingConnection");
    // Stale generation and generation 0 are dropped.
    model.setStatusText(6, "stale");
    model.setStatusText(0, "zero");
    CHECK(model.statusText() == "#GameUI_EstablishingConnection");

    model.loadingStarted(7, "level", "osprey");
    CHECK(model.levelStarted() && model.resourceText() == "osprey");
    model.retire();
    CHECK(model.generation() == 0);
    // Every late event of the retired generation is dropped: the values
    // stay at their last live state, nothing of the late writes lands.
    model.setStatusText(7, "late");
    model.continueProgress(7, 5, 0.5f);
    model.loadingFinished(7, "level");
    CHECK(model.statusText() == "#GameUI_EstablishingConnection");
    CHECK(!model.levelFinished());
    CHECK(!model.primary().Known);
  }

  // -- progress channels: unknown totals stay unknown ---------------------
  {
    TEST_CASE("progress-unknown-total");
    LoadingModel model;
    model.startAttempt(1, "x");
    model.startProgress(1, "Connecting", 0); // engine: unknown steps
    CHECK(!model.primary().Known);
    model.continueProgress(1, 0, 0.0f); // still no total, no fraction
    CHECK(!model.primary().Known);
  }
  {
    TEST_CASE("progress-known");
    LoadingModel model;
    model.startAttempt(1, "x");
    model.startProgress(1, "Connecting", 12);
    CHECK(model.primary().Known && model.primary().Steps == 12);
    model.continueProgress(1, 3, 0.25f);
    CHECK(model.primary().Point == 3);
    CHECK(model.primary().Fraction > 0.24f && model.primary().Fraction < 0.26f);
    // Fractions are clamped to 0..1.
    model.continueProgress(1, 99, 1.5f);
    CHECK(model.primary().Fraction == 1.0f);
    model.continueProgress(1, -1, -0.5f);
    CHECK(model.primary().Point == 99 && model.primary().Fraction == 0.0f);
  }
  {
    TEST_CASE("secondary-independent");
    LoadingModel model;
    model.startAttempt(1, "x");
    model.setSecondaryProgress(1, 0.5f);
    model.setSecondaryProgressText(1, "#GameUI_VerifyingAndDownloading");
    CHECK(model.secondaryFraction() > 0.49f &&
          model.secondaryFraction() < 0.51f);
    CHECK(model.secondaryText() == "#GameUI_VerifyingAndDownloading");
    // The two channels never share state: the primary stays unknown.
    CHECK(!model.primary().Known);
    model.setSecondaryProgress(1, 2.0f);
    CHECK(model.secondaryFraction() == 1.0f);
  }

  // -- cancel latch: exactly once per live generation ---------------------
  {
    TEST_CASE("cancel-once");
    LoadingModel model;
    CHECK(!model.requestCancel(1)); // no live attempt
    model.startAttempt(4, "x");
    CHECK(model.requestCancel(4));
    CHECK(!model.requestCancel(4)); // second request lands nowhere
    CHECK(model.canceled());
    CHECK(!model.requestCancel(3)); // stale generation
    // A fresh attempt is NOT born canceled.
    model.startAttempt(5, "y");
    CHECK(!model.canceled());
    CHECK(model.requestCancel(5));
  }

  // -- ordered completion / out-of-order drop ----------------------------
  {
    TEST_CASE("out-of-order-completion");
    LoadingModel model;
    model.startAttempt(2, "x");
    // LoadingFinished before LoadingStarted: recorded, no assumption.
    model.loadingFinished(2, "level");
    CHECK(model.levelFinished() && !model.levelStarted());
    // A late LoadingStarted still lands (no ordering requirement here -
    // the module owns the phase transitions).
    model.loadingStarted(2, "level", "osprey");
    CHECK(model.levelStarted());
  }

  // -- the feed: bounded, copied, FIFO -----------------------------------
  {
    TEST_CASE("feed-fifo-copied");
    auto &feed = services::loadingFeed();
    services::LoadingFeedEvent drained;
    while (feed.pop(drained)) {
    } // drain any residue from earlier cases

    {
      services::LoadingFeedEvent event;
      event.kind = services::LoadingFeedKind::kStatusText;
      std::string borrowed = "#GameUI_EstablishingConnection";
      event.text = borrowed; // copied in
      borrowed = "mutated";  // the source mutation must not matter
      feed.push(event);
    }
    CHECK(feed.pop(drained));
    CHECK(drained.kind == services::LoadingFeedKind::kStatusText);
    CHECK(drained.text == "#GameUI_EstablishingConnection");
    CHECK(!feed.pop(drained));
  }
  {
    TEST_CASE("feed-bounded");
    auto &feed = services::loadingFeed();
    services::LoadingFeedEvent drained;
    while (feed.pop(drained)) {
    }
    const uint32_t droppedBefore = feed.dropped();
    for (int i = 0; i < (int)services::LoadingFeed::kCapacity + 10; ++i) {
      services::LoadingFeedEvent event;
      event.kind = services::LoadingFeedKind::kContinueProgress;
      event.point = i;
      feed.push(event);
    }
    CHECK(feed.dropped() == droppedBefore + 10);
    int expected = 10; // the oldest ten were dropped
    int got = 0;
    while (feed.pop(drained)) {
      CHECK(drained.point == expected);
      ++expected;
      ++got;
    }
    CHECK(got == (int)services::LoadingFeed::kCapacity);
  }

  // -- the classification table (the module drain's single decision) ------
  {
    TEST_CASE("classify-table");
    services::LoadingFeedEvent event;
    event.kind = services::LoadingFeedKind::kLoadingStarted;
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kStarted);
    event.kind = services::LoadingFeedKind::kLoadingFinished;
    event.text = "level";
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kJoined);
    // A non-level finish is bookkeeping only - never "in game".
    event.text = "sound";
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kNone);
    event.kind = services::LoadingFeedKind::kStopProgress;
    event.error = false;
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kNone);
    event.error = true;
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kFailed);
    event.kind = services::LoadingFeedKind::kPasswordPrompt;
    CHECK(services::classifyLoadingEvent(event) ==
          services::LoadingStep::kPasswordRequired);
    for (auto valueOnly : {services::LoadingFeedKind::kStartProgress,
                           services::LoadingFeedKind::kContinueProgress,
                           services::LoadingFeedKind::kStatusText,
                           services::LoadingFeedKind::kSecondaryProgress,
                           services::LoadingFeedKind::kSecondaryText}) {
      event.kind = valueOnly;
      CHECK(services::classifyLoadingEvent(event) ==
            services::LoadingStep::kNone);
    }
  }

  // -- full attempt sequence: connect -> progress -> cancel race ----------
  {
    TEST_CASE("sequence-cancel-race");
    LoadingModel model;
    model.startAttempt(3, "example.com");
    model.loadingStarted(3, "transition", "");
    model.startProgress(3, "Connecting", 12);
    model.continueProgress(3, 6, 0.5f);
    CHECK(model.primary().Point == 6);
    // The player cancels: the model latches once, then the attempt is
    // retired (the controller retires on its own cancel row). Late engine
    // events of generation 3 must land nowhere.
    CHECK(model.requestCancel(3));
    CHECK(!model.requestCancel(3));
    model.retire();
    model.continueProgress(3, 12, 1.0f);
    model.loadingStarted(3, "level", "osprey");
    model.loadingFinished(3, "level");
    CHECK(model.primary().Point == 6);
    CHECK(!model.levelStarted() && !model.levelFinished());
  }

  // -- success path: fresh attempt, level join ----------------------------
  {
    TEST_CASE("sequence-success");
    LoadingModel model;
    model.startAttempt(4, "example.com:27015");
    model.loadingStarted(4, "level", "osprey");
    CHECK(model.resourceText() == "osprey");
    model.loadingFinished(4, "level");
    CHECK(model.levelFinished());
    // The success retires the attempt on the controller side; the model
    // keeps its values until the next startAttempt (the presentation of
    // a closing page does not need them cleared).
  }

  // -- failure then retry: a NEW generation owns the model ---------------
  {
    TEST_CASE("sequence-retry");
    LoadingModel model;
    model.startAttempt(5, "bad.example");
    model.retire();
    // Retry = a fresh attempt; every value of the old one is gone.
    model.startAttempt(6, "bad.example");
    CHECK(model.generation() == 6);
    CHECK(model.address() == "bad.example");
    CHECK(!model.levelStarted() && !model.levelFinished() && !model.canceled());
    CHECK(!model.primary().Known && model.secondaryFraction() == 0.0f);
    CHECK(model.statusText().empty() && model.secondaryText().empty());
  }

  return abctest::finish("abc_ui_loading_model");
}
