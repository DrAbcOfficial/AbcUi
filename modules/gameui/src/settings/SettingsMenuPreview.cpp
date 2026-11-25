// The main menu character card's camera, model placement and light on the
// Menu settings page: the config section behind them, the slider rows built
// from the model's field table, the draft projection and the card's own
// restore-defaults entry. The values live in menupreview::state(); the page
// transactions reach them through applyMenuPreview and menuPreviewDirty,
// exactly like the cvar rows.

#include "../shared/PageShared.h"
#include "MenuPreviewSettings.h"
#include "SettingsInternal.h"

#include <optional>
#include <string>

namespace settingspage {

bool MenuPreviewBuilt = false;

namespace {

// The stored section is a few hundred bytes; anything far larger is not
// this module's and is treated as damaged.
constexpr uint32_t kMaxSectionBytes = 4 * 1024;

// The card's tokens, with the module prefix the slider table leaves off.
std::string menuPreviewText(const char *Token) {
  return resolve(("#abcui.gameui/" + std::string(Token)).c_str());
}

std::string controlId(const menupreview::Slider &Slider) {
  return "menu-preview-" + std::string(Slider.Id);
}

// The card's own status line: what the page owes, and whether a save can
// succeed at all.
std::string menuPreviewStatus() {
  const menupreview::State &current = menupreview::state();
  if (current.Unsaved)
    return menuPreviewText(current.ReadOnly ? "Settings_MenuPreviewReadOnly"
                                            : "Settings_MenuPreviewSaveFailed");
  if (current.ReadOnly)
    return menuPreviewText("Settings_MenuPreviewReadOnly");
  if (current.Draft != current.Persisted)
    return menuPreviewText("Settings_MenuPreviewPending");
  return menuPreviewText("Settings_MenuPreviewSaved");
}
} // namespace

void loadMenuPreview() {
  menupreview::State &current = menupreview::state();
  current = menupreview::State{};
  if (!Api || !Api->config)
    return;
  const char *section = menupreview::kSection;
  uint32_t required = 0;
  int32_t status = Api->config->readSection(section, nullptr, 0, &required);
  // An absent section is the defaults. Nothing is written for it.
  if (status == ABCUI_ERROR_NOT_FOUND)
    return;

  menupreview::Settings stored;
  std::string problem;
  menupreview::ParseResult result = menupreview::ParseResult::Invalid;
  bool read = false;
  if (status == ABCUI_ERROR_LIMIT && required > 0 &&
      required <= kMaxSectionBytes) {
    std::string json(required, '\0');
    const uint32_t capacity = required;
    status =
        Api->config->readSection(section, json.data(), capacity, &required);
    if (status == ABCUI_OK) {
      json.resize(capacity - 1); // without the terminator
      read = true;
      result = menupreview::parse(json, stored, problem);
    }
  }
  if (result != menupreview::ParseResult::Invalid) {
    current.Persisted = current.Applied = current.Draft = stored;
    if (result == menupreview::ParseResult::Clamped)
      Api->core->warn("config section module.abcui.gameui.menu_preview: %s; "
                      "clamped to its range\n",
                      problem.c_str());
    return;
  }
  // Unreadable or damaged: the defaults are shown and nothing is written
  // over the stored file this session.
  current.ReadOnly = true;
  if (read)
    Api->core->warn("config section module.abcui.gameui.menu_preview is "
                    "damaged (%s); the menu preview uses its defaults and "
                    "is not saved this session\n",
                    problem.c_str());
  else
    Api->core->warn("config section module.abcui.gameui.menu_preview could "
                    "not be read (%d); the menu preview uses its defaults "
                    "and is not saved this session\n",
                    status);
}

void buildMenuPreview() {
  MenuPreviewBuilt = true;
  AbcUiElement *controls = node("menu-preview-controls");
  if (!controls)
    return;
  std::string markup;
  uint8_t column = menupreview::kSliders[0].Column;
  const char *group = nullptr;
  const auto openColumn = [&markup](uint8_t Index) {
    markup += "<div class=\"menu-preview-col\" id=\"menu-preview-col-" +
              std::to_string(Index) + "\">";
  };
  openColumn(column);
  for (const menupreview::Slider &slider : menupreview::kSliders) {
    if (slider.Column != column) {
      markup += "</div>";
      column = slider.Column;
      group = nullptr;
      openColumn(column);
    }
    // Consecutive sliders of one column share their group header.
    if (slider.Group != group) {
      group = slider.Group;
      markup += "<div class=\"menu-preview-group\">#abcui.gameui/" +
                std::string(group) + "</div>";
    }
    const std::string id = controlId(slider);
    markup += "<div class=\"menu-preview-row\"><span "
              "class=\"menu-preview-axis\">#abcui.gameui/" +
              std::string(slider.Label) +
              "</span><input class=\"row-input row-slider\" type=\"range\" "
              "min=\"" +
              numberText(slider.Min) + "\" max=\"" + numberText(slider.Max) +
              "\" step=\"1\" id=\"" + id + "\" aria-label=\"" +
              escapeAttribute(menuPreviewText(slider.Group) + " " +
                              menuPreviewText(slider.Label)) +
              "\"/><span class=\"row-value\" id=\"" + id +
              "-value\"></span></div>";
  }
  markup += "</div>";
  Api->ui->elementSetInnerRML(controls, markup.c_str());
}

void renderMenuPreview() {
  if (!Page)
    return;
  if (!MenuPreviewBuilt)
    buildMenuPreview();
  const menupreview::State &current = menupreview::state();
  const std::string degrees = menuPreviewText("Settings_MenuPreviewDegreeUnit");
  for (const menupreview::Slider &slider : menupreview::kSliders) {
    const std::string id = controlId(slider);
    const float value = menupreview::sliderValue(current.Draft, slider);
    if (AbcUiElement *control = node(id)) {
      // The control's value is the projection: a programmatic value write
      // does not dispatch change, and the value it already shows is not
      // written again.
      char shown[32];
      shown[0] = '\0';
      const std::optional<float> parsed =
          Api->ui->elementValue(control, shown, sizeof(shown)) > 0
              ? parseFloat(shown)
              : std::nullopt;
      if (!parsed || *parsed != value)
        Api->ui->elementSetValue(control, numberText(value).c_str());
    }
    setInnerRml(node(id + "-value"),
                escapeText(numberText(value) +
                           (slider.Degrees ? degrees : std::string())));
  }
  renderMenuPreviewColor();
  // The line takes the warning colour whenever a save did not land.
  if (AbcUiElement *status = node("menu-preview-status")) {
    Api->ui->elementSetClass(status, "unsaved", menupreview::state().Unsaved);
    setInnerRml(status, escapeText(menuPreviewStatus()));
  }
}

bool handleMenuPreviewChange(AbcUiElement *target) {
  if (!target)
    return false;
  const std::string id = Api->ui->elementId(target);
  for (const menupreview::Slider &slider : menupreview::kSliders) {
    if (id != controlId(slider))
      continue;
    // The slider commits whole values inside its range; the model clamps
    // and ignores the value the slider already showed.
    std::string value;
    const std::optional<float> parsed =
        rowEditValue(target, value) ? parseFloat(value) : std::nullopt;
    if (parsed && menupreview::setSliderValue(menupreview::state().Draft,
                                              slider, *parsed)) {
      renderMenuPreview();
      setStatusSummary();
    }
    return true; // the card's control: consumed
  }
  return false;
}

bool menuPreviewDirty() {
  return menupreview::dirty();
}

bool applyMenuPreview() {
  const bool saved = menupreview::apply([](const std::string &Json) {
    const int32_t status =
        Api->config->writeSection(menupreview::kSection, Json.c_str(),
                                  static_cast<uint32_t>(Json.size()));
    return status == ABCUI_OK;
  });
  if (!saved)
    Api->core->warn("the main menu preview settings could not be saved; "
                    "they apply to this session only\n");
  return saved;
}

} // namespace settingspage
