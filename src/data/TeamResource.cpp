// data::teamres implementation - see TeamResource.h.
// from: ABCEnchance/src/core/resource/teamresource.cpp.
#include "TeamResource.h"

namespace teamres {

const std::array<PredefinedTeam, kPredefinedTeamCount> PredefinedTeams = {{
	{TeamId::ForceNone, "Team_ForceNone"},
	{TeamId::None, "Team_None"},
	{TeamId::Machine, "Team_Machine"},
	{TeamId::Player, "Team_Player"},
	{TeamId::HumanPassive, "Team_HumanPassive"},
	{TeamId::HumanMilitary, "Team_HumanMilitary"},
	{TeamId::AlienMilitary, "Team_AlienMilitary"},
	{TeamId::AlienPassive, "Team_AlienPassive"},
	{TeamId::AlienMonster, "Team_AlienMonster"},
	{TeamId::AlienPrey, "Team_AlienPrey"},
	{TeamId::AlienPredator, "Team_AlienPredator"},
	{TeamId::Insect, "Team_Insect"},
	{TeamId::PlayerAlly, "Team_PlayerAlly"},
	{TeamId::PlayerBioweapon, "Team_PlayerBioweapon"},
	{TeamId::AlienBioweapon, "Team_AlienBioweapon"},
	{TeamId::XracePitdrone, "Team_XracePitdrone"},
	{TeamId::XraceShock, "Team_XraceShock"},
	{TeamId::Team1, "Team_Team1"},
	{TeamId::Team2, "Team_Team2"},
	{TeamId::Team3, "Team_Team3"},
	{TeamId::Team4, "Team_Team4"},
	{TeamId::Barnacle, "Team_Barnacle"},
}};

const char* teamNameToken(TeamId id) {
	for (const auto& team : PredefinedTeams) {
		if (team.id == id)
			return team.token;
	}
	return "Team_Unknown";
}

int teamIndexByTeamId(TeamId id) {
	for (size_t i = 0; i < PredefinedTeams.size(); i++) {
		if (PredefinedTeams[i].id == id)
			return static_cast<int>(i);
	}
	return -1;
}

}
