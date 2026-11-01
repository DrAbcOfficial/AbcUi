// abc_ui_particle_system - the fixed-capacity particle system behind the HUD
// effects: emission, constant-acceleration integration, expiry, slot recycling
// and reset. Pure math.
#include "common/ParticleSystem.h"

#include <cmath>
#include <limits>

#include "framework/TestHarness.h"

using moduleui::particles::Emission;
using moduleui::particles::System;

int main() {
  TEST_CASE("emit");
  {
    System<4> Pool;
    Emission Spec;
    Spec.Position = {10.0f, 20.0f};
    Spec.Velocity = {2.0f, 3.0f};
    Spec.Acceleration = {0.0f, 10.0f};
    Spec.Lifetime = 1.0f;

    const std::size_t Index = Pool.emit(Spec);
    CHECK(Index == 0);
    CHECK(Pool.capacity() == 4);
    CHECK(Pool.particles().size() == 4);
    CHECK(Pool.at(Index).Alive);
    CHECK_NEAR(Pool.at(Index).Position.X, 10.0, 0.001);
    CHECK_NEAR(Pool.at(Index).Position.Y, 20.0, 0.001);
    CHECK(Pool.anyAlive());
  }

  TEST_CASE("integration");
  {
    System<4> Pool;
    Emission Spec;
    Spec.Position = {10.0f, 20.0f};
    Spec.Velocity = {2.0f, 3.0f};
    Spec.Acceleration = {0.0f, 10.0f};
    Spec.Lifetime = 2.0f;
    const std::size_t Index = Pool.emit(Spec);

    CHECK(Pool.update(0.5f));
    // p += v*dt + a*dt*dt/2, then v += a*dt.
    CHECK_NEAR(Pool.at(Index).Velocity.Y, 8.0, 0.001);
    CHECK_NEAR(Pool.at(Index).Position.X, 11.0, 0.001);
    CHECK_NEAR(Pool.at(Index).Position.Y, 22.75, 0.001);
  }

  TEST_CASE("expiry");
  {
    System<4> Pool;
    Emission Spec;
    Spec.Lifetime = 0.1f;
    const std::size_t Index = Pool.emit(Spec);

    CHECK(Pool.update(0.05f));
    CHECK(Pool.at(Index).Alive);
    // The expiry frame still reports "changed" so the caller hides the slot.
    CHECK(Pool.update(0.05f));
    CHECK(!Pool.at(Index).Alive);
    CHECK(!Pool.update(0.016f));
    CHECK(!Pool.anyAlive());
  }

  TEST_CASE("recycles-oldest");
  {
    System<2> Pool;
    Emission Slow;
    Slow.Lifetime = 10.0f;
    const std::size_t First = Pool.emit(Slow);
    CHECK(Pool.update(0.5f)); // First is now the oldest
    Pool.emit(Slow);
    const std::size_t Recycled = Pool.emit(Slow);
    CHECK(Recycled == First);
  }

  TEST_CASE("zero-delta");
  {
    System<2> Pool;
    Emission Spec;
    const std::size_t Index = Pool.emit(Spec);
    Pool.update(0.5f);
    const float Age = Pool.at(Index).Age;
    CHECK(!Pool.update(0.0f));
    CHECK_NEAR(Pool.at(Index).Age, Age, 0.001);
  }

  TEST_CASE("lifetime-floor");
  {
    System<2> Pool;
    Emission Spec;
    Spec.Lifetime = -5.0f;
    const std::size_t Index = Pool.emit(Spec);
    CHECK_NEAR(Pool.at(Index).Lifetime, 1.0, 0.001);
  }

  TEST_CASE("clear");
  {
    System<2> Pool;
    Emission Spec;
    Pool.emit(Spec);
    Pool.clear();
    CHECK(!Pool.anyAlive());
    CHECK(
        Pool.update(0.0f)); // consume the final hide, even without a time step
    CHECK(!Pool.update(0.016f));
    Pool.clear();
    CHECK(!Pool.update(0.016f)); // an already-clean empty pool stays idle
  }

  TEST_CASE("same-frame-recycling");
  {
    System<3> Pool;
    for (std::size_t Index = 0; Index < 12; ++Index)
      CHECK(Pool.emit({}) == Index % 3);
  }

  TEST_CASE("split-frame-invariance");
  {
    Emission Spec;
    Spec.Position = {10.0f, 20.0f};
    Spec.Velocity = {26.0f, 30.0f};
    Spec.Acceleration = {0.0f, 420.0f};
    Spec.AngularVelocity = 300.0f;
    System<1> Whole;
    System<1> Split;
    Whole.emit(Spec);
    Split.emit(Spec);
    Whole.update(0.5f);
    for (int Index = 0; Index < 60; ++Index)
      Split.update(1.0f / 120.0f);
    CHECK_NEAR(Whole.at(0).Position.X, Split.at(0).Position.X, 0.001);
    CHECK_NEAR(Whole.at(0).Position.Y, Split.at(0).Position.Y, 0.001);
    CHECK_NEAR(Whole.at(0).Rotation, Split.at(0).Rotation, 0.001);
  }

  TEST_CASE("invalid-time-and-emission");
  {
    const float Nan = std::numeric_limits<float>::quiet_NaN();
    const float Inf = std::numeric_limits<float>::infinity();
    System<1> Pool;
    Emission Spec;
    Spec.Position = {Nan, Inf};
    Spec.Velocity = {Inf, Nan};
    Spec.Acceleration = {Nan, Inf};
    Spec.Rotation = Nan;
    Spec.AngularVelocity = Inf;
    Spec.Lifetime = Inf;
    Pool.emit(Spec);
    CHECK(Pool.update(0.0f)); // a new emission still needs painting
    for (float Delta : {Nan, Inf, -Inf, -0.5f}) {
      CHECK(!Pool.update(Delta));
      CHECK_NEAR(Pool.at(0).Age, 0.0f, 0.0);
    }
    CHECK(Pool.update(0.1f));
    CHECK(std::isfinite(Pool.at(0).Position.X));
    CHECK(std::isfinite(Pool.at(0).Position.Y));
    CHECK(std::isfinite(Pool.at(0).Rotation));
    CHECK_NEAR(Pool.at(0).Lifetime, 1.0f, 0.0);
    CHECK(Pool.update(1.0f));
    CHECK(!Pool.anyAlive());
  }

  TEST_CASE("numeric-overflow-expires");
  {
    System<1> Pool;
    Emission Spec;
    Spec.Position.X = std::numeric_limits<float>::max();
    Spec.Velocity.X = std::numeric_limits<float>::max();
    Pool.emit(Spec);
    CHECK(Pool.update(0.5f));
    CHECK(!Pool.anyAlive());
  }

  return abctest::finish("abc_ui_particle_system");
}
