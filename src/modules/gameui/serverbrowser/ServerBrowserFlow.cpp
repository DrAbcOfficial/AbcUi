// Extracted from GameUiModule.cpp (UTF-8). Responsibility: serverbrowser/ServerBrowserFlow.
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <string>
#include "modules/gameui/navigation/MenuAction.h"
namespace gameui {
using namespace navigation;
void GameUiModule::presentServerBrowserPage() {
	if (!Browser)
		return;
	auto Filter = ServerBrowser.List.filter();
	Filter.HideUnresponsive = abc::CVAR_GET_FLOAT("abc_ui_browser_hide_unresponsive") != 0;
	ServerBrowser.List.setFilter(Filter);
	Browser->show();
	ServerBrowser.Presented = true;
}


void GameUiModule::beginServerQuery(services::serverbrowser::QueryKind kind) {
	if (ServerBrowser.Query.closed())
		return;
	services::serverbrowser::FilterPair pairs[8];
	const int count = ServerBrowser.List.filter().buildPairs(pairs, 8);
	ServerBrowser.Query.begin(kind, count > 0 ? pairs : nullptr, count);
	logServerQueryChange();
}


void GameUiModule::connectSelectedServer() {
	const auto* selected = ServerBrowser.List.selectedEntry(ServerBrowser.Query);
	if (!selected || !selected->responded) {
		abc::ConsoleWarn("[AbcUi] server connect refused (no responded selection)\n");
		return;
	}
	if (Blacklist.contains(selected->ip, selected->connectionPort ? selected->connectionPort : selected->queryPort)) {
		abc::ConsoleWarn("[AbcUi] blocked server must be removed from the blacklist before connecting\n");
		return;
	}
	const std::string address = services::serverbrowser::buildConnectAddress(*selected);
	std::string host;
	int port = 0;
	if (address.empty() || !services::loading::LoadingModel::parseAddress(address.c_str(), host, port)) {
		abc::ConsoleWarn("[AbcUi] server connect refused (unrepresentable address)\n");
		return;
	}
	// The join-success memory entry (the shipped ConnectedToGame records
	// flags=2 into Steam's own store; see ServerFavorites.h). Kept for the
	// live attempt only - a cancel/failure retires it.
	PendingHistory = services::serverbrowser::favoriteKeyOf(*selected);
	PendingHistoryValid = PendingHistory.valid();
	Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
	finishDispatch();
}


void GameUiModule::toggleSelectedFavorite() {
	const auto* selected = ServerBrowser.List.selectedEntry(ServerBrowser.Query);
	if (!selected) {
		abc::ConsoleWarn("[AbcUi] favorite toggle refused (no selection)\n");
		return;
	}
	auto* favorites = services::serverbrowser::steamFavorites();
	const auto key = services::serverbrowser::favoriteKeyOf(*selected);
	if (!key.valid()) {
		abc::ConsoleWarn("[AbcUi] favorite toggle refused (unrepresentable address)\n");
		return;
	}
	const bool wasFavorite = favorites->isFavorite(key.ip, key.connPort, key.queryPort);
	const bool ok = wasFavorite
		? favorites->removeFavorite(key.ip, key.connPort, key.queryPort)
		: favorites->addFavorite(key.ip, key.connPort, key.queryPort);
	abc::ConsolePrint("[AbcUi] favorite %s \"%s\": %s (count=%d)\n",
		wasFavorite ? "remove" : "add", selected->name.c_str(), ok ? "OK" : "REFUSED",
		favorites->favoriteCount());
	if (Browser)
		Browser->tick();  // re-project the button state from the store
}


void GameUiModule::releaseServerPageQueries() {
	ServerBrowser.Query.cancelDetail();
	ServerBrowser.Query.release();
	ServerBrowser.Open = false;
}


void GameUiModule::logServerQueryChange() {
	using services::serverbrowser::QueryStatus;
	const QueryStatus status = ServerBrowser.Query.status();
	if (status == ServerBrowser.LastStatus)
		return;
	ServerBrowser.LastStatus = status;
	switch (status) {
	case QueryStatus::Querying:
		abc::ConsolePrint("[AbcUi] server query started: kind=%d gen=%u\n",
			static_cast<int>(ServerBrowser.Query.kind()), ServerBrowser.Query.generation());
		break;
	case QueryStatus::Completed:
		abc::ConsolePrint("[AbcUi] server query completed: gen=%u count=%d noServers=%d\n",
			ServerBrowser.Query.generation(), ServerBrowser.Query.count(),
			ServerBrowser.Query.noServersListed() ? 1 : 0);
		break;
	case QueryStatus::Cancelled:
		abc::ConsolePrint("[AbcUi] server query cancelled: gen=%u partial=%d\n",
			ServerBrowser.Query.generation(), ServerBrowser.Query.count());
		break;
	case QueryStatus::Failed:
		abc::ConsolePrint("[AbcUi] server query failed: gen=%u failure=%d\n",
			ServerBrowser.Query.generation(), static_cast<int>(ServerBrowser.Query.failure()));
		break;
	default:
		break;
	}
}


void GameUiModule::recordPendingHistory() {
	if (!PendingHistoryValid)
		return;
	PendingHistoryValid = false;
	auto* favorites = services::serverbrowser::steamFavorites();
	const bool ok = favorites->addHistory(PendingHistory.ip, PendingHistory.connPort,
		PendingHistory.queryPort);
	abc::ConsolePrint("[AbcUi] history record %u.%u.%u.%u:%u: %s\n",
		PendingHistory.ip >> 24, (PendingHistory.ip >> 16) & 0xff,
		(PendingHistory.ip >> 8) & 0xff, PendingHistory.ip & 0xff, PendingHistory.connPort,
		ok ? "OK" : "FAILED");
}

void GameUiModule::handleServerAction(ActionKind Kind) {
	// T18 browser intents (fixed page-internal names only): the query
	// lifecycle entries. Row/sort/paging never reach here (the page
	// resolves them against its models directly).
	using services::serverbrowser::QueryKind;
	if (Kind == ActionKind::kSbTabInternet)
		beginServerQuery(QueryKind::Internet);
	else if (Kind == ActionKind::kSbTabLan)
		beginServerQuery(QueryKind::Lan);
	else if (Kind == ActionKind::kSbTabFavorites)
		beginServerQuery(QueryKind::Favorites);
	else if (Kind == ActionKind::kSbTabHistory)
		beginServerQuery(QueryKind::History);
	else if (Kind == ActionKind::kSbTabFriends)
		beginServerQuery(QueryKind::Friends);
	else if (Kind == ActionKind::kSbFiltersApply)
		beginServerQuery(ServerBrowser.Query.kind());
	else if (Kind == ActionKind::kSbRefresh) {
		// The shipped Refresh button semantics (T17 pin): RefreshQuery
		// on a live handle; a fresh request when nothing is live.
		if (ServerBrowser.Query.listRequest() != 0)
			ServerBrowser.Query.refreshAll();
		else
			beginServerQuery(ServerBrowser.Query.kind());
		logServerQueryChange();
	}
	else if (Kind == ActionKind::kSbStop) {
		ServerBrowser.Query.cancel();
		logServerQueryChange();
	}
	else if (Kind == ActionKind::kSbFilterEmpty || Kind == ActionKind::kSbFilterFull
		|| Kind == ActionKind::kSbFilterSecure) {
		// The view model owns the filter truth; a toggle re-issues the
		// list query with the new master pairs (the original's filter
		// apply). Client-side the same rules re-project immediately.
		services::serverbrowser::ServerFilter filter = ServerBrowser.List.filter();
		if (Kind == ActionKind::kSbFilterEmpty)
			filter.hideEmpty = !filter.hideEmpty;
		else if (Kind == ActionKind::kSbFilterFull)
			filter.hideFull = !filter.hideFull;
		else
			filter.secureOnly = !filter.secureOnly;
		ServerBrowser.List.setFilter(filter);
		beginServerQuery(ServerBrowser.Query.kind());
	}
	else if (Kind == ActionKind::kSbConnect) {
		connectSelectedServer();
		return;
	}
	else if (Kind == ActionKind::kSbFavoriteToggle) {
		toggleSelectedFavorite();
		return;
	}
	else {
		abc::ConsoleWarn("[AbcUi] unknown menu action \"%s\" dropped\n", actionName(Kind));
		return;
	}
	finishDispatch();
	return;

}

}
