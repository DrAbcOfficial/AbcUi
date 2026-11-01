#pragma once

// moduleui::bulletfx::paint - the render bridge shared by the ammo readouts.
//
// The doc puts one <img class="bullet"/> per round slot inside a layer
// element; this writes the slot's position, rotation and fade to its element
// (and hides the free slots). Kept beside BulletFx so both abcui.division and
// abcui.ammo drive the effect with the same code.

#include "BulletFx.h"

#include <abcui/module_api.h>

#include <cstdio>

namespace moduleui::bulletfx {

inline void paint(const AbcUiModuleApi *Api, AbcUiElement *Layer,
                  const Bullets &Effect) {
  for (std::size_t Index = 0; Index < Effect.capacity(); ++Index) {
    AbcUiElement *Element =
        Api->ui->elementChild(Layer, static_cast<int32_t>(Index));
    if (!Element)
      continue;

    const particles::Particle &Round = Effect.at(Index);
    if (!Round.Alive) {
      Api->ui->elementSetProperty(Element, "display", "none");
      continue;
    }

    // Transform-only motion avoids a layout change per slot and needs no
    // measurement of an initially display:none sprite.
    char Alpha[16] = {};
    char Transform[192] = {};
    std::snprintf(Alpha, sizeof(Alpha), "%.2f", opacity(Round));
    std::snprintf(Transform, sizeof(Transform),
                  "translate(%.1fpx, %.1fpx) translate(-50%%, -50%%) "
                  "rotate(%.0fdeg)",
                  Round.Position.X, Round.Position.Y, Round.Rotation);

    Api->ui->elementSetProperty(Element, "display", "block");
    Api->ui->elementSetProperty(Element, "opacity", Alpha);
    Api->ui->elementSetProperty(Element, "transform", Transform);
  }
}

} // namespace moduleui::bulletfx
