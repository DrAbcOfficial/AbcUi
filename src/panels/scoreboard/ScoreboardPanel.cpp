// panels::Scoreboard implementation - see ScoreboardPanel.h.
// from: ABCEnchance/src/hud/vgui/scoreboard.cpp (GBK->UTF-8).
// D-17 notes on the deliberate deviations:
// - The vgui SectionedListPanel item-id bookkeeping becomes a full table
//   rebuild per data version / 0.5s (visible only) - 32 rows of innerRML is
//   well inside budget and removes the old add/remove-item state machine.
// - The killer-highlight background needed a numeric DeathMsg feed the new
//   text-based killfeed does not carry; not ported (low signal, logged).
// - Sorting drops the "frags+deaths+ping == 0" pre-filter (the old
//   not-yet-updated heuristic); zero-score rows simply sort to the bottom.
// - Avatar textures resolve through data::avatars ("avatar://<id>", the
//   bundled default until steam delivers; the animated-GIF path and the
//   friend overlay need the steamapi backend's Friends surface).
#include <metahook.h>

#include "ScoreboardPanel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <abc/CvarCmd.h>

#include <keydefs.h>

// Windows.h min/max macros fight std::min/std::max (old repo precedent).
#undef max
#undef min

#include "../../data/Avatars.h"
#include "../../data/NetworkMessages.h"
#include "../../data/PlayerResource.h"
#include "../../data/TeamResource.h"
#include "../../framework/Commands.h"
#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../hooks.h"
#include "../../rmlui/RmlUiModule.h"
#include "../../rmlui/VGUI2ExtensionImport.h"

namespace panels {

namespace scoreboard {

// Old ctor defaults (persisted contract - never rename).
cvar_t* ShowAvatars = nullptr;
cvar_t* ShowLoss = nullptr;
cvar_t* ShowSteamId = nullptr;
cvar_t* ShowSteamIdType = nullptr;
cvar_t* ShowRealName = nullptr;
cvar_t* Size = nullptr;
cvar_t* SpacingNormal = nullptr;
cvar_t* SpacingCompact = nullptr;

void registerCvars() {
	ShowAvatars = abc::CREATE_CVAR("hud_scoreboard_showavatars", "1", abc::FCVAR_VALUE, nullptr);
	ShowLoss = abc::CREATE_CVAR("hud_scoreboard_showloss", "1", abc::FCVAR_VALUE, nullptr);
	ShowSteamId = abc::CREATE_CVAR("hud_scoreboard_showsteamid", "1", abc::FCVAR_VALUE, nullptr);
	ShowSteamIdType = abc::CREATE_CVAR("hud_scoreboard_showsteamidtype", "1", abc::FCVAR_VALUE, nullptr);
	ShowRealName = abc::CREATE_CVAR("hud_scoreboard_showrealname", "1", abc::FCVAR_VALUE, nullptr);
	Size = abc::CREATE_CVAR("hud_scoreboard_size", "0", abc::FCVAR_VALUE, nullptr);
	SpacingNormal = abc::CREATE_CVAR("hud_scoreboard_spacing_normal", "0", abc::FCVAR_VALUE, nullptr);
	SpacingCompact = abc::CREATE_CVAR("hud_scoreboard_spacing_compact", "0", abc::FCVAR_VALUE, nullptr);
}

ScoreboardPanel* PanelInstance = nullptr;

bool onKeyEvent(int down, int keynum) {
	return PanelInstance && PanelInstance->answerKey(down, keynum);
}

} // namespace scoreboard

namespace {

constexpr int kMouse2 = K_MOUSE2;

std::string localized(const char* token, const char* fallback) {
	std::string out;
	return framework::Localization::translate(out, token) ? out : fallback;
}

} // namespace

// Table mouse plumbing: row right-click opens the context menu, menu item
// clicks run their action, a click elsewhere closes it.
class ScoreboardPanel::TableListener final : public Rml::EventListener {
public:
	void ProcessEvent(Rml::Event& event) override {
		if (!Panel)
			return;
		const Rml::String type = event.GetType();
		if (type != "mousedown" && type != "click")
			return;
		const int button = event.GetParameter("button", 0);
		const Rml::Vector2f mouse(
			static_cast<float>(event.GetParameter("mouse_x", 0)),
			static_cast<float>(event.GetParameter("mouse_y", 0)));

		// Walk up to the element carrying data-action (menu item) or
		// data-client (row).
		for (Rml::Element* node = event.GetCurrentElement(); node; node = node->GetParentNode()) {
			if (node->HasAttribute("data-action")) {
				if (type == "click" && button == 0) {
					if (const Rml::Variant* value = node->GetAttribute("data-action")) {
						Panel->menuAction(value->Get<int>());
						Panel->closeContextMenu();
						event.StopPropagation();
						return;
					}
				}
			}
			if (node->HasAttribute("data-client")) {
				const Rml::Variant* value = node->GetAttribute("data-client");
				const int client = value ? value->Get<int>() : 0;
				if (type == "mousedown" && button == 1 && client > 0) {
					Panel->openContextMenu(client, mouse);
					event.StopPropagation();
					return;
				}
			}
		}
		if (type == "mousedown") {
			Panel->closeContextMenu();
		}
	}

	ScoreboardPanel* Panel = nullptr;
};

ScoreboardPanel::ScoreboardPanel()
	: HudPanel("scoreboard", "abcui/rml/scoreboard.rml") {
	scoreboard::PanelInstance = this;
}

void ScoreboardPanel::reset() {
	ServerName.clear();
	NextMap.clear();
	TimeEndCount = 0;
	MenuClient = 0;
	DataVersion++;
	if (Document) {
		closeContextMenu();
		setVisible(false);
		setMouseMode(false);
	}
}

void ScoreboardPanel::setVisible(bool visible) {
	if (visible == Visible)
		return;
	Visible = visible;
	if (Rml::Element* box = element("box"))
		box->SetProperty("display", visible ? "block" : "none");
	if (!visible)
		closeContextMenu();
	// Old ShowPanel: a fresh open refreshes everything immediately.
	if (visible) {
		DataVersion++;
		LastRefresh = -1.0;
	}
}

void ScoreboardPanel::setMouseMode(bool enabled) {
	if (enabled == MouseMode)
		return;
	MouseMode = enabled;
	if (enabled) {
		vgui2ext::centerAndUnlockCursor();
	} else {
		vgui2ext::lockCursor();
		closeContextMenu();
	}
}

bool ScoreboardPanel::answerKey(int down, int keynum) {
	if (!Visible || !down)
		return false;
	if (keynum == kMouse2 && !MouseMode) {
		// Old EnableMousePointer(true): cursor to window center + visible.
		setMouseMode(true);
		return true;
	}
	if (keynum == kMouse2 && MouseMode) {
		setMouseMode(false);
		return true;
	}
	return false;
}

void ScoreboardPanel::onDocumentReady() {
	if (abc::CChainedCommand* open = commands::get("+showscores")) {
		subscribe(open->Event(), [this]() {
			setVisible(true);
			return false;
		});
	}
	if (abc::CChainedCommand* close = commands::get("-showscores")) {
		subscribe(close->Event(), [this]() {
			setVisible(false);
			setMouseMode(false);
			return false;
		});
	}
	subscribe(netmsg::ServerName, [this](const char* name) {
		ServerName = name ? name : "";
		DataVersion++;
	});
	subscribe(netmsg::NextMap, [this](const char* map) {
		NextMap = map ? map : "";
		DataVersion++;
	});
	subscribe(netmsg::TimeEnd, [this](int seconds) {
		TimeEndCount = seconds;
		TimeEndBase = ClientTimeNow;
		DataVersion++;
	});
	subscribe(netmsg::ScoreInfo, [this](int, float, int, float, float, int, int, int) {
		DataVersion++;
	});

	if (!Listener) {
		Listener = new TableListener();
		Listener->Panel = this;
	}
	if (Rml::Element* box = element("box"))
		box->AddEventListener("mousedown", Listener, false);
	setVisible(false);
}

void ScoreboardPanel::updateHeader() {
	char buffer[128];
	if (Rml::Element* node = element("server-name"))
		node->SetInnerRML(framework::TextUtil::escapeRml(ServerName));
	{
		char base[64];
		const char* level = gEngfuncs.pfnGetLevelName();
		const char* slash = level ? (std::max)(strrchr(level, '/'), strrchr(level, '\\')) : nullptr;
		sprintf_s(base, "%s", slash ? slash + 1 : (level ? level : ""));
		if (Rml::Element* node = element("map-name"))
			node->SetInnerRML(framework::TextUtil::escapeRml(base));
	}
	{
		int connected = 0;
		for (int i = 1; i <= playerres::kMaxPlayers; i++) {
			if (const playerres::PlayerInfo* info = playerres::getPlayerInfo(i); info && info->isValid())
				connected++;
		}
		sprintf_s(buffer, "%d/%d", connected, gEngfuncs.GetMaxClients());
		if (Rml::Element* node = element("player-count"))
			node->SetInnerRML(buffer);
	}
	{
		const std::string label = localized("Scores_NextMap", "Next Map: ");
		sprintf_s(buffer, "%s%s", label.c_str(), NextMap.c_str());
		if (Rml::Element* node = element("next-map"))
			node->SetInnerRML(framework::TextUtil::escapeRml(buffer));
	}
	{
		const int remaining = std::max(0, TimeEndCount - static_cast<int>(ClientTimeNow - TimeEndBase));
		sprintf_s(buffer, "%.1d:%.2d", remaining / 60, remaining % 60);
		if (Rml::Element* node = element("time-end"))
			node->SetInnerRML(buffer);
	}
}

void ScoreboardPanel::rebuildTable() {
	Rml::Element* table = element("table");
	if (!table)
		return;

	// Group connected players by team, sort teams by score then players by
	// frags (old RefreshItems + StaticPlayerSortFuncByFrags, D-17 notes).
	struct RowRef {
		playerres::PlayerInfo* info;
	};
	std::map<teamres::TeamId, std::vector<RowRef>, std::greater<>> sections;
	for (int i = 1; i <= playerres::kMaxPlayers; i++) {
		playerres::PlayerInfo* info = playerres::getPlayerInfo(i);
		if (!info || !info->isValid())
			continue;
		info->update();
		sections[info->team].push_back({info});
	}

	struct TeamScore {
		int frags = 0;
		int deaths = 0;
	};
	std::map<teamres::TeamId, TeamScore> teamScores;
	for (auto& [team, rows] : sections) {
		for (RowRef& row : rows) {
			teamScores[team].frags += static_cast<int>(row.info->frags);
			teamScores[team].deaths += row.info->deaths;
		}
		std::sort(rows.begin(), rows.end(), [](const RowRef& a, const RowRef& b) {
			if (a.info->frags != b.info->frags)
				return a.info->frags > b.info->frags;
			if (a.info->deaths != b.info->deaths)
				return a.info->deaths < b.info->deaths;
			return a.info->index < b.info->index;
		});
	}
	// Team order: frags desc, deaths asc, id desc (old comparator).
	std::vector<teamres::TeamId> order;
	for (const auto& [team, rows] : sections)
		order.push_back(team);
	std::sort(order.begin(), order.end(), [&](teamres::TeamId a, teamres::TeamId b) {
		const TeamScore& sa = teamScores[a];
		const TeamScore& sb = teamScores[b];
		if (sa.frags != sb.frags)
			return sa.frags > sb.frags;
		if (sa.deaths != sb.deaths)
			return sa.deaths < sb.deaths;
		return a > b;
	});

	std::string rml;
	rml.reserve(8192);

	const bool showSteamId = scoreboard::ShowSteamId && scoreboard::ShowSteamId->value > 0;
	const bool showLoss = scoreboard::ShowLoss && scoreboard::ShowLoss->value > 0;
	const bool showRealName = scoreboard::ShowRealName && scoreboard::ShowRealName->value > 0;

	auto headerRow = [&](bool withLabels) {
		rml += "<div class='row header'>";
		rml += "<div class='cell c-avatar'></div><div class='cell c-donor'></div>";
		if (withLabels) {
			rml += "<div class='cell c-name'>";
			rml += framework::TextUtil::escapeRml(localized("Scores_PlayerName", "Name"));
			rml += "</div>";
			if (showSteamId) {
				rml += "<div class='cell c-steamid'>";
				rml += framework::TextUtil::escapeRml(localized("Scores_SteamID", "SteamID"));
				rml += "</div>";
			}
			rml += "<div class='cell c-health'>";
			rml += framework::TextUtil::escapeRml(localized("Scores_PlayerHealth", "HP"));
			rml += "</div><div class='cell c-armor'>";
			rml += framework::TextUtil::escapeRml(localized("Scores_PlayerArmor", "AP"));
			rml += "</div><div class='cell c-frags'>";
			rml += framework::TextUtil::escapeRml(localized("Scores_PlayerScore", "Frags"));
			rml += "</div><div class='cell c-deaths'>";
			rml += framework::TextUtil::escapeRml(localized("Scores_PlayerDeath", "Death"));
			rml += "</div><div class='cell c-ping'>";
			rml += framework::TextUtil::escapeRml(localized(showLoss ? "Scores_lPingLoss" : "Scores_Ping",
				showLoss ? "Ping/Loss" : "Ping"));
			rml += "</div>";
		} else {
			rml += "<div class='cell c-name'></div>";
			if (showSteamId)
				rml += "<div class='cell c-steamid'></div>";
			rml += "<div class='cell c-health'></div><div class='cell c-armor'></div>";
			rml += "<div class='cell c-frags'></div><div class='cell c-deaths'></div><div class='cell c-ping'></div>";
		}
		rml += "</div>";
	};
	headerRow(true);

	char buffer[128];
	const std::string na = localized("Scores_NA", "N/A");
	const std::string specTag = localized("Scores_PlayerSpec", "(Spectator)");
	const std::string deadTag = localized("Scores_PlayerHealthDead", "DEAD");

	auto appendDonorIcon = [&](const playerres::PlayerInfo& info) {
		const char* icon = "";
		switch (info.donor) {
			case playerres::Donor::ElectricCrowbar: icon = "donor_1"; break;
			case playerres::Donor::GoldedUzi: icon = "donor_2"; break;
			case playerres::Donor::GoldedDollar: icon = "donor_3"; break;
			case playerres::Donor::Tester: icon = "donor_4"; break;
			case playerres::Donor::Artist: icon = "donor_5"; break;
			case playerres::Donor::Developer: icon = "donor_6"; break;
			default: break;
		}
		if (info.admin == playerres::Admin::Operator)
			icon = "admin_icon32";
		else if (info.admin == playerres::Admin::ServerOwner)
			icon = "serverowner_icon32";
		if (!icon[0]) {
			rml += "<div class='cell c-donor'></div>";
			return;
		}
		sprintf_s(buffer, "<div class='cell c-donor'><img src='../img/%s.tga'/></div>", icon);
		rml += buffer;
	};

	for (teamres::TeamId team : order) {
		const std::vector<RowRef>& rows = sections[team];
		// Section header: team name (player share) + team score.
		{
			const char* token = teamres::teamNameToken(team);
			std::string teamName = localized(token, token);
			sprintf_s(buffer, "%s (%d/%d)", teamName.c_str(), static_cast<int>(rows.size()),
				static_cast<int>(sections.size()) > 0 ? teamScores[team].frags : 0);
			rml += "<div class='row section'>";
			rml += "<div class='cell c-avatar'></div><div class='cell c-donor'></div>";
			rml += "<div class='cell c-name'>";
			rml += framework::TextUtil::escapeRml(buffer);
			rml += "</div>";
			if (showSteamId)
				rml += "<div class='cell c-steamid'></div>";
			rml += "<div class='cell c-health'></div><div class='cell c-armor'></div>";
			sprintf_s(buffer, "%d", teamScores[team].frags);
			rml += "<div class='cell c-frags'>";
			rml += framework::TextUtil::escapeRml(buffer);
			rml += "</div>";
			sprintf_s(buffer, "%d", teamScores[team].deaths);
			rml += "<div class='cell c-deaths'>";
			rml += framework::TextUtil::escapeRml(buffer);
			rml += "</div><div class='cell c-ping'></div></div>";
		}

		for (const RowRef& row : rows) {
			const playerres::PlayerInfo& info = *row.info;
			char rowOpen[96];
			sprintf_s(rowOpen, "<div class='row%s' data-client='%d'>",
				info.isThisPlayer() ? " thisplayer" : "", info.index);
			rml += rowOpen;

			// Avatar (default until steam delivers; the scoreboard refresh
			// cache-busts ready ones so Rml re-resolves the texture).
			if (scoreboard::ShowAvatars && scoreboard::ShowAvatars->value > 0) {
				sprintf_s(buffer, "<div class='cell c-avatar'><img src='avatar://%llu'/></div>",
					static_cast<unsigned long long>(info.steamId64));
				rml += buffer;
			} else {
				rml += "<div class='cell c-avatar'></div>";
			}
			appendDonorIcon(info);

			// Name (+ real name when the backend can serve it).
			{
				std::string name = info.name;
				if (showRealName && !info.realName.empty()) {
					name += " (";
					name += info.realName;
					name += ")";
				}
				sprintf_s(buffer, "<div class='cell c-name'>%s</div>",
					framework::TextUtil::escapeRml(name).c_str());
				rml += buffer;
			}

			if (showSteamId) {
				char steam[32];
				sprintf_s(steam, "%llu", static_cast<unsigned long long>(info.steamId64));
				sprintf_s(buffer, "<div class='cell c-steamid'>%s</div>",
					framework::TextUtil::escapeRml(steam).c_str());
				rml += buffer;
			}

			// Health / armor (spectators show the tag; negative = dead).
			if (info.spectate) {
				sprintf_s(buffer, "<div class='cell c-health'>%s</div><div class='cell c-armor'>%s</div>",
					framework::TextUtil::escapeRml(specTag).c_str(), framework::TextUtil::escapeRml(na).c_str());
				rml += buffer;
			} else {
				if (info.health < 0) {
					sprintf_s(buffer, "<div class='cell c-health'>%s</div>",
						framework::TextUtil::escapeRml(deadTag).c_str());
				} else {
					sprintf_s(buffer, "<div class='cell c-health'>%d</div>", static_cast<int>(info.health));
				}
				rml += buffer;
				if (info.armor < 0) {
					sprintf_s(buffer, "<div class='cell c-armor'>%s</div>",
						framework::TextUtil::escapeRml(na).c_str());
				} else {
					sprintf_s(buffer, "<div class='cell c-armor'>%d</div>", static_cast<int>(info.armor));
				}
				rml += buffer;
			}

			sprintf_s(buffer, "<div class='cell c-frags'>%d</div><div class='cell c-deaths'>%d</div>",
				static_cast<int>(info.frags), info.deaths);
			rml += buffer;
			if (showLoss)
				sprintf_s(buffer, "<div class='cell c-ping'>%d/%d</div>", info.ping, info.loss);
			else
				sprintf_s(buffer, "<div class='cell c-ping'>%d</div>", info.ping);
			rml += buffer;
			rml += "</div>";
		}
	}

	table->SetInnerRML(rml);
}

void ScoreboardPanel::openContextMenu(int clientIndex, const Rml::Vector2f& position) {
	const playerres::PlayerInfo* info = playerres::getPlayerInfo(clientIndex);
	if (!info || !info->isValid())
		return;
	MenuClient = clientIndex;
	Rml::Element* menu = element("menu");
	if (!menu)
		return;

	char label[96];
	sprintf_s(label, "%llu", static_cast<unsigned long long>(info->steamId64));
	const bool hasSteamId = info->steamId64 != 0;
	struct Entry {
		const char* id;
		int action;
		const char* token;
		const char* fallback;
	};
	const Entry entries[] = {
		{"menu-profile", 0, "Scores_MenuSteamProfile", "Open Steam Profile"},
		{"menu-copy-url", 1, "Scores_MenuSteamURL", "Copy Profile URL"},
		{"menu-copy-name", 2, "Scores_MenuCopyName", "Copy Steam Name"},
		{"menu-copy-name-raw", 3, "Scores_MenuCopyNameRaw", "Copy Game Name"},
		{"menu-copy-steamid", 4, "Scores_MenuCopySteamID", "Copy SteamID"},
		{"menu-copy-steamid64", 5, "Scores_MenuCopySteamID64", "Copy SteamID64"},
	};
	for (const Entry& entry : entries) {
		if (Rml::Element* item = element(entry.id)) {
			item->SetInnerRML(framework::TextUtil::escapeRml(localized(entry.token, entry.fallback)));
			item->SetAttribute("data-action", std::to_string(entry.action));
		}
	}
	if (Rml::Element* item = element("menu-profile"))
		item->SetProperty("display", hasSteamId ? "block" : "none");
	if (Rml::Element* item = element("menu-copy-name"))
		item->SetProperty("display", info->realName.empty() ? "none" : "block");

	// Position at the pointer, clamped into the window (old
	// PositionRelativeToPanel CURSOR mode).
	Rml::Vector2f at = position;
	if (Rml::Context* context = rmlui::context()) {
		at.x = std::clamp(at.x, 0.0f, static_cast<float>(context->GetDimensions().x) - 180.0f);
		at.y = std::clamp(at.y, 0.0f, static_cast<float>(context->GetDimensions().y) - 96.0f);
	}
	menu->SetProperty("left", std::to_string(at.x) + "px");
	menu->SetProperty("top", std::to_string(at.y) + "px");
	menu->SetProperty("display", "block");
}

bool ScoreboardPanel::overMenu(const Rml::Vector2f& position) {
	Rml::Element* menu = element("menu");
	if (!menu || MenuClient == 0)
		return false;
	const Rml::Vector2f origin = menu->GetAbsoluteOffset();
	const Rml::Vector2f size = menu->GetBox().GetSize(Rml::BoxArea::Content);
	return position.x >= origin.x && position.x <= origin.x + size.x &&
		position.y >= origin.y && position.y <= origin.y + size.y;
}

void ScoreboardPanel::closeContextMenu() {
	MenuClient = 0;
	if (Rml::Element* menu = element("menu"))
		menu->SetProperty("display", "none");
}

void ScoreboardPanel::menuAction(int action) {
	const playerres::PlayerInfo* info = playerres::getPlayerInfo(MenuClient);
	if (!info || !info->isValid())
		return;

	auto copy = [](const std::string& text) {
		if (Rml::SystemInterface* system = Rml::GetSystemInterface())
			system->SetClipboardText(text);
	};
	switch (action) {
		case 0: { // steam profile (browser; the old overlay path needed
			// SteamFriends - the steamapi backend serves the browser fall-back
			// unconditionally, which the old code kept as its own fallback)
			char url[96];
			sprintf_s(url, "http://steamcommunity.com/profiles/%llu",
				static_cast<unsigned long long>(info->steamId64));
			ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}
		case 1: {
			char url[96];
			sprintf_s(url, "http://steamcommunity.com/profiles/%llu",
				static_cast<unsigned long long>(info->steamId64));
			copy(url);
			break;
		}
		case 2: copy(info->realName); break;
		case 3: copy(info->name); break;
		case 4: {
			char steamid[32];
			// Old m_pSteamId.Render(): STEAM_0:X:Y form from the 64-bit id.
			const std::uint64_t id = info->steamId64;
			const std::uint32_t accountId = static_cast<std::uint32_t>(id & 0xFFFFFFFFull);
			sprintf_s(steamid, "STEAM_0:%u:%u", accountId & 1, accountId >> 1);
			copy(steamid);
			break;
		}
		case 5: {
			char steamid[32];
			sprintf_s(steamid, "%llu", static_cast<unsigned long long>(info->steamId64));
			copy(steamid);
			break;
		}
		default: break;
	}
}

void ScoreboardPanel::onData(double frameTime) {
	ClientTimeNow += frameTime;
	if (!Document)
		return;

	// Mouse mode rides visibility: closing the scoreboard always re-locks.
	if (!Visible)
		return;

	updateHeader();
	// Old OnThink: 0.5s full refresh cadence; data-version bumps refresh
	// immediately. The table only rebuilds while shown (old
	// UpdateOnPlayerInfo IsVisible guard).
	if (DataVersion != AppliedVersion || ClientTimeNow - LastRefresh >= 0.5) {
		AppliedVersion = DataVersion;
		LastRefresh = ClientTimeNow;
		rebuildTable();
	}

	// Vertical centering (old Resize): the box hugs its content height.
	if (Rml::Context* context = rmlui::context()) {
		if (Rml::Element* box = element("box")) {
			const float contentHeight = box->GetBox().GetSize(Rml::BoxArea::Content).y;
			const float top = std::max(0.0f, (static_cast<float>(context->GetDimensions().y) - contentHeight) * 0.5f);
			box->SetProperty("top", std::to_string(top) + "px");
		}
	}
}

}
