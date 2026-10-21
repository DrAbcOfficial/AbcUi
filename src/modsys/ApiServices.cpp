// framework shell/voice sections - the sdk seams the interactive panels need
// (profile links, copy actions and the per-player voice block list). See
// ApiTable.h for how they join the module table.
#include "ApiTable.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/SystemInterface.h>

#include "../framework/ShellActions.h"
#include "../services/voice/VoiceStatus.h"

namespace framework {
namespace ShellApi {
namespace {

void apiOpenUrl(const char *url) {
  if (url && *url)
    OpenExternalUrl(url);
}

void apiCopyText(const char *text) {
  if (!text || !*text)
    return;
  if (Rml::SystemInterface *system = Rml::GetSystemInterface())
    system->SetClipboardText(text);
}

const AbcUiShellApi kShellApi = {
    sizeof(AbcUiShellApi),
    apiOpenUrl,
    apiCopyText,
};

} // namespace

const AbcUiShellApi *get() {
  return &kShellApi;
}

} // namespace ShellApi

namespace VoiceApi {
namespace {

int32_t apiAvailable() {
  return services::voice::status().available() ? 1 : 0;
}

int32_t apiIsBlocked(int32_t index) {
  return services::voice::status().isPlayerBlocked(index) ? 1 : 0;
}

int32_t apiSetBlocked(int32_t index, int32_t blocked) {
  return services::voice::status().setPlayerBlockedState(index, blocked != 0)
             ? 1
             : 0;
}

const AbcUiVoiceApi kVoiceApi = {
    sizeof(AbcUiVoiceApi),
    apiAvailable,
    apiIsBlocked,
    apiSetBlocked,
};

} // namespace

const AbcUiVoiceApi *get() {
  return &kVoiceApi;
}

} // namespace VoiceApi
} // namespace framework
