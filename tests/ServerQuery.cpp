// T17 server-query model acceptance. Links the REAL production translation
// unit (src/services/serverbrowser/ServerQuery.cpp); the provider seam is a
// scripted fake that reproduces the events the Steam adapter emits (handle-
// tagged deliveries pushed from the callback thread). The adapter TU itself
// (ServerQuerySteam.cpp) binds steam_api.dll at runtime and is exercised live
// (T17c), not here.
//
// What is pinned here (05 doc tier 1 + tier 2):
//   - the lifecycle states and failure values: Idle / Querying / Completed /
//     Cancelled / Failed (Unavailable / Rejected / TimedOut /
//     MasterNoResponse);
//   - generation increments on every begin and on refreshAll; stale events
//     (old handle, post-cancel, post-completion) are dropped and counted and
//     can never overwrite newer results;
//   - cancel vs release are separate: cancel keeps the handle, release hands
//     it back; replacing a live request cancels+releases the predecessor;
//   - timeout: a query without RefreshComplete fails as TimedOut and is
//     cancelled (no silent hang);
//   - bounds: server index cap, one-slot detail queries with replacement;
//   - ping and players details: separate handles, stale-drop, failure states,
//     PlayersRefreshComplete vs PlayersFailedToRespond;
//   - shutdown releases every handle, latches the model closed, and late
//     events are drained-and-discarded.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/services/serverbrowser/ServerQuery.h"
#include "framework/TestHarness.h"

namespace {

using namespace services::serverbrowser;

uint64_t TestClock = 0;
uint64_t testClock() {
  return TestClock;
}

// The scripted provider: records every call, hands out handles from a
// counter, and lets the test push events exactly like the Steam callback
// objects do (the same handle tagging, owned payload copies).
class FakeProvider final : public IServerQueryProvider {
public:
  bool Available = true;
  int Outstanding = 0;
  uint64_t NextHandle = 100;

  std::vector<std::string> Calls;
  std::vector<uint64_t> Released;
  std::vector<uint64_t> Cancelled;
  std::vector<uint64_t> ServerCancelled;
  std::vector<std::pair<int, int>> RefreshServers;
  int RefreshAllCount = 0;

  struct ListState {
    uint64_t handle = 0;
    QueryKind kind = QueryKind::Internet;
    std::vector<FilterPair> filters;
    int count = 0;
    std::vector<ServerEntry> rows; // canned details per index
  };
  std::vector<ListState> Lists;
  uint64_t LastList = 0;

  // Detail canned data.
  std::vector<PlayerLine> CannedPlayers;
  ServerEntry CannedPing;

  // Queued deliveries (what the adapter's response objects would push).
  std::vector<QueryEvent> Queue;

  bool available() override {
    Calls.push_back("available");
    return Available;
  }

  uint64_t requestList(QueryKind kind, const FilterPair *filters,
                       int filterCount) override {
    Calls.push_back("requestList");
    ListState state;
    state.handle = NextHandle++;
    state.kind = kind;
    for (int i = 0; i < filterCount; ++i)
      state.filters.push_back(filters[i]);
    Lists.push_back(state);
    LastList = state.handle;
    ++Outstanding;
    return state.handle;
  }

  void cancelQuery(uint64_t request) override {
    Calls.push_back("cancelQuery");
    Cancelled.push_back(request);
  }

  void releaseRequest(uint64_t request) override {
    Calls.push_back("releaseRequest");
    Released.push_back(request);
    --Outstanding;
  }

  void refreshQueryAll(uint64_t request) override {
    Calls.push_back("refreshQueryAll");
    ++RefreshAllCount;
    (void)request;
  }

  void refreshServer(uint64_t request, int index) override {
    Calls.push_back("refreshServer");
    RefreshServers.emplace_back(static_cast<int>(request), index);
  }

  int serverCount(uint64_t) override {
    const ListState *list = find();
    return list ? static_cast<int>(list->rows.size()) : 0;
  }

  bool serverDetails(uint64_t, int index, ServerEntry &out) override {
    const ListState *list = find();
    if (!list || index < 0 || index >= static_cast<int>(list->rows.size()))
      return false;
    out = list->rows[index];
    return true;
  }

  uint64_t pingServer(uint32_t, uint16_t) override {
    Calls.push_back("pingServer");
    ++Outstanding;
    return NextHandle++;
  }

  uint64_t requestPlayers(uint32_t, uint16_t) override {
    Calls.push_back("requestPlayers");
    ++Outstanding;
    return NextHandle++;
  }

  void cancelServerQuery(uint64_t query) override {
    Calls.push_back("cancelServerQuery");
    ServerCancelled.push_back(query);
    --Outstanding;
  }

  int pollEvents(QueryEvent *out, int max) override {
    int count = 0;
    while (count < max && !Queue.empty()) {
      out[count++] = std::move(Queue.front());
      Queue.erase(Queue.begin());
    }
    return count;
  }

  int outstandingRequests() override {
    return Outstanding;
  }

  // --- test helpers -------------------------------------------------------

  void seedRow(int index, const char *name) {
    ListState &list = Lists.back();
    if (static_cast<int>(list.rows.size()) <= index)
      list.rows.resize(index + 1);
    list.rows[index].name = name;
    list.rows[index].responded = true;
  }

  void pushServerResponded(uint64_t handle, int index) {
    QueryEvent event;
    event.type = QueryEvent::Type::ServerResponded;
    event.request = handle;
    event.index = index;
    Queue.push_back(std::move(event));
  }

  void pushFailed(uint64_t handle, int index) {
    QueryEvent event;
    event.type = QueryEvent::Type::ServerFailedToRespond;
    event.request = handle;
    event.index = index;
    Queue.push_back(std::move(event));
  }

  void pushRefreshComplete(uint64_t handle, int response) {
    QueryEvent event;
    event.type = QueryEvent::Type::RefreshComplete;
    event.request = handle;
    event.response = response;
    Queue.push_back(std::move(event));
  }

  void pushPingResponded(uint64_t handle, const ServerEntry &server) {
    QueryEvent event;
    event.type = QueryEvent::Type::PingResponded;
    event.request = handle;
    event.server = server;
    Queue.push_back(std::move(event));
  }

  void pushPingFailed(uint64_t handle) {
    QueryEvent event;
    event.type = QueryEvent::Type::PingFailedToRespond;
    event.request = handle;
    event.failed = true;
    Queue.push_back(std::move(event));
  }

  void pushPlayers(uint64_t handle, const PlayerLine &line) {
    QueryEvent event;
    event.type = QueryEvent::Type::Players;
    event.request = handle;
    event.players.push_back(line);
    Queue.push_back(std::move(event));
  }

  void pushPlayersComplete(uint64_t handle, bool failed) {
    QueryEvent event;
    event.type = QueryEvent::Type::PlayersComplete;
    event.request = handle;
    event.failed = failed;
    Queue.push_back(std::move(event));
  }

private:
  ListState *find(uint64_t handle) {
    for (ListState &list : Lists) {
      if (list.handle == handle)
        return &list;
    }
    return nullptr;
  }
  ListState *find() {
    return find(LastList);
  }
};

void resetClock() {
  TestClock = 1000;
}

// --- 1. lifecycle: begin -> querying -> completed --------------------------

void testBasicLifecycle() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.status() == QueryStatus::Idle);
  CHECK(query.generation() == 0);

  FilterPair filter{"gamedir", "svencoop"};
  CHECK(query.begin(QueryKind::Internet, &filter, 1));
  CHECK(query.status() == QueryStatus::Querying);
  CHECK(query.generation() == 1);
  CHECK(query.listRequest() == provider.LastList);
  CHECK(provider.Lists.back().kind == QueryKind::Internet);
  CHECK(provider.Lists.back().filters.size() == 1 &&
        provider.Lists.back().filters[0].key == "gamedir");

  provider.seedRow(0, "server-A");
  provider.seedRow(1, "server-B");
  provider.pushServerResponded(provider.LastList, 0);
  provider.pushServerResponded(provider.LastList, 1);
  provider.pushRefreshComplete(provider.LastList, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);
  CHECK(query.failure() == QueryFailure::None);
  CHECK(query.count() == 2);
  CHECK(query.entry(0) && query.entry(0)->name == "server-A");
  CHECK(query.entry(1) && query.entry(1)->name == "server-B");
  CHECK(query.entry(2) == nullptr);
  CHECK(query.staleEvents() == 0);

  // Events after completion are stale: they cannot overwrite results.
  provider.seedRow(0, "overwritten");
  provider.pushServerResponded(provider.LastList, 0);
  query.pump();
  CHECK(query.entry(0)->name == "server-A");
  CHECK(query.staleEvents() == 1);
}

// --- 2. no servers listed --------------------------------------------------

void testNoServersListed() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Lan));
  CHECK(provider.Lists.back().kind == QueryKind::Lan);
  provider.pushRefreshComplete(provider.LastList,
                               2); // eNoServersListedOnMasterServer
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);
  CHECK(query.noServersListed());
  CHECK(query.count() == 0);
}

// --- 3. master failure -----------------------------------------------------

void testMasterFailure() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  provider.pushRefreshComplete(provider.LastList, 1); // eServerFailedToRespond
  query.pump();
  CHECK(query.status() == QueryStatus::Failed);
  CHECK(query.failure() == QueryFailure::MasterNoResponse);
}

// --- 4. unavailable / rejected fail closed ----------------------------------

void testFailClosed() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);

  provider.Available = false;
  CHECK(!query.begin(QueryKind::Internet));
  CHECK(query.status() == QueryStatus::Failed);
  CHECK(query.failure() == QueryFailure::Unavailable);
  CHECK(query.generation() == 1); // attempt still advanced the latch

  provider.Available = true;
  provider.NextHandle = 0; // provider refuses: requestList returns 0
  CHECK(!query.begin(QueryKind::Internet));
  CHECK(query.status() == QueryStatus::Failed);
  CHECK(query.failure() == QueryFailure::Rejected);
  CHECK(query.generation() == 2);

  // No provider at all: same fail-closed path.
  ServerQuery bare;
  CHECK(!bare.begin(QueryKind::Internet));
  CHECK(bare.failure() == QueryFailure::Unavailable);
}

// --- 5. generation + stale old-generation events ----------------------------

void testStaleAcrossGenerations() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  const uint64_t first = provider.LastList;
  CHECK(query.begin(QueryKind::Internet)); // replaces: cancel + release(1st)
  CHECK(query.generation() == 2);
  CHECK(provider.Cancelled.size() == 1 && provider.Cancelled[0] == first);
  CHECK(provider.Released.size() == 1 && provider.Released[0] == first);
  CHECK(provider.Outstanding == 1);

  // A late delivery from the OLD request must be dropped, not projected.
  provider.pushServerResponded(first, 0);
  query.pump();
  CHECK(query.count() == 0);
  CHECK(query.staleEvents() == 1);

  const uint64_t second = provider.LastList;
  provider.seedRow(0, "fresh");
  provider.pushServerResponded(second, 0);
  provider.pushRefreshComplete(second, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);
  CHECK(query.entry(0)->name == "fresh");
}

// --- 6. cancel vs release ---------------------------------------------------

void testCancelAndRelease() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  const uint64_t handle = provider.LastList;

  // Partial results before the cancel remain readable.
  provider.seedRow(0, "partial");
  provider.pushServerResponded(handle, 0);
  query.pump();
  CHECK(query.count() == 1);

  query.cancel();
  CHECK(query.status() == QueryStatus::Cancelled);
  CHECK(provider.Cancelled.size() == 1 && provider.Cancelled[0] == handle);
  CHECK(provider.Released.empty()); // cancel keeps the handle
  CHECK(provider.Outstanding == 1);

  // Events arriving after the cancel are stale.
  provider.pushServerResponded(handle, 1);
  query.pump();
  CHECK(query.staleEvents() == 1);
  CHECK(query.count() == 1);

  query.release();
  CHECK(provider.Released.size() == 1 && provider.Released[0] == handle);
  CHECK(provider.Outstanding == 0);
  CHECK(query.listRequest() == 0);

  // Idempotent: a second release is a no-op (no double free).
  query.release();
  CHECK(provider.Released.size() == 1);
}

// --- 7. timeout -------------------------------------------------------------

void testTimeout() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  query.setClock(&testClock);
  query.setTimeoutMs(1000);
  CHECK(query.begin(QueryKind::Internet));

  TestClock += 999;
  query.pump();
  CHECK(query.status() == QueryStatus::Querying); // not yet

  TestClock += 1;
  query.pump();
  CHECK(query.status() == QueryStatus::Failed);
  CHECK(query.failure() == QueryFailure::TimedOut);
  CHECK(provider.Cancelled.size() == 1); // the timed-out query is cancelled
  CHECK(provider.Released.empty());      // but the handle stays for release

  // A late RefreshComplete after the timeout is stale.
  provider.pushRefreshComplete(provider.LastList, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Failed);
  CHECK(query.staleEvents() == 1);
  query.release();
  CHECK(provider.Outstanding == 0);
}

// --- 8. refresh paths -------------------------------------------------------

void testRefresh() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  const uint64_t handle = provider.LastList;
  provider.seedRow(0, "A");
  provider.seedRow(1, "B");
  provider.pushServerResponded(handle, 0);
  provider.pushServerResponded(handle, 1);
  provider.pushRefreshComplete(handle, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);
  CHECK(query.generation() == 1);

  // refreshAll on a completed list re-pings the whole list (the shipped
  // Refresh button path: serverbrowser.dll 0x10005230 -> slot 0x24) and
  // returns to Querying until the new RefreshComplete arrives.
  query.refreshAll();
  CHECK(provider.RefreshAllCount == 1);
  CHECK(query.status() == QueryStatus::Querying);
  CHECK(query.generation() == 2); // refresh advanced the generation
  provider.pushRefreshComplete(handle, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);

  CHECK(query.begin(QueryKind::Favorites));
  const uint64_t second = provider.LastList;
  CHECK(query.generation() == 3); // the begin itself advanced it
  query.refreshAll();
  CHECK(provider.RefreshAllCount == 2);
  CHECK(query.generation() == 4); // refreshAll advances again (UI reset)
  CHECK(query.status() == QueryStatus::Querying); // back to live

  // refreshServer stays valid while a (pre-refresh) snapshot exists.
  provider.seedRow(0, "A");
  provider.pushServerResponded(second, 0);
  query.pump();
  CHECK(query.refreshServer(0));
  CHECK(query.refreshServer(99) == false); // out of range refused
  CHECK(provider.RefreshServers.size() == 1 &&
        provider.RefreshServers[0].first == static_cast<int>(second) &&
        provider.RefreshServers[0].second == 0);
  query.release();
}

// --- 9. bounds --------------------------------------------------------------

void testBounds() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  const uint64_t handle = provider.LastList;

  // An out-of-range index is dropped and counted, never resized unbounded.
  provider.pushServerResponded(handle, kMaxServers + 10);
  query.pump();
  CHECK(query.count() == 0);
  CHECK(query.droppedServers() == 1);

  // fillMissingDetails truncates at the cap too (serverCount > cap).
  provider.Lists.back().rows.resize(kMaxServers + 5);
  provider.pushRefreshComplete(handle, 0);
  query.pump();
  CHECK(query.status() == QueryStatus::Completed);
  CHECK(query.count() == kMaxServers);
  CHECK(query.droppedServers() == 6); // 5 overflow + the earlier 1
}

// --- 10. ping details -------------------------------------------------------

void testPing() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);

  ServerEntry server;
  server.ip = 0x7f000001;
  server.queryPort = 27015;

  CHECK(query.requestPing(server) != 0);
  CHECK(query.detailKind() == DetailKind::Ping);
  CHECK(query.detailStatus() == QueryStatus::Querying);
  CHECK(provider.Calls.back() == "pingServer");
  const uint64_t ping = query.detailRequest();

  provider.CannedPing.name = "pinged";
  provider.CannedPing.map = "osprey";
  provider.pushPingResponded(ping, provider.CannedPing);
  query.pump();
  CHECK(query.detailStatus() == QueryStatus::Completed);
  CHECK(query.pingResult() && query.pingResult()->name == "pinged" &&
        query.pingResult()->map == "osprey");

  // A second ping replaces the first: cancelServerQuery on the old handle.
  CHECK(query.requestPing(server) != 0);
  CHECK(provider.ServerCancelled.size() == 1 &&
        provider.ServerCancelled[0] == ping);
  const uint64_t second = query.detailRequest();
  CHECK(second != ping);

  // Late event from the replaced ping: stale.
  provider.pushPingResponded(ping, provider.CannedPing);
  query.pump();
  CHECK(query.staleEvents() == 1);

  // Failure state.
  provider.pushPingFailed(second);
  query.pump();
  CHECK(query.detailStatus() == QueryStatus::Failed);
  CHECK(query.detailFailed());

  query.cancelDetail();
  CHECK(query.detailRequest() == 0);
}

// --- 11. players details ----------------------------------------------------

void testPlayers() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  ServerEntry server;
  server.ip = 0x0a000001;
  server.connectionPort = 27016; // queryPort absent: falls back to it

  CHECK(query.requestPlayers(server) != 0);
  CHECK(query.detailKind() == DetailKind::Players);
  const uint64_t players = query.detailRequest();

  provider.pushPlayers(players, PlayerLine{"alice", 10, 12.5f});
  provider.pushPlayers(players, PlayerLine{"bob", 3, 4.0f});
  query.pump();
  CHECK(query.playerCount() == 2);
  CHECK(query.player(0)->name == "alice" && query.player(0)->score == 10);
  CHECK(query.detailStatus() == QueryStatus::Querying); // no complete yet

  provider.pushPlayersComplete(players, false);
  query.pump();
  CHECK(query.detailStatus() == QueryStatus::Completed);
  CHECK(!query.detailFailed());

  // Bounded: more lines than the cap are refused at insertion.
  for (int i = 0; i < kMaxPlayers + 10; ++i)
    provider.pushPlayers(players, PlayerLine{"flood", 0, 0.0f});
  CHECK(query.requestPlayers(server) != 0); // new handle replaces
  const uint64_t flood = query.detailRequest();
  for (int i = 0; i < kMaxPlayers + 10; ++i)
    provider.pushPlayers(flood, PlayerLine{"flood", 0, 0.0f});
  query.pump();
  CHECK(query.playerCount() == kMaxPlayers);

  // Failed completion.
  provider.pushPlayersComplete(flood, true);
  query.pump();
  CHECK(query.detailStatus() == QueryStatus::Failed);
  CHECK(query.detailFailed());

  // Stale: a players event from an old handle is dropped.
  query.cancelDetail();
  provider.pushPlayers(flood, PlayerLine{"late", 0, 0.0f});
  provider.pushPlayersComplete(players, false);
  query.pump();
  CHECK(query.staleEvents() >= 2);
}

// --- 12. shutdown -----------------------------------------------------------

void testShutdown() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  const uint64_t handle = provider.LastList;
  ServerEntry server;
  server.ip = 0x7f000001;
  CHECK(query.requestPing(server) != 0);
  const uint64_t ping = query.detailRequest();
  CHECK(provider.Outstanding == 2);

  query.shutdown();
  CHECK(query.closed());
  CHECK(provider.Outstanding == 0); // every handle handed back
  CHECK(provider.ServerCancelled.size() == 1 &&
        provider.ServerCancelled[0] == ping);
  CHECK(provider.Cancelled.size() == 1 && provider.Cancelled[0] == handle);
  CHECK(provider.Released.size() == 1 && provider.Released[0] == handle);
  CHECK(query.status() == QueryStatus::Idle);
  CHECK(query.count() == 0);

  // Latched closed: no new request, and late events drain-and-discard.
  CHECK(!query.begin(QueryKind::Internet));
  provider.pushServerResponded(handle, 0);
  provider.pushPingResponded(ping, server);
  query.pump();
  CHECK(query.staleEvents() == 2);
  // Shutdown is idempotent.
  query.shutdown();
  query.pump();
  CHECK(query.closed());
}

// --- 13. page-leave pattern: release releases, no projector -----------------

void testPageLeaveRelease() {
  resetClock();
  FakeProvider provider;
  ServerQuery query;
  query.setProvider(&provider);
  CHECK(query.begin(QueryKind::Internet));
  provider.seedRow(0, "kept-until-leave");
  provider.pushServerResponded(provider.LastList, 0);
  query.pump();
  CHECK(query.count() == 1);

  // Page leave: release the handle; the snapshot stays for the re-open to
  // decide (T18 re-begins; the model does not silently wipe results).
  // A still-live query is cancelled first (release implies cancel), so the
  // status moves to Cancelled while the partial snapshot stays readable.
  query.release();
  CHECK(query.count() == 1);
  CHECK(provider.Outstanding == 0);
  CHECK(query.status() == QueryStatus::Cancelled);

  // A later begin replaces cleanly (no handle leak on the second open).
  CHECK(query.begin(QueryKind::History));
  CHECK(query.generation() == 2);
  query.release();
  CHECK(provider.Outstanding == 0);
}

} // namespace

int main() {
  testBasicLifecycle();
  testNoServersListed();
  testMasterFailure();
  testFailClosed();
  testStaleAcrossGenerations();
  testCancelAndRelease();
  testTimeout();
  testRefresh();
  testBounds();
  testPing();
  testPlayers();
  testShutdown();
  testPageLeaveRelease();
  std::cout << "abc_ui_server_query: all sections passed\n";
  return abctest::finish("abc_ui_server_query");
}
