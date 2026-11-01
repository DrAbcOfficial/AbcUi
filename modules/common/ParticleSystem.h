#pragma once

// moduleui::particles - a fixed-capacity 2D particle system for HUD effects.
//
// C++20 and LLVM style: std::array storage, [[nodiscard]] queries, no
// allocation and no owning container. The system integrates each particle
// from its velocity and a constant acceleration, advances its rotation and
// expires it at the end of its lifetime. Callers render one element per slot
// and bind the slot index returned by emit().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace moduleui::particles {

struct Vec2 {
  float X = 0.0f;
  float Y = 0.0f;
};

// Emission parameters: the particle's whole future, integrated by the system.
struct Emission {
  Vec2 Position{};
  Vec2 Velocity{};
  Vec2 Acceleration{};
  float Rotation = 0.0f;
  float AngularVelocity = 0.0f;
  float Lifetime = 1.0f; // invalid or non-positive uses one second
};

// One live or free slot. The caller reads it to place its render element.
struct Particle {
  Vec2 Position{};
  Vec2 Velocity{};
  Vec2 Acceleration{};
  float Rotation = 0.0f;
  float AngularVelocity = 0.0f;
  float Age = 0.0f;
  float Lifetime = 1.0f;
  bool Alive = false;
};

template <std::size_t Capacity = 8> class System {
  static_assert(Capacity > 0, "a particle system needs at least one slot");

public:
  // Emits one particle and returns its slot index. A full pool recycles its
  // oldest particle so a long burst never stalls the effect.
  std::size_t emit(const Emission &Spec) {
    const std::size_t Index = freeSlot();
    Particle &Element = Particles[Index];
    Element.Position = {finite(Spec.Position.X), finite(Spec.Position.Y)};
    Element.Velocity = {finite(Spec.Velocity.X), finite(Spec.Velocity.Y)};
    Element.Acceleration = {finite(Spec.Acceleration.X),
                            finite(Spec.Acceleration.Y)};
    Element.Rotation = std::fmod(finite(Spec.Rotation), 360.0f);
    Element.AngularVelocity = finite(Spec.AngularVelocity);
    Element.Age = 0.0f;
    Element.Lifetime = std::isfinite(Spec.Lifetime) && Spec.Lifetime > 0.0f
                           ? Spec.Lifetime
                           : 1.0f;
    Element.Alive = true;
    // Keep emission order independently of age. Several emissions between
    // updates have identical ages, but must still recycle distinct old slots.
    const auto Entry = std::find(Order.begin(), Order.end(), Index);
    std::rotate(Entry, Entry + 1, Order.end());
    Dirty = true;
    return Index;
  }

  // Advances every live particle. Returns true while any slot still needs
  // drawing, including the frame a particle expires (so the caller hides it).
  bool update(float DeltaTime) {
    bool Changed = std::exchange(Dirty, false);
    if (!std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
      return Changed;

    for (Particle &Element : Particles) {
      if (!Element.Alive)
        continue;
      Changed = true;
      const double Age = static_cast<double>(Element.Age) + DeltaTime;
      if (Age >= Element.Lifetime) {
        Element = {};
        continue;
      }
      // Exact constant-acceleration integration: splitting a frame does not
      // change the trajectory. Double intermediates also let us reject
      // overflow before narrowing back into the render-facing floats.
      const double Dt = DeltaTime;
      const double X = Element.Position.X + Element.Velocity.X * Dt +
                       0.5 * Element.Acceleration.X * Dt * Dt;
      const double Y = Element.Position.Y + Element.Velocity.Y * Dt +
                       0.5 * Element.Acceleration.Y * Dt * Dt;
      const double Vx = Element.Velocity.X + Element.Acceleration.X * Dt;
      const double Vy = Element.Velocity.Y + Element.Acceleration.Y * Dt;
      if (!fits(X) || !fits(Y) || !fits(Vx) || !fits(Vy)) {
        Element = {};
        continue;
      }
      Element.Age = static_cast<float>(Age);
      Element.Position = {static_cast<float>(X), static_cast<float>(Y)};
      Element.Velocity = {static_cast<float>(Vx), static_cast<float>(Vy)};
      Element.Rotation = static_cast<float>(
          std::fmod(Element.Rotation + Element.AngularVelocity * Dt, 360.0));
    }
    return Changed;
  }

  void clear() {
    Dirty = Dirty || anyAlive();
    for (Particle &Element : Particles)
      Element = {};
  }

  [[nodiscard]] bool anyAlive() const {
    return std::ranges::any_of(
        Particles, [](const Particle &Element) { return Element.Alive; });
  }

  [[nodiscard]] std::size_t capacity() const {
    return Capacity;
  }

  [[nodiscard]] const Particle &at(std::size_t Index) const {
    return Particles[Index];
  }

  [[nodiscard]] std::span<const Particle> particles() const {
    return Particles;
  }

private:
  static float finite(float Value) {
    return std::isfinite(Value) ? Value : 0.0f;
  }

  static bool fits(double Value) {
    return std::isfinite(Value) &&
           std::abs(Value) <= std::numeric_limits<float>::max();
  }

  [[nodiscard]] std::size_t freeSlot() const {
    for (std::size_t Index : Order)
      if (!Particles[Index].Alive)
        return Index;
    return Order.front();
  }

  std::array<Particle, Capacity> Particles{};
  std::array<std::size_t, Capacity> Order = [] {
    std::array<std::size_t, Capacity> Result{};
    for (std::size_t Index = 0; Index < Capacity; ++Index)
      Result[Index] = Index;
    return Result;
  }();
  bool Dirty = false;
};

} // namespace moduleui::particles
