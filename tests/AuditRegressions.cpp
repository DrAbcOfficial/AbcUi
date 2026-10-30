// Audit-only evidence. Links unchanged production translation units.
// Exit 1 means product expectations failed, NOT a failed reproduction.
#include "framework/UiInputRouter.h"
#include "services/console/ConsoleBackend.h"
#include "services/serverbrowser/ServerQuery.h"
#include "services/serverbrowser/ServerListModel.h"
#include "services/settings/VideoModeModel.h"
#include "services/playerlist/PlayerListModel.h"
#include "services/serverbrowser/ServerBrowserSession.h"
#include "modules/gameui/lifecycle/FramePump.h"
#include "modules/gameui/navigation/MenuAction.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include "framework/TestHarness.h"

void expect(bool good, const char *name) {
  EXPECT_NAMED(good, name);
}
struct AuditSink final : framework::uiinput::Sink {
  int Ups = 0;
  void uiKeyDown(int) override {}
  void uiKeyUp(int) override {
    ++Ups;
  }
  void onExclusiveChange(bool) override {}
};
struct AuditOwner final : framework::uiinput::Owner {
  bool claimsKey(int, int) override {
    return true;
  }
  bool claimsWheel() override {
    return true;
  }
};
using namespace services::serverbrowser;
struct Provider final : IServerQueryProvider {
  std::vector<ServerEntry> Entries;
  std::vector<QueryEvent> Events;
  uint64_t NextRequest = 1;
  uint64_t CurrentRequest = 0;
  std::vector<FilterPair> RequestedFilters;
  bool available() override {
    return true;
  }
  uint64_t requestList(QueryKind, const FilterPair *Filters,
                       int Count) override {
    RequestedFilters.clear();
    if (Filters && Count > 0)
      RequestedFilters.assign(Filters, Filters + Count);
    return CurrentRequest = NextRequest++;
  }
  void cancelQuery(uint64_t) override {}
  void releaseRequest(uint64_t) override {}
  void refreshQueryAll(uint64_t) override {}
  void refreshServer(uint64_t, int) override {}
  int serverCount(uint64_t) override {
    return int(Entries.size());
  }
  bool serverDetails(uint64_t, int i, ServerEntry &out) override {
    if (i < 0 || i >= int(Entries.size()))
      return false;
    out = Entries[i];
    return true;
  }
  uint64_t pingServer(uint32_t, uint16_t) override {
    return 2;
  }
  uint64_t requestPlayers(uint32_t, uint16_t) override {
    return 3;
  }
  void cancelServerQuery(uint64_t) override {}
  int pollEvents(QueryEvent *out, int max) override {
    int n = std::min(max, int(Events.size()));
    std::copy_n(Events.begin(), n, out);
    Events.erase(Events.begin(), Events.begin() + n);
    return n;
  }
  int outstandingRequests() override {
    return 0;
  }
  void response(int i) {
    QueryEvent e;
    e.request = CurrentRequest;
    e.index = i;
    Events.push_back(e);
  }
  void complete() {
    QueryEvent e;
    e.type = QueryEvent::Type::RefreshComplete;
    e.request = CurrentRequest;
    Events.push_back(e);
  }
};
using services::settings::VideoMode;
using services::settings::VideoModeModel;
using services::settings::VideoPendingRecord;
VideoMode Actual{1920, 1080, 32, false};
VideoPendingRecord Record;
std::vector<std::string> Commands;
long long Wall = 1000000;
double Mono = 0;
bool SaveOk = true;
services::playerlist::RosterEntry LivePlayer;
bool LiveMuted = false;
int MuteWrites = 0;

VideoModeModel::Seams videoSeams() {
  VideoModeModel::Seams s;
  s.actualMode = [](VideoMode &out) {
    out = Actual;
    return true;
  };
  s.modeList = [](VideoMode *out, int max) {
    if (max)
      out[0] = Actual;
    return 1;
  };
  s.clientCmd = [](const char *c) {
    Commands.emplace_back(c);
    return true;
  };
  s.savePending = [](const VideoPendingRecord &r) {
    if (!SaveOk)
      return false;
    Record = r;
    return true;
  };
  s.loadPending = [](VideoPendingRecord &r) {
    r = Record;
    return true;
  };
  s.monotonicSeconds = []() { return Mono; };
  s.wallMs = []() { return Wall; };
  return s;
}
void seedRecord() {
  Record = {1, 1920, 1080, 0, 1024, 768, 1, Wall, 1};
  Commands.clear();
  SaveOk = true;
}
int main() {
  using namespace framework::uiinput;
  {
    AuditSink sink;
    Router router(&sink);
    AuditOwner owner;
    router.onKey(1, 'w', false);
    auto token = router.acquire(&owner, OwnerTier::kMenu);
    router.onKey(0, 'w', false);
    auto again = router.onKey(1, 'w', false);
    expect(again.consumeEngine && again.uiKey,
           "A01 released pre-held key must be claimed on its NEXT press");
  }
  {
    AuditSink sink;
    Router router(&sink);
    AuditOwner owner;
    router.onKey(1, 'w', false);
    router.onFocusLost();
    expect(sink.Ups == 1,
           "A02 focus loss without owners must pair observed UI key");
    auto token = router.acquire(&owner, OwnerTier::kMenu);
    expect(router.onKey(1, 'w', false).consumeEngine,
           "A02 focus loss without owners must clear stale EngineDown");
  }
  {
    services::console::ConsoleBackend backend;
    services::console::ConsoleBackend::Seams s;
    s.clientCmd = [](const char *) { return true; };
    backend.setSeams(s);
    backend.submit("echo ok; password AUDIT_DUMMY_SECRET");
    expect(backend.history().empty(),
           "A03 semicolon password command must not be retained in history");
  }
  {
    Provider p;
    p.Entries.resize(2);
    p.Entries[0].ip = 10;
    p.Entries[0].queryPort = 27015;
    p.Entries[0].name = "alpha";
    p.Entries[0].players = 0;
    p.Entries[0].responded = true;
    p.Entries[1].ip = 11;
    p.Entries[1].queryPort = 27015;
    p.Entries[1].name = "zeta";
    p.Entries[1].players = 4;
    p.Entries[1].responded = true;
    ServerQuery q;
    q.setProvider(&p);
    q.begin(QueryKind::Internet);
    ServerListModel list;
    ServerFilter filter;
    filter.hideEmpty = true;
    list.setFilter(filter);
    p.response(1);
    q.pump();
    list.sync(q); // creates slot 0 placeholder
    p.response(0);
    q.pump();
    list.sync(q); // count stays 2
    expect(list.rowCount() == 1,
           "A04 late same-count row update must reapply hide-empty filter");
    bool keyMatches = true;
    for (int row = 0; row < list.rowCount(); ++row) {
      auto *e = q.entry(list.entryIndexAt(row));
      if (e && list.keyAt(row) != ServerListModel::keyOf(*e))
        keyMatches = false;
    }
    expect(keyMatches,
           "A04 late response must replace cached placeholder identity");
    p.complete();
    q.pump();
    q.refreshServer(1);
    p.Entries[1].name = "updated";
    p.response(1);
    q.pump();
    expect(q.entry(1)->name == "updated",
           "A05 refreshServer after completion must accept its response");
    FilterPair Filter{"gamedir", "svencoop"};
    q.begin(QueryKind::Internet, &Filter, 1);
    const uint64_t OldRequest = q.listRequest();
    q.cancel();
    q.refreshAll();
    expect(q.status() == QueryStatus::Querying,
           "A05 refresh after Stop must start querying again (UI calls "
           "refreshAll on retained handle)");
    expect(q.listRequest() != OldRequest && p.RequestedFilters.size() == 1 &&
               p.RequestedFilters[0].value == "svencoop",
           "F11 restart uses new provider tag and preserves filters");
    q.shutdown();
    q.setProvider(&p);
    expect(!q.begin(QueryKind::Internet),
           "A06 lifecycle witness: shutdown is terminal even after "
           "setProvider; module must recreate query");
  }
  {
    seedRecord();
    Record.AppliedUnixMs = Wall - VideoModeModel::kStaleRecordMs - 1;
    VideoModeModel model;
    model.setSeams(videoSeams());
    const auto r = model.resolveRecovery();
    expect(r != VideoModeModel::RecoveryResult::kAccepted,
           "A07 crashed unconfirmed mode must not become accepted solely by "
           "wall-clock age");
    expect(r == VideoModeModel::RecoveryResult::kAutoReverted &&
               Record.Pending == 0 && Commands.back() == "_restart\n",
           "F05 stale applied mode writes old settings and restarts");
  }
  {
    seedRecord();
    VideoModeModel model;
    model.setSeams(videoSeams());
    model.resolveRecovery();
    SaveOk = false;
    const bool reverted = model.revert();
    expect(!reverted && model.awaitingConfirm(),
           "A08 failed pending-record clear must not claim completed revert");
    const auto Issued = Commands.size();
    expect(!model.confirm(), "F06 cannot Keep a transaction already reverting");
    SaveOk = true;
    expect(model.revert() && !model.awaitingConfirm() &&
               Commands.size() == Issued,
           "F06 persistence retry does not issue a second restart");
  }
  {
    using namespace services::playerlist;
    PlayerListModel model;
    PlayerListModel::Seams s;
    s.roster = [](RosterEntry *out, int max) {
      if (max)
        out[0] = LivePlayer;
      return 1;
    };
    s.muteAvailable = []() { return true; };
    s.isMuted = [](const RosterEntry &) { return LiveMuted; };
    s.mute = [](const RosterEntry &) {
      ++MuteWrites;
      LiveMuted = true;
      return true;
    };
    s.unmute = [](const RosterEntry &) {
      ++MuteWrites;
      LiveMuted = false;
      return true;
    };
    model.setSeams(s);
    LivePlayer.index = 1;
    LivePlayer.steamId64 = 100;
    LivePlayer.name = "before";
    model.refresh();
    model.select(SelectionKey::forEntry(LivePlayer));
    // Roster changes after the last rendered snapshot, before the action.
    LivePlayer.steamId64 = 200;
    LivePlayer.name = "replacement";
    bool after = false;
    model.toggleMuted(after);
    expect(MuteWrites == 0, "A09 mute action must revalidate LIVE identity, "
                            "not only its cached roster");
  }

  {
    Provider Backend;
    ServerBrowserSession Session;
    Session.setProvider(&Backend);
    for (int i = 0; i < 2; ++i) {
      Session.startContext();
      expect(Session.Query.begin(QueryKind::Internet),
             "F03 query starts in each context cycle");
      Session.closeContext();
      expect(Session.Query.closed(), "F03 old context query is terminal");
    }
    Session.startContext();
    Session.Query.begin(QueryKind::Internet);
    QueryEvent Late;
    Late.request = Session.Query.listRequest();
    Late.index = 0;
    Session.closeContext();
    Session.startContext();
    Session.Query.begin(QueryKind::Internet);
    Backend.Events.push_back(Late);
    Session.Query.pump();
    expect(Session.Query.count() == 0 && Session.Query.staleEvents() == 1,
           "F03 late old-context callback cannot populate recreated query");
  }
  {
    seedRecord();
    Mono = 0;
    VideoModeModel Model;
    Model.setSeams(videoSeams());
    Model.resolveRecovery();
    int FramesPainted = 0;
    auto Frame = [&](bool Visible) {
      gameui::lifecycle::pumpMenuFrame([&] { Model.tick(); },
                                       [&] { return Visible; },
                                       [&] { ++FramesPainted; });
    };
    Frame(true);
    Mono = VideoModeModel::kConfirmWindowMs / 1000.0 + 1;
    Frame(false); // focus/DOM ownership lost; services still run
    expect(!Model.awaitingConfirm() && Record.Pending == 0 &&
               FramesPainted == 1,
           "F04 production frame scheduler reverts expired video while hidden");
  }
  {
    using namespace gameui::navigation;
    for (const char *Name :
         {"resume", "console-close", "cg-apply", "pl-mute", "sb-tab-internet",
          "sb-filter-empty", "video-revert", "password-connect",
          "bind-capture-begin"}) {
      const Action A = parseAction(Name);
      expect(
          A.Kind != ActionKind::kUnknown &&
              std::string(actionName(A.Kind)) == Name,
          "D02 fixed DOM intents decode and round-trip through typed routing");
    }
    expect(parseAction("video-revert; quit").Kind == ActionKind::kUnknown &&
               parseAction("pl-row:1:123:fake").Kind == ActionKind::kUnknown,
           "D02 arbitrary command text and serialized row identities are not "
           "actions");
    expect(parseAction("password-connect").Domain ==
                   ActionDomain::kConnection &&
               parseAction("video-keep").Domain == ActionDomain::kVideo,
           "D02 connection and video actions reach their own coordinators");
  }
  return abctest::finish("abc_ui_audit_regressions");
}
