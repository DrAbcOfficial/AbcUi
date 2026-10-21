#pragma once

// framework::ApiTable - the C ABI service table handed to module entries.
//
// The table and its section structs live in sdk/abcui/module_api.h; this
// header is the framework-side owner of the single frozen instance.

#include <abcui/module_api.h>

namespace framework {

namespace ApiTable {

// The one table instance; its layout is append-only (see abi.h).
const AbcUiModuleApi *get();

} // namespace ApiTable

// SDK section implementations living outside ApiTable.cpp (they pull in
// their own subsystems); ApiTable::get() wires the pointers.
namespace DataApi {
const AbcUiDataApi *get();
} // namespace DataApi

namespace UiApi {
const AbcUiUiApi *get();
} // namespace UiApi

namespace WeaponsApi {
const AbcUiWeaponsApi *get();
} // namespace WeaponsApi

namespace PlayersApi {
const AbcUiPlayersApi *get();
} // namespace PlayersApi

namespace ScoreApi {
const AbcUiScoreApi *get();
} // namespace ScoreApi

namespace InputApi {
const AbcUiInputApi *get();
} // namespace InputApi

namespace ShellApi {
const AbcUiShellApi *get();
} // namespace ShellApi

namespace VoiceApi {
const AbcUiVoiceApi *get();
} // namespace VoiceApi

} // namespace framework
