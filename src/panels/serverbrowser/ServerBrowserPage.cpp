// panels::serverbrowser implementation - see ServerBrowserPage.h for the
// projection contract and the shipped-binary references behind the rules.
#include "ServerBrowserPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/serverbrowser/ServerListModel.h"

#include <abc/Console.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace panels::serverbrowser {

namespace {

using services::serverbrowser::QueryKind;
using services::serverbrowser::QueryStatus;
using services::serverbrowser::ServerEntry;

constexpr const char* kActionTabInternet = "sb-tab-internet";
constexpr const char* kActionTabLan = "sb-tab-lan";
constexpr const char* kActionTabFavorites = "sb-tab-favorites";
constexpr const char* kActionTabHistory = "sb-tab-history";
constexpr const char* kActionRefresh = "sb-refresh";
constexpr const char* kActionStop = "sb-stop";
constexpr const char* kActionFilterEmpty = "sb-filter-empty";
constexpr const char* kActionFilterFull = "sb-filter-full";
constexpr const char* kActionFilterSecure = "sb-filter-secure";
constexpr const char* kActionSort = "sb-sort";
constexpr const char* kActionRow = "sb-row";
constexpr const char* kActionPagePrev = "sb-page-prev";
constexpr const char* kActionPageNext = "sb-page-next";
constexpr const char* kActionConnect = "sb-connect";
constexpr const char* kActionFavoriteToggle = "sb-favorite-toggle";
constexpr const char* kActionReturn = "return";

Rml::Element* actionNode(Rml::Element* node) {
	for (Rml::Element* it = node; it; it = it->GetParentNode()) {
		if (it->HasAttribute("data-action"))
			return it;
	}
	return nullptr;
}

// Token resolution: our own table first; an unknown token passes through as
// raw text (visible honest fallback - never silently blanked, same rule the
// loading pages use for engine text).
std::string token(const char* name) {
	std::string out;
	if (framework::Localization::translate(out, name))
		return out;
	return std::string("#") + name;
}

std::string tokenValue(const char* name, int value) {
	std::string out;
	if (!framework::Localization::translate(out, name)) {
		out = std::string("#") + name;  // raw fallback keeps the count visible
	}
	const std::string marker = "%d";
	const auto at = out.find(marker);
	if (at != std::string::npos)
		out.replace(at, marker.size(), std::to_string(value));
	return out;
}

void setHtml(Rml::ElementDocument* doc, const char* id, const std::string& html) {
	if (!doc)
		return;
	if (Rml::Element* node = doc->GetElementById(id))
		node->SetInnerRML(html);
}

void setPressed(Rml::ElementDocument* doc, const char* id, bool on) {
	if (!doc)
		return;
	if (Rml::Element* node = doc->GetElementById(id))
		node->SetClass("sb-on", on);
}

void setDisabled(Rml::ElementDocument* doc, const char* id, bool disabled) {
	if (!doc)
		return;
	if (Rml::Element* node = doc->GetElementById(id))
		node->SetClass("sb-disabled", disabled);
}

// FNV-1a byte mixin for the projection signature.
void mix(std::size_t& hash, const void* data, std::size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}
}
template <typename T>
void mixValue(std::size_t& hash, const T& value) {
	mix(hash, &value, sizeof(T));
}
void mixText(std::size_t& hash, const std::string& text) {
	mix(hash, text.data(), text.size());
}

std::string playersCell(const ServerEntry& entry) {
	if (!entry.responded)
		return "?";
	char buffer[24];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d/%d", entry.players, entry.maxPlayers);
	return buffer;
}

std::string pingCell(const ServerEntry& entry) {
	if (!entry.responded || entry.pingMs < 0)
		return "-";
	char buffer[16];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d", entry.pingMs);
	return buffer;
}

std::string playTime(int seconds) {
	if (seconds < 0)
		seconds = 0;
	char buffer[24];
	if (seconds >= 3600)
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d:%02d:%02d", seconds / 3600,
			(seconds / 60) % 60, seconds % 60);
	else
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%d:%02d", seconds / 60, seconds % 60);
	return buffer;
}

const char* tabActionOf(QueryKind kind) {
	switch (kind) {
	case QueryKind::Internet:
		return kActionTabInternet;
	case QueryKind::Lan:
		return kActionTabLan;
	case QueryKind::Favorites:
		return kActionTabFavorites;
	case QueryKind::History:
		return kActionTabHistory;
	case QueryKind::Friends:
		return "sb-tab-friends";
	default:
		return kActionTabInternet;
	}
}

}

ServerBrowserPage::ServerBrowserPage()
	: InteractiveDocument("menuserverbrowser", "abcui/rml/serverbrowser.rml", true), Listener(this) {}

ServerBrowserPage::~ServerBrowserPage() {
	close();
}

void ServerBrowserPage::onDocumentReady() {
	if (Rml::ElementDocument* doc = document()) {
		addListener(doc, "click", &Listener);
		addListener(doc, "change", &Listener);
	}
	Projected = false;
	HaveSignature = false;
}

void ServerBrowserPage::tick() {
	if (!Query || !List || !document())
		return;
	if (List->sync(*Query))
		Projected = false;  // the view model moved: rebuild once
	const std::size_t signature = projectionSignature();
	if (Projected && HaveSignature && signature == LastSignature)
		return;
	render();
	LastSignature = signature;
	HaveSignature = true;
	Projected = true;
}

std::size_t ServerBrowserPage::projectionSignature() const {
	std::size_t hash = 1469598103934665603ULL;
	const auto& query = *Query;
	const auto& list = *List;
	mixValue(hash, static_cast<int>(query.status()));
	mixValue(hash, static_cast<int>(query.failure()));
	mixValue(hash, static_cast<int>(query.kind()));
	mixValue(hash, query.generation());
	mixValue(hash, query.count());
	mixValue(hash, query.noServersListed());
	mixValue(hash, static_cast<int>(query.detailKind()));
	mixValue(hash, static_cast<int>(query.detailStatus()));
	mixValue(hash, query.detailFailed());
	mixValue(hash, query.playerCount());
	mixValue(hash, list.rowCount());
	mixValue(hash, list.page());
	mixValue(hash, list.pageCount());
	mixValue(hash, static_cast<int>(list.sortColumn()));
	mixValue(hash, list.sortAscending());
	mixValue(hash, list.selectedKey());
	mixValue(hash, list.blockedOnly());
	mixValue(hash, FilterOpen);
	if (Blacklist) mixValue(hash, Blacklist->revision());
	const auto& filter = list.filter();
	mixValue(hash, filter.hideEmpty);
	mixValue(hash, filter.hideFull);
	mixValue(hash, filter.secureOnly);
	mixText(hash, filter.gameDir);
	mixText(hash, filter.Name);
	mixText(hash, filter.Map);
	mixValue(hash, filter.MaxPing);
	mixValue(hash, filter.NoPassword);
	mixValue(hash, filter.InsecureOnly);
	mixValue(hash, filter.HideUnresponsive);
	const int rows = services::serverbrowser::ServerListModel::kPageSize;
	for (int row = 0; row < rows; ++row) {
		const int index = list.entryIndexAt(row);
		if (index < 0)
			break;
		const ServerEntry* entry = query.entry(index);
		if (!entry)
			continue;
		mixValue(hash, list.keyAt(row));
		mixText(hash, entry->name);
		mixText(hash, entry->map);
		mixValue(hash, entry->players);
		mixValue(hash, entry->maxPlayers);
		mixValue(hash, entry->pingMs);
		mixValue(hash, entry->responded);
		mixValue(hash, entry->secure);
		mixValue(hash, entry->password);
		mixValue(hash, entry->botPlayers);
	}
	for (int i = 0; i < query.playerCount(); ++i) {
		const auto* player = query.player(i);
		if (!player)
			continue;
		mixText(hash, player->name);
		mixValue(hash, player->score);
	}
	// T18b: the selected server's favorite state is read-through (Steam is
	// the one truth); it must move the signature so a change re-projects even
	// when the list did not move.
	if (Favorites) {
		const ServerEntry* selected = list.selectedEntry(query);
		if (selected) {
			const auto key = services::serverbrowser::favoriteKeyOf(*selected);
			mixValue(hash, key.ip);
			mixValue(hash, key.connPort);
			mixValue(hash, key.queryPort);
			mixValue(hash, Favorites->isFavorite(key.ip, key.connPort, key.queryPort));
		}
	}
	return hash;
}

void ServerBrowserPage::render() {
	Rml::ElementDocument* doc = document();
	if (!doc || !Query || !List)
		return;
	const auto& query = *Query;
	const auto& list = *List;

	// --- status line --------------------------------------------------------
	std::string status;
	switch (query.status()) {
	case QueryStatus::Idle:
		break;
	case QueryStatus::Querying:
		status = tokenValue("SB_Querying", query.count());
		break;
	case QueryStatus::Completed:
		status = query.noServersListed() || query.count() == 0
			? token("SB_NoServers")
			: tokenValue("SB_Completed", query.count());
		break;
	case QueryStatus::Cancelled:
		status = tokenValue("SB_Cancelled", query.count());
		break;
	case QueryStatus::Failed:
		switch (query.failure()) {
		case services::serverbrowser::QueryFailure::Unavailable:
			status = token("SB_FailUnavailable");
			break;
		case services::serverbrowser::QueryFailure::Rejected:
			status = token("SB_FailRejected");
			break;
		case services::serverbrowser::QueryFailure::TimedOut:
			status = token("SB_FailTimedOut");
			break;
		case services::serverbrowser::QueryFailure::MasterNoResponse:
		default:
			status = token("SB_FailMaster");
			break;
		}
		break;
	}
	setHtml(doc, "sb-status", framework::TextUtil::escapeRml(status));

	// --- tabs / buttons / filters --------------------------------------------
	for (auto Kind : {QueryKind::Internet, QueryKind::Lan, QueryKind::Favorites, QueryKind::History, QueryKind::Friends}) {
		if (Rml::Element* tab = doc->GetElementById(tabActionOf(Kind)))
			tab->SetClass("sb-active", Kind == query.kind() && !list.blockedOnly());
	}
	setPressed(doc, "sb-filter-empty", list.filter().hideEmpty);
	setPressed(doc, "sb-filter-full", list.filter().hideFull);
	setPressed(doc, "sb-filter-secure", list.filter().secureOnly);
	// Stop only acts on a live query; Refresh acts in every state (the shipped
	// RefreshQuery is valid while querying AND after completion - T17 pin).
	setDisabled(doc, "sb-stop", query.status() != QueryStatus::Querying);

	// --- sort markers ---------------------------------------------------------
	const auto mark = [&](const char* id, services::serverbrowser::ServerSort column) {
		if (Rml::Element* node = doc->GetElementById(id)) {
			const bool active = list.sortColumn() == column;
			node->SetClass("sb-sort-asc", active && list.sortAscending());
			node->SetClass("sb-sort-desc", active && !list.sortAscending());
		}
	};
	mark("sb-col-name", services::serverbrowser::ServerSort::Name);
	mark("sb-col-map", services::serverbrowser::ServerSort::Map);
	mark("sb-col-players", services::serverbrowser::ServerSort::Players);
	mark("sb-col-ping", services::serverbrowser::ServerSort::Ping);

	// --- rows (the bounded page window) ----------------------------------------
	const int selectedRow = list.selectedRow();
	std::string rows;
	for (int row = 0; row < services::serverbrowser::ServerListModel::kPageSize; ++row) {
		const int index = list.entryIndexAt(row);
		if (index < 0)
			break;
		const ServerEntry* entry = query.entry(index);
		if (!entry)
			continue;
		rows += "<div class=\"sb-row";
		if (row == selectedRow)
			rows += " sb-selected";
		rows += "\" data-action=\"";
		rows += kActionRow;
		rows += "\" data-row=\"";
		rows += std::to_string(row);
		rows += "\"><span class=\"c-flags\">";
		if (entry->responded && entry->password)
			rows += "<img class=\"server-icon\" src=\"../img/server-lock.png\" title=\""
				+ framework::TextUtil::escapeRmlAttribute(token("SB_PasswordYes")) + "\"/>";
		if (entry->responded && entry->secure)
			rows += "<img class=\"server-icon\" src=\"../img/server-vac.png\" title=\""
				+ framework::TextUtil::escapeRmlAttribute(token("SB_SecureYes")) + "\"/>";
		rows += "</span><span class=\"c-bots\">" + (entry->responded ? std::to_string(entry->botPlayers) : "?")
			+ "</span><span class=\"c-name\">";
		rows += framework::TextUtil::escapeRml(entry->name);
		rows += "</span><span class=\"c-map\">";
		rows += framework::TextUtil::escapeRml(entry->map);
		rows += "</span><span class=\"c-players\">";
		rows += playersCell(*entry);
		rows += "</span><span class=\"c-ping\">";
		rows += pingCell(*entry);
		rows += "</span></div>";
	}
	if (rows.empty())
		rows = "<p class=\"sb-empty\">" + framework::TextUtil::escapeRml(token("SB_EmptyList"))
			+ "</p>";
	setHtml(doc, "sb-list", rows);

	// --- paging ------------------------------------------------------------------
	setDisabled(doc, "sb-page-prev", list.page() <= 0);
	setDisabled(doc, "sb-page-next", list.page() >= list.pageCount() - 1);
	char pageLabel[48];
	_snprintf_s(pageLabel, sizeof(pageLabel), _TRUNCATE, "%d / %d", list.page() + 1,
		list.pageCount());
	setHtml(doc, "sb-page-label", framework::TextUtil::escapeRml(pageLabel));

	// --- detail panel (the CURRENT single slot only) ------------------------------
	const ServerEntry* selected = list.selectedEntry(query);
	if (!selected) {
		setHtml(doc, "sb-detail-info",
			"<p class=\"sb-hint\">" + framework::TextUtil::escapeRml(token("SB_DetailHint"))
				+ "</p>");
		setHtml(doc, "sb-detail-players", "");
		setDisabled(doc, "sb-connect", true);
		setDisabled(doc, "sb-favorite-toggle", true);
	} else {
		std::string info = "<h2 id=\"sb-detail-name\">";
		info += framework::TextUtil::escapeRml(selected->name);
		info += "</h2><p class=\"sb-detail-line\">";
		info += framework::TextUtil::escapeRml(selected->map);
		info += " | ";
		info += framework::TextUtil::escapeRml(playersCell(*selected));
		info += " | ";
		info += framework::TextUtil::escapeRml(pingCell(*selected));
		if (selected->password) {
			info += " | ";
			info += framework::TextUtil::escapeRml(token("SB_PasswordYes"));
		}
		if (selected->secure) {
			info += " | ";
			info += framework::TextUtil::escapeRml(token("SB_SecureYes"));
		}
		info += "</p>";
		setHtml(doc, "sb-detail-info", info);
		setDisabled(doc, "sb-connect", list.blockedOnly() || !selected->responded);

		// The favorite state is READ from the authoritative store each
		// projection (never cached: Steam is the one truth; the toggle lands
		// through the module and the next tick re-reads).
		const auto key = services::serverbrowser::favoriteKeyOf(*selected);
		const bool isFavorite = Favorites && key.valid()
			&& Favorites->isFavorite(key.ip, key.connPort, key.queryPort);
		setDisabled(doc, "sb-favorite-toggle", !(Favorites && Favorites->available()));
		if (Rml::Element* fav = doc->GetElementById("sb-favorite-toggle"))
			fav->SetInnerRML(framework::TextUtil::escapeRml(
				token(isFavorite ? "SB_FavoriteRemove" : "SB_FavoriteAdd")));

		std::string players;
		if (query.detailKind() == services::serverbrowser::DetailKind::Players
			&& query.detailStatus() == QueryStatus::Querying)
			players = "<p class=\"sb-hint\">"
				+ framework::TextUtil::escapeRml(token("SB_DetailQuerying")) + "</p>";
		else if (query.detailKind() == services::serverbrowser::DetailKind::Players
			&& query.detailStatus() == QueryStatus::Failed)
			players = "<p class=\"sb-hint\">"
				+ framework::TextUtil::escapeRml(token("SB_DetailFailed")) + "</p>";
		else if (query.detailKind() == services::serverbrowser::DetailKind::Players
			&& query.detailStatus() == QueryStatus::Completed) {
			if (query.playerCount() == 0)
				players = "<p class=\"sb-hint\">"
					+ framework::TextUtil::escapeRml(token("SB_NoPlayers")) + "</p>";
			for (int i = 0; i < query.playerCount(); ++i) {
				const auto* player = query.player(i);
				if (!player)
					continue;
				players += "<div class=\"sb-player\"><span class=\"dp-name\">";
				players += framework::TextUtil::escapeRml(player->name);
				players += "</span><span class=\"dp-score\">";
				players += std::to_string(player->score);
				players += "</span><span class=\"dp-time\">";
				players += playTime(static_cast<int>(player->seconds));
				players += "</span></div>";
			}
		}
		setHtml(doc, "sb-detail-players", players);
	}
	renderControls();
}

void ServerBrowserPage::handleClick(Rml::Event& event) {
	if (handleControls(event))
		return;
	Rml::Element* entry = actionNode(event.GetTargetElement());
	if (!entry)
		return;
	const Rml::String action = entry->GetAttribute<Rml::String>("data-action", "");

	// Page-internal view state first: selection, sort, paging (no module trip;
	// the re-render is immediate, within this click).
	if (action == kActionRow) {
		if (!List || !Query)
			return;
		const Rml::String rowText = entry->GetAttribute<Rml::String>("data-row", "-1");
		List->selectRow(std::atoi(rowText.c_str()));
		const ServerEntry* selected = List->selectedEntry(*Query);
		if (selected)
			Query->requestPlayers(*selected);  // single-slot replace (T17 model)
		List->sync(*Query);
		render();
		return;
	}
	if (action == kActionSort) {
		if (!List)
			return;
		const Rml::String column = entry->GetAttribute<Rml::String>("data-column", "name");
		services::serverbrowser::ServerSort sort;
		if (column == "map")
			sort = services::serverbrowser::ServerSort::Map;
		else if (column == "players")
			sort = services::serverbrowser::ServerSort::Players;
		else if (column == "ping")
			sort = services::serverbrowser::ServerSort::Ping;
		else
			sort = services::serverbrowser::ServerSort::Name;
		if (List->sortColumn() == sort)
			List->setSort(sort, !List->sortAscending());
		else
			List->setSort(sort, true);
		render();
		return;
	}
	if (action == kActionPagePrev) {
		if (List && List->pagePrev())
			render();
		return;
	}
	if (action == kActionPageNext) {
		if (List && List->pageNext())
			render();
		return;
	}

	// Module intents (fixed names; the module owns the query lifecycle, the
	// filter truth and every engine/Steam mutation, the tick re-projects).
	if (action == kActionTabInternet || action == kActionTabLan || action == kActionTabFavorites
		|| action == kActionTabHistory || action == "sb-tab-friends" || action == kActionRefresh || action == kActionStop
		|| action == kActionFilterEmpty || action == kActionFilterFull
		|| action == kActionFilterSecure || action == kActionConnect
		|| action == kActionFavoriteToggle || action == kActionReturn) {
		const ActionSink Action = Sink;
		if (Action)
			Action(action.c_str());
	}
}

void ServerBrowserPage::PageListener::ProcessEvent(Rml::Event& event) {
	if (event.GetType() == "change")
		Owner->handleChange(event);
	else
		Owner->handleClick(event);
}

void ServerBrowserPage::refocusDefault() {
	if (Rml::Element* fallback = document() ? document()->GetElementById("sb-refresh") : nullptr)
		fallback->Focus();
}

}
