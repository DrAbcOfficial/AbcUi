#pragma once

// rmlui - abc_ui_probe, Debug builds only.
//
// Live diagnostic cross-section for acceptance and triage: dumps hudstate,
// per-document visibility and hardcoded panel element states (armor-bar-wrap,
// flashlight off-image), then opens a font-pipeline document. The element ids
// and panel names it probes are deliberately listed only here, in a Debug
// translation unit - the production surface must not couple to them.

namespace rmlui {

// HUD_Init time (abc_ui_probe command). No-op in Release.
void registerProbeCommand();

}
