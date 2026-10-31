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

    // Centre the sprite on the particle position: the round is emitted at the
    // ammo icon's centre, so it has to leave from under the icon itself.
    float Width = 0.0f;
    float Height = 0.0f;
    Api->ui->elementOffsetSize(Element, &Width, &Height);

    char Left[24] = {};
    char Top[24] = {};
    char Alpha[16] = {};
    char Transform[48] = {};
    std::snprintf(Left, sizeof(Left), "%.1fpx",
                  Round.Position.X - Width * 0.5f);
    std::snprintf(Top, sizeof(Top), "%.1fpx", Round.Position.Y - Height * 0.5f);
    std::snprintf(Alpha, sizeof(Alpha), "%.2f", opacity(Round));
    std::snprintf(Transform, sizeof(Transform), "rotate(%.0fdeg)",
                  Round.Rotation);

    Api->ui->elementSetProperty(Element, "display", "block");
    Api->ui->elementSetProperty(Element, "left", Left);
    Api->ui->elementSetProperty(Element, "top", Top);
    Api->ui->elementSetProperty(Element, "opacity", Alpha);
    Api->ui->elementSetProperty(Element, "transform", Transform);
  }
}

} // namespace moduleui::bulletfx
