#pragma once

// The settings page's player model preview, the model browser. Its camera is
// explicit - a <model> never fits itself to what it shows (issue 36) - so the
// page names the distance. The engine projects with a fixed horizontal field
// of view, so a card shows 2 x distance x height / width units from its top
// to its bottom: one fixed distance fills the short card and leaves the
// model small in the tall one. Keeping kModelPreviewVisibleUnits in view
// puts the 72-unit player model at four fifths of a card's height, whatever
// the card's shape, and whole through a full drag turn.

namespace settingspage {

// Units from the top of the card to its bottom, at the model's plane.
constexpr float kModelPreviewVisibleUnits = 90.0f;
// The document's own distance, until the card has a layout.
constexpr float kModelPreviewDefaultDistance = 100.0f;

// The camera's distance from the model for a card of `width` x `height`
// context pixels.
constexpr float modelPreviewDistance(float width, float height) {
  return width > 0.0f && height > 0.0f
             ? kModelPreviewVisibleUnits * 0.5f * width / height
             : kModelPreviewDefaultDistance;
}

} // namespace settingspage
