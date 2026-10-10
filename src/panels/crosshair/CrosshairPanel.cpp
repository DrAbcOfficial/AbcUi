// panels::CrosshairPanel implementation - see CrosshairPanel.h.
// from: ABCEnchance/src/hud/vgui/crosshair.* (GBK->UTF-8).
#include <metahook.h>

#include "CrosshairPanel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include <event_api.h>
#include <pm_defs.h>

#include <abc/CvarCmd.h>
#include <abc/Math.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../data/WeaponResource.h"
#include "../../framework/Projection.h"

namespace panels {

namespace crosshair {

// Old CCrosshairPanel ctor defaults (persisted contract - never rename).
cvar_t* Crosshair = nullptr;
static cvar_t* AutoHide = nullptr;
static cvar_t* Size = nullptr;
static cvar_t* Thickness = nullptr;
static cvar_t* Gap = nullptr;
static cvar_t* Multiple = nullptr;
static cvar_t* Alpha = nullptr;
static cvar_t* ColorR = nullptr;
static cvar_t* ColorG = nullptr;
static cvar_t* ColorB = nullptr;
static cvar_t* OutlineAlpha = nullptr;
static cvar_t* OutlineR = nullptr;
static cvar_t* OutlineG = nullptr;
static cvar_t* OutlineB = nullptr;
static cvar_t* OutlineDraw = nullptr;
static cvar_t* OutlineWidth = nullptr;
static cvar_t* TShape = nullptr;
static cvar_t* Dot = nullptr;
static cvar_t* AnimInterval = nullptr;
// Old pCvarDefaultCrosshair / pCvarDefaultFOV: the vanilla "crosshair" toggle
// and the engine "default_fov" (zoom detection).
static cvar_t* VanillaCrosshair = nullptr;
static cvar_t* DefaultFov = nullptr;

void registerCvars() {
	Crosshair = abc::CREATE_CVAR("cl_crosshair", "1", abc::FCVAR_VALUE, nullptr);
	AutoHide = abc::CREATE_CVAR("cl_crosshairautohide", "1", abc::FCVAR_VALUE, nullptr);
	Size = abc::CREATE_CVAR("cl_crosshairsize", "24", abc::FCVAR_VALUE, nullptr);
	Thickness = abc::CREATE_CVAR("cl_crosshairthickness", "2", abc::FCVAR_VALUE, nullptr);
	Gap = abc::CREATE_CVAR("cl_crosshairgap", "16", abc::FCVAR_VALUE, nullptr);
	Multiple = abc::CREATE_CVAR("cl_crosshairmultiple", "3", abc::FCVAR_VALUE, nullptr);
	Alpha = abc::CREATE_CVAR("cl_crosshairalpha", "200", abc::FCVAR_VALUE, nullptr);
	ColorR = abc::CREATE_CVAR("cl_crosshaircolor_r", "50", abc::FCVAR_VALUE, nullptr);
	ColorG = abc::CREATE_CVAR("cl_crosshaircolor_g", "250", abc::FCVAR_VALUE, nullptr);
	ColorB = abc::CREATE_CVAR("cl_crosshaircolor_b", "50", abc::FCVAR_VALUE, nullptr);
	OutlineAlpha = abc::CREATE_CVAR("cl_crosshair_outline_alpha", "255", abc::FCVAR_VALUE, nullptr);
	OutlineR = abc::CREATE_CVAR("cl_crosshaircolor_outline_r", "0", abc::FCVAR_VALUE, nullptr);
	OutlineG = abc::CREATE_CVAR("cl_crosshaircolor_outline_g", "0", abc::FCVAR_VALUE, nullptr);
	OutlineB = abc::CREATE_CVAR("cl_crosshaircolor_outline_b", "0", abc::FCVAR_VALUE, nullptr);
	OutlineDraw = abc::CREATE_CVAR("cl_crosshair_outline_draw", "1", abc::FCVAR_VALUE, nullptr);
	OutlineWidth = abc::CREATE_CVAR("cl_crosshair_outline", "1", abc::FCVAR_VALUE, nullptr);
	TShape = abc::CREATE_CVAR("cl_crosshair_t", "0", abc::FCVAR_VALUE, nullptr);
	Dot = abc::CREATE_CVAR("cl_crosshairdot", "0", abc::FCVAR_VALUE, nullptr);
	AnimInterval = abc::CREATE_CVAR("cl_crosshair_anim_interval", "0.1", abc::FCVAR_VALUE, nullptr);
	VanillaCrosshair = abc::CVAR_GET_POINTER("crosshair");
	DefaultFov = abc::CVAR_GET_POINTER("default_fov");
}

} // namespace crosshair

namespace {

// Old GetCurrentFOV: the HUD_UpdateClientData fov capture (s_flFov).
float currentFov() {
	return hudstate::fov();
}

// Old CPlayerTrace::Update's VIEW trace, inlined (only consumer in batch B):
// hull-2 box trace from the eye along the view axis. The full PlayerTrace
// module (head/foot traces for ItemHighlight) lands with batch D.
pmtrace_t viewTrace() {
	pmtrace_t trace = {};
	const cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local)
		return trace;
	vec3_t angles{}, forward{};
	gEngfuncs.GetViewAngles(angles);
	abc::CMathlib::AngleVectors(angles, forward, nullptr, nullptr);
	vec3_t end{};
	for (int i = 0; i < 3; i++)
		end[i] = local->origin[i] + forward[i] * 8192.0f;
	vec3_t start{};
	vec3_t viewOfs{};
	gEngfuncs.pEventAPI->EV_LocalPlayerViewheight(viewOfs);
	for (int i = 0; i < 3; i++)
		start[i] = local->origin[i] + viewOfs[i];
	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_PlayerTrace(start, end, PM_STUDIO_BOX, local->index, &trace);
	return trace;
}

constexpr const char* kTransparent = "rgba(0, 0, 0, 0)";

// Writes one crosshair part when its target state differs from the applied
// one (property-churn guard - see AppliedPart in the header).
void applyPart(Rml::Element* element, CrosshairPanel::AppliedPart& applied,
	const char* color, const float bounds[4]) {
	if (!element)
		return;
	if (applied.valid && applied.color == color &&
		memcmp(applied.bounds, bounds, sizeof(applied.bounds)) == 0)
		return;
	applied.valid = true;
	applied.color = color;
	memcpy(applied.bounds, bounds, sizeof(applied.bounds));
	element->SetProperty("background-color", color);
	element->SetProperty("left", std::to_string(bounds[0]) + "px");
	element->SetProperty("top", std::to_string(bounds[1]) + "px");
	element->SetProperty("width", std::to_string(bounds[2]) + "px");
	element->SetProperty("height", std::to_string(bounds[3]) + "px");
}

} // namespace

CrosshairPanel::CrosshairPanel()
	: HudPanel("crosshair", "abcui/rml/crosshair.rml") {}

void CrosshairPanel::onDocumentReady() {
	subscribe(netmsg::CurWeapon, [this](int state, int id, int, int) {
		// Old SetWeapon feed (g_EventCurWeapon state>0): swap the sprite
		// source; state<=0 leaves the last weapon's spr (old parity - the
		// panel kept drawing the previous spr until Reset).
		if (state <= 0)
			return;
		const weaponres::Weapon* weapon = weaponres::getWeapon(id);
		CurrentWeaponId = (weapon && weapon->id > 0) ? id : 0;
		SprUri.clear();
	});
}

void CrosshairPanel::reset() {
	// Old Reset: hide + forget the weapon/anim state.
	hide();
	CurrentWeaponId = 0;
	PreOffset = 0.0f;
	for (auto& part : Core)
		part.valid = false;
	for (auto& part : Outline)
		part.valid = false;
	SprUri.clear();
	hideDynamicCrosshair();
}

void CrosshairPanel::hideDynamicCrosshair() {
	// Old HideDynamicCrossHair zeroed the ten panel colors; the spr img has no
	// old counterpart (vgui simply stopped painting it) - display:none is the
	// Rml equivalent of "not composited this frame".
	for (const Part& part : Parts) {
		if (Rml::Element* core = Document->GetElementById(part.core))
			core->SetProperty("background-color", kTransparent);
		if (Rml::Element* outline = Document->GetElementById(part.outline))
			outline->SetProperty("background-color", kTransparent);
	}
	for (auto& part : Core)
		part.valid = false;
	for (auto& part : Outline)
		part.valid = false;
	if (Rml::Element* spr = Document->GetElementById("spr-crosshair"))
		spr->SetProperty("display", "none");
	SprUri.clear();
}

void CrosshairPanel::updateCenter() {
	if (gExportfuncs.CL_IsThirdPerson()) {
		const pmtrace_t trace = viewTrace();
		float screen[2] = {};
		framework::projection::worldToScreen(trace.endpos, screen);
		CenterX = screen[0];
		CenterY = screen[1];
	}
	else {
		if (Rml::Context* context = Document->GetContext()) {
			CenterX = static_cast<float>(context->GetDimensions().x / 2);
			CenterY = static_cast<float>(context->GetDimensions().y / 2);
		}
	}
}

void CrosshairPanel::drawDefaultCrosshair() {
	// Old PaintBackground tail: only the vanilla "crosshair" cvar gate here -
	// the spectate/hudhide/suit/health gates already returned above.
	if (!crosshair::VanillaCrosshair || crosshair::VanillaCrosshair->value <= 0)
		return;
	const weaponres::Weapon* weapon = weaponres::getWeapon(CurrentWeaponId);
	if (!weapon)
		return;

	const bool onTarget = (weapon->state & weaponres::kWeaponStateOnTarget) != 0;
	const float defaultFov = crosshair::DefaultFov ? crosshair::DefaultFov->value : 90.0f;
	const bool zoomed = currentFov() < defaultFov;
	const weaponres::Weapon::SpriteSource* source;
	if (!zoomed)
		source = (onTarget && weapon->autoaim.valid) ? &weapon->autoaim : &weapon->crosshair;
	else
		source = (onTarget && weapon->zoomedAutoaim.valid) ? &weapon->zoomedAutoaim : &weapon->zoomedCrosshair;
	if (!source || !source->valid)
		return;

	char uri[160];
	sprintf_s(uri, "spralpha://%s/%d,%d,%d,%d", source->atlas.c_str(),
		source->rect.left, source->rect.top, source->rect.right, source->rect.bottom);
	const float w = static_cast<float>(source->rect.right - source->rect.left);
	const float h = static_cast<float>(source->rect.bottom - source->rect.top);

	Rml::Element* spr = Document->GetElementById("spr-crosshair");
	if (!spr)
		return;
	if (SprUri != uri) {
		SprUri = uri;
		spr->SetAttribute("src", Rml::String(uri));
	}
	spr->SetProperty("display", "block");
	spr->SetProperty("left", std::to_string(CenterX - w / 2.0f) + "px");
	spr->SetProperty("top", std::to_string(CenterY - h / 2.0f) + "px");
	spr->SetProperty("width", std::to_string(w) + "px");
	spr->SetProperty("height", std::to_string(h) + "px");
}

void CrosshairPanel::onData(double frameTime) {
	(void)frameTime;
	// The whole per-frame body runs in onRedraw (old vgui think/paint phase -
	// the third-person projection needs live view matrices).
}

void CrosshairPanel::onRedraw(double frameTime) {
	(void)frameTime;
	// Old OnThink + PaintBackground gates, evaluated live (the cvar-callback
	// instant-show paths reach the same state one frame later at worst).
	const bool showDocument = hudstate::hasSuit() &&
		(!crosshair::Crosshair || crosshair::Crosshair->value > 0) &&
		!hudstate::isHudHidden(hudstate::kHideAll);
	if (showDocument)
		show();
	else
		hide();
	if (!showDocument || !Document || !gEngfuncs.GetLocalPlayer())
		return;

	// Old OnThink/PaintBackground hide gates (spectate / hud hide / suit /
	// health): nothing draws this frame.
	if (hudstate::isSpectate() || hudstate::isHudHidden(hudstate::kHideAll | hudstate::kHideWeapons) ||
		!hudstate::hasSuit() || hudstate::health() <= 0) {
		hideDynamicCrosshair();
		return;
	}
	// Zoom autohide: dynamic parts hide, the vanilla spr still draws (old
	// PaintBackground ran independently of this gate).
	if (crosshair::VanillaCrosshair && crosshair::VanillaCrosshair->value != 0 &&
		crosshair::AutoHide && crosshair::AutoHide->value > 0 &&
		crosshair::DefaultFov && crosshair::DefaultFov->value != currentFov()) {
		hideDynamicCrosshair();
		drawDefaultCrosshair();
		return;
	}

	updateCenter();

	// Old OnThink dynamic geometry (raw pixels; drift from clientdata
	// punchangle - see the header port note for the EV capture).
	const float* punch = hudstate::punchangle();
	float drift = (punch ? fabsf(punch[0]) : 0.0f) + (punch ? fabsf(punch[1]) : 0.0f);
	drift = drift * 5.0f * (crosshair::Multiple ? crosshair::Multiple->value : 3.0f);
	const float width = crosshair::Thickness ? crosshair::Thickness->value : 2.0f;
	const float length = crosshair::Size ? crosshair::Size->value : 24.0f;
	const float outlineWidth = crosshair::OutlineWidth ? crosshair::OutlineWidth->value : 1.0f;
	const float widthOffset = width / 2.0f;
	const float finalOffset = drift + (crosshair::Gap ? crosshair::Gap->value : 16.0f) + widthOffset;

	char color[40];
	sprintf_s(color, "rgba(%d, %d, %d, %d)",
		static_cast<int>(crosshair::ColorR ? crosshair::ColorR->value : 50.0f),
		static_cast<int>(crosshair::ColorG ? crosshair::ColorG->value : 250.0f),
		static_cast<int>(crosshair::ColorB ? crosshair::ColorB->value : 50.0f),
		static_cast<int>(crosshair::Alpha ? crosshair::Alpha->value : 200.0f));
	const Rml::String coreColor = color;
	sprintf_s(color, "rgba(%d, %d, %d, %d)",
		static_cast<int>(crosshair::OutlineR ? crosshair::OutlineR->value : 0.0f),
		static_cast<int>(crosshair::OutlineG ? crosshair::OutlineG->value : 0.0f),
		static_cast<int>(crosshair::OutlineB ? crosshair::OutlineB->value : 0.0f),
		static_cast<int>(crosshair::OutlineAlpha ? crosshair::OutlineAlpha->value : 255.0f));
	const Rml::String outlineColor = color;

	// Per-part alpha rules: center dot only with cl_crosshairdot, top bar
	// hidden in T shape (old ctor SetBgColor dance).
	const bool dotOn = crosshair::Dot && crosshair::Dot->value > 0;
	const bool topOn = !(crosshair::TShape && crosshair::TShape->value > 0);
	const bool partOn[5] = {dotOn, topOn, true, true, true};

	// Old DrawDefaultCrosshair ran in PaintBackground (every frame, outside
	// the bounds throttle).
	drawDefaultCrosshair();

	// Old anim throttle: skip the bounds rewrite while inside the interval and
	// the gap did not grow.
	const float now = static_cast<float>(gEngfuncs.GetClientTime());
	const float interval = crosshair::AnimInterval ? crosshair::AnimInterval->value : 0.1f;
	if (now - AnimTime <= interval && PreOffset >= finalOffset)
		return;
	AnimTime = now;
	PreOffset = finalOffset;

	const float core[5][4] = {
		{CenterX - widthOffset, CenterY - widthOffset, width, width},
		{CenterX - widthOffset, CenterY - finalOffset - length, width, length},
		{CenterX - widthOffset, CenterY + finalOffset, width, length},
		{CenterX - finalOffset - length, CenterY - widthOffset, length, width},
		{CenterX + finalOffset, CenterY - widthOffset, length, width},
	};
	const bool outlineOn = crosshair::OutlineDraw && crosshair::OutlineDraw->value > 0;
	const float ow = outlineOn ? outlineWidth : 0.0f;
	const float outline[5][4] = {
		{CenterX - widthOffset - ow, CenterY - widthOffset - ow, 2 * ow + width, 2 * ow + width},
		{CenterX - widthOffset - ow, CenterY - finalOffset - length - ow, 2 * ow + width, 2 * ow + length},
		{CenterX - widthOffset - ow, CenterY + finalOffset - ow, 2 * ow + width, 2 * ow + length},
		{CenterX - finalOffset - length - ow, CenterY - widthOffset - ow, 2 * ow + length, 2 * ow + width},
		{CenterX + finalOffset - ow, CenterY - widthOffset - ow, 2 * ow + length, 2 * ow + width},
	};

	for (int i = 0; i < 5; i++) {
		applyPart(Document->GetElementById(Parts[i].core), Core[i],
			partOn[i] ? coreColor.c_str() : kTransparent, core[i]);
		applyPart(Document->GetElementById(Parts[i].outline), Outline[i],
			(outlineOn && partOn[i]) ? outlineColor.c_str() : kTransparent, outline[i]);
	}
}

}
