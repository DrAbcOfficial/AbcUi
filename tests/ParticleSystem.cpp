// abc_ui_particle_system - the fixed-capacity particle system behind the HUD
// effects: emission, semi-implicit integration, expiry, oldest-slot recycling
// and reset. Pure math.
#include "common/ParticleSystem.h"

#include <cmath>

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
    // Semi-implicit Euler: v += a*dt, then p += v*dt.
    CHECK_NEAR(Pool.at(Index).Velocity.Y, 8.0, 0.001);
    CHECK_NEAR(Pool.at(Index).Position.X, 11.0, 0.001);
    CHECK_NEAR(Pool.at(Index).Position.Y, 24.0, 0.001);
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
    CHECK(!Pool.update(0.016f));
  }

  return abctest::finish("abc_ui_particle_system");
}
