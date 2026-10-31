// abc_ui_bullet_fx - the fired-round effect built on the particle system:
// deterministic spread, gravity, fade and expiry. Pure math.
#include "common/BulletFx.h"

#include <cmath>

#include "framework/TestHarness.h"

using moduleui::bulletfx::Bullets;
using moduleui::bulletfx::kDrift;
using moduleui::bulletfx::kLifetime;
using moduleui::bulletfx::opacity;

int main() {
  TEST_CASE("fire");
  {
    Bullets Effect;
    const std::size_t Slot = Effect.fire({100.0f, 50.0f});
    CHECK(Slot < Effect.capacity());
    const auto &Round = Effect.at(Slot);
    CHECK(Round.Alive);
    CHECK_NEAR(Round.Position.X, 100.0, 0.001);
    CHECK_NEAR(Round.Position.Y, 50.0, 0.001);
    CHECK(Round.Velocity.Y > 0.0f);
    CHECK(std::abs(Round.Velocity.X) <= kDrift + 0.001f);
  }

  TEST_CASE("gravity");
  {
    Bullets Effect;
    const std::size_t Slot = Effect.fire({0.0f, 0.0f});
    const float FallSpeed = Effect.at(Slot).Velocity.Y;
    CHECK(Effect.update(0.1f));
    CHECK(Effect.at(Slot).Velocity.Y >
          FallSpeed + moduleui::bulletfx::kGravity * 0.1f - 1.0f);
    CHECK(Effect.at(Slot).Position.Y > 0.0f);
  }

  TEST_CASE("fade");
  {
    Bullets Effect;
    const std::size_t Slot = Effect.fire({0.0f, 0.0f});
    CHECK_NEAR(opacity(Effect.at(Slot)), 1.0, 0.001);
    Effect.update(kLifetime * 0.5f);
    CHECK_NEAR(opacity(Effect.at(Slot)), 0.5, 0.02);
  }

  TEST_CASE("expiry");
  {
    Bullets Effect;
    const std::size_t Slot = Effect.fire({0.0f, 0.0f});
    CHECK(Effect.update(kLifetime));
    CHECK(!Effect.at(Slot).Alive);
  }

  TEST_CASE("bounded-pool");
  {
    Bullets Effect;
    for (int I = 0; I < static_cast<int>(Effect.capacity()) * 3; ++I)
      Effect.fire({static_cast<float>(I), 0.0f});
    const auto Rounds = Effect;
    int Alive = 0;
    for (std::size_t I = 0; I < Rounds.capacity(); ++I)
      if (Rounds.at(I).Alive)
        ++Alive;
    CHECK(Alive == static_cast<int>(Rounds.capacity()));
  }

  TEST_CASE("clear");
  {
    Bullets Effect;
    Effect.fire({0.0f, 0.0f});
    Effect.clear();
    CHECK(!Effect.update(0.016f));
  }

  return abctest::finish("abc_ui_bullet_fx");
}
