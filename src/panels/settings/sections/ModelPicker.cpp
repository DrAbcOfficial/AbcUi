// sections::ModelPicker implementation - see the header.
#include "panels/settings/sections/ModelPicker.h"

#include "panels/settings/SettingsUi.h"
#include "framework/Localization.h"
#include "framework/TextUtil.h"
#include "services/settings/ModelFavorites.h"
#include "services/settings/SettingsModel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <abc/Format.h>

#include <algorithm>
#include <string>

namespace panels::settings::sections {
namespace {
// Byte-wise tolower: the filter compares model directory names, which the
// scan takes verbatim from the filesystem (ASCII names in practice; the
// legacy picker used the same ::tolower rule - non-ASCII names simply only
// match their exact spelling).
char lowerByte(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsCaseInsensitive(const std::string &value,
                             const std::string &filter) {
  if (filter.empty())
    return true;
  const auto found =
      std::search(value.begin(), value.end(), filter.begin(), filter.end(),
                  [](char a, char b) { return lowerByte(a) == lowerByte(b); });
  return found != value.end();
}
} // namespace

void ModelPicker::render(Rml::ElementDocument *doc,
                         services::settings::SettingsModel *model) {
  if (!doc)
    return;
  Rml::Element *list = doc->GetElementById("model-picker-list");
  Rml::Element *status = doc->GetElementById("model-picker-status");
  Rml::Element *favOnly = doc->GetElementById("model-picker-favonly");
  if (!list || !status || !favOnly)
    return;

  const auto view = model ? model->find("model") : std::nullopt;
  const std::string current =
      view && !view->missing ? view->draftText : std::string();
  const std::vector<std::string> *options = view ? view->options : nullptr;

  int total = 0;
  int shown = 0;
  if (options) {
    for (const auto &name : *options) {
      ++total;
      if (FavOnly && !(Favorites && Favorites->isFavorite(name)))
        continue;
      if (!containsCaseInsensitive(name, Filter))
        continue;
      ++shown;
    }
  }

  const std::string signature =
      Filter + "|" + (FavOnly ? "fav" : "all") + "|" + current;
  const bool rebuild =
      signature != BuiltSignature || FavGeneration != BuiltFavGeneration;
  if (rebuild) {
    BuiltSignature = signature;
    BuiltFavGeneration = FavGeneration;
    std::string Markup;
    if (options) {
      for (const auto &name : *options) {
        if (FavOnly && !(Favorites && Favorites->isFavorite(name)))
          continue;
        if (!containsCaseInsensitive(name, Filter))
          continue;
        Markup += "<button class=\"model-picker-row";
        if (name == current)
          Markup += " selected";
        Markup += "\" data-action=\"";
        Markup += detail::kActionModelPick;
        Markup += "\" data-model=\"";
        Markup += framework::TextUtil::escapeRmlAttribute(name);
        Markup += "\"><span class=\"model-picker-name\">";
        Markup += framework::TextUtil::escapeRml(name);
        Markup += "</span><span class=\"model-picker-star";
        if (Favorites && Favorites->isFavorite(name))
          Markup += " on";
        Markup += "\" data-action=\"";
        Markup += detail::kActionModelFav;
        Markup += "\" data-model=\"";
        Markup += framework::TextUtil::escapeRmlAttribute(name);
        Markup += "\">";
        Markup += framework::TextUtil::escapeRml(
            detail::translated("#Settings_ModelStar"));
        Markup += "</span></button>";
      }
    }
    if (shown == 0) {
      Markup += "<div class=\"model-picker-empty\">";
      Markup += framework::TextUtil::escapeRml(
          detail::translated(total == 0 ? "#Settings_ModelPickerEmpty"
                                        : "#Settings_ModelPickerNoMatch"));
      Markup += "</div>";
    }
    list->SetInnerRML(Markup);
  }

  // The toggle's state and the count line are cheap writes: they follow
  // every render pass so external model/favorite changes stay honest.
  favOnly->SetClass("on", FavOnly);
  std::string statusText;
  if (Favorites && !Favorites->savesAllowed()) {
    statusText = detail::translated("#Settings_ModelFavLocked");
  } else if (!options || options->empty()) {
    statusText = detail::translated("#Settings_ModelPickerEmpty");
  } else {
    statusText = abc::Format("{0} / {1} {2}", std::to_string(shown),
                             std::to_string(total),
                             detail::translated("#Settings_ModelPickerModels"));
  }
  status->SetInnerRML(framework::TextUtil::escapeRml(statusText));
}

bool ModelPicker::handleAction(const char *action, Rml::Element *entry,
                               Rml::ElementDocument *doc,
                               services::settings::SettingsModel *model) {
  const std::string which = action ? action : "";
  if (which != detail::kActionModelPick && which != detail::kActionModelFav &&
      which != detail::kActionModelFavOnly)
    return false;
  if (which == detail::kActionModelFavOnly) {
    FavOnly = !FavOnly;
    return true;
  }
  const std::string name =
      entry->GetAttribute<Rml::String>("data-model", "").c_str();
  if (name.empty())
    return true; // picker intent without a payload: consumed, no-op
  if (which == detail::kActionModelFav) {
    toggleFavorite(name);
    return true;
  }
  // The pick commits immediately (issue 16): the draft feeds the shared
  // <model> preview on the next render, applyOne writes the cvar with the
  // model's own read-back; a refused write leaves the row dirty and the
  // page's status bar reports the pending change.
  if (model) {
    model->setDraftString("model", name.c_str());
    model->applyOne("model");
  }
  return true;
}

bool ModelPicker::handleFilterChange(Rml::Event &event,
                                     Rml::ElementDocument *doc,
                                     services::settings::SettingsModel *model) {
  Rml::Element *target = event.GetTargetElement();
  if (!target || target->GetId() != "model-picker-filter")
    return false;
  Filter = event.GetParameter<Rml::String>("value", Rml::String()).c_str();
  render(doc, model);
  return true;
}

void ModelPicker::toggleFavorite(const std::string &model) {
  if (!Favorites || !Favorites->savesAllowed())
    return; // read-only session: the status line explains why
  Favorites->setFavorite(model, !Favorites->isFavorite(model));
  ++FavGeneration; // same-signature rebuild with the new marks
}

} // namespace panels::settings::sections
