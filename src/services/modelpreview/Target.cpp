// New implementation; see Target.h. Model loading uses the engine filesystem
// through Renderer::loadModel, including mounted game search paths.
#include <metahook.h>
#include "Target.h"

#include <com_model.h>
#include <studio.h>
#include <abc/Console.h>
#include <abc/Render.h>
#include <abc/RenderCommon.h>
#include <abc/Plugin.h>
#include "../../rmlui/AbcRenderInterface.h"
#include "../../rmlui/RmlUiModule.h"

#include <algorithm>
#include <cmath>
#include <vector>

#undef min
#undef max

namespace modelpreview {
namespace {
std::vector<Target*>& targets() {
	static std::vector<Target*> Targets;
	return Targets;
}
IModelPreviewRenderer* previewRenderer() {
	static IModelPreviewRenderer* Preview = nullptr;
	if (!Preview) {
		mh_plugininfo_t Info{};
		if (g_pMetaHookAPI->GetPluginInfoByBaseFileName("Renderer", &Info)) {
			const auto Factory = Sys_GetFactory((HINTERFACEMODULE)Info.PluginModuleBase);
			if (Factory)
				Preview = static_cast<IModelPreviewRenderer*>(Factory(MODEL_PREVIEW_RENDERER_INTERFACE_VERSION, nullptr));
		}
	}
	return Preview;
}
}

struct Target::State {
	FBO_Container_t Fbo{};
	cl_entity_t Entity{};
	std::string Source;
	std::string Uri;
	unsigned int Frames = 0;
	bool Attempted = false;
	bool Warned = false;
	double LastTime = 0;
	float AnimationFrame = 0;
	int LastSequence = -1;
	float LastFrame = -1;
};

Target::Target() : Data(std::make_unique<State>()) {
	static unsigned long long NextId = 0;
	Data->Uri = "modelfbo://" + std::to_string(++NextId) + "/";
	targets().push_back(this);
}

Target::~Target() {
	invalidate();
	std::erase(targets(), this);
}

void Target::invalidate() {
	if (auto* Interface = rmlui::renderInterface())
		Interface->unregisterExternalTexture(Data->Uri);
	if (auto* Renderer = abc::MetaRenderer(); Renderer && Data->Fbo.s_hBackBufferFBO)
		Renderer->FreeFBO(&Data->Fbo);
	Data->Fbo = {};
	Data->Entity = {};
	Data->Attempted = false;
	Data->LastSequence = -1;
	Data->LastTime = 0;
}

bool Target::render(const Options& Config, int Width, int Height) {
	auto* Renderer = abc::MetaRenderer();
	auto* Preview = previewRenderer();
	if (!Renderer || !Preview) {
		if (!Data->Warned) {
			abc::ConsoleWarn("[AbcUi model] Renderer lacks %s; preview disabled\n", MODEL_PREVIEW_RENDERER_INTERFACE_VERSION);
			Data->Warned = true;
		}
		return false;
	}
	if (Data->Source != Config.Source) {
		invalidate();
		Data->Source = Config.Source;
	}
	if (Config.Source.empty() || Width <= 0 || Height <= 0)
		return false;
	if (!Data->Attempted) {
		Data->Attempted = true;
		Data->Entity.model = Preview->loadModel(Config.Source.c_str());
		if (!Data->Entity.model) {
			abc::ConsoleWarn("[AbcUi model] cannot load Studio model: %s\n", Config.Source.c_str());
			return false;
		}
	}
	if (!Data->Entity.model)
		return false;
	Width = std::clamp(Width, 1, 2048);
	Height = std::clamp(Height, 1, 2048);
	if (Data->Fbo.iWidth != Width || Data->Fbo.iHeight != Height) {
		Renderer->PushFrameBuffer();
		if (Data->Fbo.s_hBackBufferFBO)
			Renderer->FreeFBO(&Data->Fbo);
		Data->Fbo = {};
		Data->Fbo.iWidth = Width;
		Data->Fbo.iHeight = Height;
		Renderer->GenFrameBuffer(&Data->Fbo, "AbcUi model");
		Renderer->FrameBufferColorTexture(&Data->Fbo, GL_RGBA8);
		Renderer->FrameBufferDepthTexture(&Data->Fbo, GL_DEPTH24_STENCIL8);
		Renderer->PopFrameBuffer();
	}
	auto& Entity = Data->Entity;
	auto& State = Entity.curstate;
	State.rendermode = Config.RenderMode;
	State.renderfx = Config.RenderFx;
	State.renderamt = Config.RenderAmount;
	State.rendercolor = {static_cast<byte>(Config.RenderColor[0]),
		static_cast<byte>(Config.RenderColor[1]), static_cast<byte>(Config.RenderColor[2])};
	State.sequence = Config.Sequence;
	State.skin = Config.Skin;
	State.body = Config.Body;
	State.scale = Config.Scale;
	State.colormap = Config.ColorMap;
	Entity.mouth.mouthopen = static_cast<byte>(Config.Mouth);
	for (int i = 0; i < 4; ++i) {
		State.controller[i] = static_cast<byte>(Config.Controllers[i]);
		Entity.latched.prevcontroller[i] = State.controller[i];
	}
	for (int i = 0; i < 2; ++i) {
		State.blending[i] = static_cast<byte>(Config.Blending[i]);
		Entity.latched.prevblending[i] = State.blending[i];
	}
	State.framerate = 0; // Animation is advanced using UI time, even without a server.
	std::copy(Config.Origin.begin(), Config.Origin.end(), Entity.origin);
	std::copy(Config.Origin.begin(), Config.Origin.end(), State.origin);
	std::copy(Config.Angles.begin(), Config.Angles.end(), Entity.angles);
	std::copy(Config.Angles.begin(), Config.Angles.end(), State.angles);
	const double Time = gEngfuncs.GetAbsoluteTime();
	if (Data->LastSequence != Config.Sequence || Data->LastFrame != Config.Frame) {
		Data->AnimationFrame = Config.Frame;
		Data->LastSequence = Config.Sequence;
		Data->LastFrame = Config.Frame;
	}
	// Use the model's own FPS/frame count, not a hard-coded 30 FPS.
	auto* Header = static_cast<studiohdr_t*>(Preview->modelData(Entity.model));
	if (!Header || Header->numseq <= 0)
		return false;
	State.sequence = std::clamp(Config.Sequence, 0, Header->numseq - 1);
	State.skin = std::clamp(Config.Skin, 0, std::max(Header->numskinfamilies - 1, 0));
	const auto* Sequence = reinterpret_cast<const mstudioseqdesc_t*>(
		reinterpret_cast<const byte*>(Header) + Header->seqindex) + State.sequence;
	if (Config.Animate && Data->LastTime > 0 && Sequence->numframes > 1) {
		const double Delta = std::clamp(Time - Data->LastTime, 0.0, 0.1);
		Data->AnimationFrame += static_cast<float>(Delta) * Config.FrameRate *
			Sequence->fps * 256.0f / (Sequence->numframes - 1);
		Data->AnimationFrame = std::fmod(Data->AnimationFrame, 256.0f);
		if (Data->AnimationFrame < 0) Data->AnimationFrame += 256;
	}
	Data->LastTime = Time;
	State.frame = Data->AnimationFrame;
	State.animtime = static_cast<float>(gEngfuncs.GetClientTime());
	Entity.latched.prevanimtime = State.animtime;
	Entity.latched.prevframe = State.frame;
	Entity.latched.prevsequence = State.sequence;
	// Auto framing: place the camera so the CURRENT sequence's bounding box
	// fills the target box (times the margin). Player models differ wildly in
	// size (a humanoid is ~72 units, a creature may be half that, a monster
	// three times), so a fixed camera cannot frame them all.
	ModelPreviewView View = Config.View;
	if (Config.AutoFrame) {
		float top = Sequence->bbmax[2];
		float bottom = Sequence->bbmin[2];
		if (!(top > bottom)) {
			top = Header->bbmax[2];
			bottom = Header->bbmin[2];
		}
		if (top > bottom) {
			const float Height = (top - bottom) * Config.FrameMargin;
			const float Center = (top + bottom) * 0.5f;
			const float HalfFov = Config.View.VerticalFov * 0.5f * 3.14159265358979f / 180.0f;
			const float Distance = std::max(Height * 0.5f / std::tan(HalfFov), 8.0f);
			View.Origin[0] = Distance;
			View.Origin[1] = 0.0f;
			View.Origin[2] = Center;
			View.Angles[0] = 0.0f;
			View.Angles[1] = 180.0f;
			View.Angles[2] = 0.0f;
		}
	}
	if (!Preview->render(&Entity, &Data->Fbo, View))
		return false;
	++Data->Frames;
	if (auto* Interface = rmlui::renderInterface())
		Interface->registerExternalTexture(Data->Uri, Data->Fbo.s_hBackBufferTex, Width, Height, nullptr, true);
	return true;
}

const std::string& Target::uri() const { return Data->Uri; }
unsigned int Target::texture() const { return Data->Fbo.s_hBackBufferTex; }
unsigned int Target::frames() const { return Data->Frames; }
bool hasWorld() {
	auto* Preview = previewRenderer();
	return Preview && Preview->hasWorld();
}
void invalidateAll() {
	for (auto* Target : targets())
		Target->invalidate();
}
}
