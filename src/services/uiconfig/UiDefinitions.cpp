#include "UiDefinitions.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace services::uiconfig {
namespace {
using Value = rapidjson::Value;

struct ValidationError {
	std::string Field;
	std::string Message;
};

void require(bool Valid, const std::string& Field, const char* Message) {
	if (!Valid)
		throw ValidationError{Field, Message};
}

bool identifier(const std::string& Text) {
	return !Text.empty() && Text.size() <= 96
		&& std::all_of(Text.begin(), Text.end(), [](unsigned char C) {
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
				|| (C >= '0' && C <= '9') || C == '_' || C == '-';
		});
}

void fields(const Value& Object, std::initializer_list<const char*> Allowed, const std::string& Path) {
	require(Object.IsObject(), Path, "expected object");
	std::set<std::string> Seen;
	for (auto It = Object.MemberBegin(); It != Object.MemberEnd(); ++It) {
		std::string Key(It->name.GetString(), It->name.GetStringLength());
		require(Seen.insert(Key).second, Path + "." + Key, "duplicate field");
		require(std::any_of(Allowed.begin(), Allowed.end(),
			[&](const char* Name) { return Key == Name; }), Path + "." + Key, "unknown field");
	}
}

const Value& member(const Value& Object, const char* Key, const std::string& Path) {
	require(Object.HasMember(Key), Path + "." + Key, "required field missing");
	return Object[Key];
}

std::string text(const Value& Object, const char* Key, const std::string& Path, bool Empty = false) {
	const auto& V = member(Object, Key, Path);
	require(V.IsString(), Path + "." + Key, "expected string");
	std::string Result(V.GetString(), V.GetStringLength());
	require((Empty || !Result.empty()) && Result.size() <= 1024
		&& Result.find('\0') == std::string::npos, Path + "." + Key, "invalid string length or NUL");
	return Result;
}

float number(const Value& Object, const char* Key, const std::string& Path) {
	const auto& V = member(Object, Key, Path);
	require(V.IsNumber(), Path + "." + Key, "expected finite number");
	float Result = static_cast<float>(V.GetDouble());
	require(std::isfinite(Result), Path + "." + Key, "number exceeds float range");
	return Result;
}

void array(const Value& V, const std::string& Path, unsigned Limit) {
	require(V.IsArray(), Path, "expected array");
	require(V.Size() <= Limit, Path, "too many entries");
}

rapidjson::Document document(const std::string& Text) {
	require(Text.size() <= 1024 * 1024, "$", "file exceeds 1 MiB");
	require(Text.find('\0') == std::string::npos, "$", "unexpected NUL byte");
	rapidjson::Document Doc;
	Doc.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(Text.data(), Text.size());
	if (Doc.HasParseError())
		throw ValidationError{"$@" + std::to_string(Doc.GetErrorOffset()), rapidjson::GetParseError_En(Doc.GetParseError())};
	require(Doc.IsObject(), "$", "expected object");
	const auto& Version = member(Doc, "version", "$");
	require(Version.IsInt() && Version.GetInt() == 1, "$.version", "unsupported version");
	return Doc;
}

std::vector<MenuEntry> menuEntries(const Value& V, const std::string& Path, std::set<std::string>& Ids) {
	array(V, Path, 64);
	std::vector<MenuEntry> Entries;
	for (unsigned I = 0; I < V.Size(); ++I) {
		const std::string P = Path + "[" + std::to_string(I) + "]";
		const auto& Item = V[I];
		fields(Item, {"id", "label", "action", "condition"}, P);
		MenuEntry Entry;
		Entry.Id = text(Item, "id", P);
		Entry.Label = text(Item, "label", P);
		Entry.Action = text(Item, "action", P);
		require(identifier(Entry.Id) && Ids.insert(Entry.Id).second, P + ".id", "invalid or duplicate id");
		// Only home-menu intents, never dialog confirmations or commands.
		const std::set<std::string> Actions = {"resume", "disconnect", "quit", "settings",
			"servers", "creategame", "playerlist", "console", "directconnect"};
		require(Actions.count(Entry.Action) != 0, P + ".action", "unregistered home-menu action");
		if (Item.HasMember("condition"))
			Entry.Condition = text(Item, "condition", P);
		require(Entry.Condition == "always" || Entry.Condition == "multiplayer"
			|| Entry.Condition == "singleplayer", P + ".condition", "unknown visibility condition");
		Entries.push_back(std::move(Entry));
	}
	require(!Entries.empty(), Path, "section must not be empty");
	return Entries;
}

SettingDefinition setting(const Value& Item, const std::string& P) {
	fields(Item, {"cvar", "label", "section", "kind", "min", "max", "step", "default", "options", "source"}, P);
	SettingDefinition Row;
	Row.Cvar = text(Item, "cvar", P);
	require(identifier(Row.Cvar), P + ".cvar", "invalid cvar identifier");
	Row.Label = text(Item, "label", P);
	Row.Section = Item.HasMember("section") ? text(Item, "section", P) : "";
	const auto Kind = text(Item, "kind", P);
	using K = settings::SettingKind;
	if (Kind == "bool") Row.Kind = K::kBool;
	else if (Kind == "float") Row.Kind = K::kFloat;
	else if (Kind == "enum") Row.Kind = K::kEnum;
	else if (Kind == "negate") Row.Kind = K::kNegate;
	else if (Kind == "text") Row.Kind = K::kText;
	else if (Kind == "string-enum") Row.Kind = K::kStringEnum;
	else if (Kind == "command-toggle") Row.Kind = K::kCommandToggle;
	else require(false, P + ".kind", "unknown setting kind");
	const bool String = Row.Kind == K::kText || Row.Kind == K::kStringEnum;
	if (String || Row.Kind == K::kBool || Row.Kind == K::kCommandToggle)
		require(!Item.HasMember("min") && !Item.HasMember("max") && !Item.HasMember("step"),
			P, "min/max/step are only valid for float, enum and negate");
	if (String) {
		Row.DefaultString = text(Item, "default", P, true);
		require(std::all_of(Row.DefaultString.begin(), Row.DefaultString.end(), [](unsigned char C) {
			return C >= 0x20 && C != 0x7f && C != '"' && C != '\\' && C != '`' && C != ';';
		}), P + ".default", "unsafe string default");
		if (Row.Kind == K::kText)
			require(!Row.DefaultString.empty() && Row.DefaultString.size() <= settings::kNameMaxBytes,
				P + ".default", "text default must be 1..63 bytes");
		Row.Min = Row.Max = Row.Step = 0;
	} else {
		Row.DefaultValue = number(Item, "default", P);
		if (Row.Kind != K::kBool && Row.Kind != K::kCommandToggle) {
			Row.Min = number(Item, "min", P);
			Row.Max = number(Item, "max", P);
			Row.Step = Item.HasMember("step") ? number(Item, "step", P) : 0;
		}
		require(Row.Min <= Row.Max && Row.Step >= 0, P, "invalid numeric domain");
		require(Row.DefaultValue >= Row.Min && Row.DefaultValue <= Row.Max, P + ".default", "default outside domain");
		if (Row.Kind == K::kBool || Row.Kind == K::kCommandToggle)
			require(Row.DefaultValue == 0 || Row.DefaultValue == 1, P + ".default", "expected 0 or 1");
	}
	if (Row.Kind == K::kCommandToggle)
		require(Row.Cvar == "in_mlook" || Row.Cvar == "in_jlook", P + ".cvar", "unregistered command-toggle adapter");
	if (Item.HasMember("source")) {
		Row.Source = text(Item, "source", P);
		require(Row.Kind == K::kStringEnum && (Row.Source == "models/player"
			|| Row.Source == "logos" || Row.Source == "logocolor"), P + ".source", "unknown string option provider");
	}
	if (Item.HasMember("options")) {
		const auto& Options = Item["options"];
		array(Options, P + ".options", 256);
		require(Row.Kind == K::kEnum, P + ".options", "options require numeric enum");
		std::set<float> Values;
		for (unsigned I = 0; I < Options.Size(); ++I) {
			const std::string OP = P + ".options[" + std::to_string(I) + "]";
			fields(Options[I], {"value", "label"}, OP);
			float V = number(Options[I], "value", OP);
			require(V >= Row.Min && V <= Row.Max && Values.insert(V).second, OP + ".value", "duplicate or out-of-domain option");
			Row.Options.push_back({V, text(Options[I], "label", OP)});
		}
		require(Values.count(Row.DefaultValue) != 0, P + ".default", "default is not an option");
	}
	require(Row.Kind != K::kEnum || !Row.Options.empty(), P + ".options", "enum requires options");
	require(Row.Kind != K::kStringEnum || !Row.Source.empty(), P + ".source", "string-enum requires option provider");
	return Row;
}
} // namespace

bool SettingsPageDefinition::hasComponent(const char* Name) const {
	return std::find(Components.begin(), Components.end(), Name) != Components.end();
}

bool DefinitionParser::parseMenu(const std::string& Text, MenuDefinition& Output, ConfigError& Error) {
	try {
		auto Doc = document(Text);
		fields(Doc, {"version", "title", "main", "pause"}, "$");
		MenuDefinition Result;
		Result.Title = text(Doc, "title", "$");
		std::set<std::string> Ids = {"mainmenu", "panel", "title", "focus-status", "page-main", "page-pause"};
		Result.Main = menuEntries(member(Doc, "main", "$"), "$.main", Ids);
		Result.Pause = menuEntries(member(Doc, "pause", "$"), "$.pause", Ids);
		auto HasExit = [](const auto& Entries, const char* Action) {
			return std::any_of(Entries.begin(), Entries.end(), [&](const auto& Entry) {
				return Entry.Action == Action && Entry.Condition == "always";
			});
		};
		require(HasExit(Result.Main, "quit"), "$.main", "unconditional quit entry required");
		require(HasExit(Result.Pause, "quit") && HasExit(Result.Pause, "resume"),
			"$.pause", "unconditional resume and quit entries required");
		Output = std::move(Result);
		return true;
	} catch (const ValidationError& E) {
		Error.Field = E.Field;
		Error.Message = E.Message;
		return false;
	}
}

bool DefinitionParser::parsePage(const std::string& Text, SettingsPageDefinition& Output, ConfigError& Error) {
	try {
		auto Doc = document(Text);
		fields(Doc, {"version", "id", "label", "order", "components", "items"}, "$");
		SettingsPageDefinition Result;
		Result.Id = text(Doc, "id", "$");
		require(identifier(Result.Id), "$.id", "invalid page identifier");
		Result.Label = text(Doc, "label", "$");
		const auto& Order = member(Doc, "order", "$");
		require(Order.IsInt(), "$.order", "expected integer");
		Result.Order = Order.GetInt();
		if (Doc.HasMember("components")) {
			const auto& Components = Doc["components"];
			array(Components, "$.components", 4);
			for (const auto& C : Components.GetArray()) {
				require(C.IsString(), "$.components", "expected component name");
				std::string Name(C.GetString(), C.GetStringLength());
				require(Name == "bindings" || Name == "video" || Name == "voice" || Name == "user-script",
					"$.components", "unknown component");
				require(!Result.hasComponent(Name.c_str()), "$.components", "duplicate component");
				Result.Components.push_back(std::move(Name));
			}
		}
		const auto& Items = member(Doc, "items", "$");
		array(Items, "$.items", 512);
		std::set<std::string> Cvars;
		for (unsigned I = 0; I < Items.Size(); ++I) {
			auto Row = setting(Items[I], "$.items[" + std::to_string(I) + "]");
			require(Cvars.insert(Row.Cvar).second, "$.items[" + std::to_string(I) + "].cvar", "duplicate cvar");
			Result.Items.push_back(std::move(Row));
		}
		Output = std::move(Result);
		return true;
	} catch (const ValidationError& E) {
		Error.Field = E.Field;
		Error.Message = E.Message;
		return false;
	}
}

MenuDefinition DefinitionParser::fallbackMenu() {
	MenuDefinition Result;
	Result.Main = {{"main-quit", "#MainMenu_Quit", "quit"}};
	Result.Pause = {{"pause-resume", "#MainMenu_Resume", "resume"},
		{"pause-quit", "#MainMenu_Quit", "quit"}};
	return Result;
}
} // namespace services::uiconfig
