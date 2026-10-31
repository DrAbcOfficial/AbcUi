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
#include <cstddef>
#include <span>

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
  float Lifetime = 1.0f; // seconds; non-positive falls back to one second
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
    Element.Position = Spec.Position;
    Element.Velocity = Spec.Velocity;
    Element.Acceleration = Spec.Acceleration;
    Element.Rotation = Spec.Rotation;
    Element.AngularVelocity = Spec.AngularVelocity;
    Element.Age = 0.0f;
    Element.Lifetime = Spec.Lifetime > 0.0f ? Spec.Lifetime : 1.0f;
    Element.Alive = true;
    return Index;
  }

  // Advances every live particle. Returns true while any slot still needs
  // drawing, including the frame a particle expires (so the caller hides it).
  bool update(float DeltaTime) {
    if (DeltaTime <= 0.0f)
      return false;

    bool Changed = false;
    for (Particle &Element : Particles) {
      if (!Element.Alive)
        continue;
      Changed = true;
      Element.Age += DeltaTime;
      if (Element.Age >= Element.Lifetime) {
        Element = {};
        continue;
      }
      Element.Velocity.X += Element.Acceleration.X * DeltaTime;
      Element.Velocity.Y += Element.Acceleration.Y * DeltaTime;
      Element.Position.X += Element.Velocity.X * DeltaTime;
      Element.Position.Y += Element.Velocity.Y * DeltaTime;
      Element.Rotation += Element.AngularVelocity * DeltaTime;
    }
    return Changed;
  }

  void clear() {
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
  [[nodiscard]] std::size_t freeSlot() const {
    for (std::size_t Index = 0; Index < Capacity; ++Index)
      if (!Particles[Index].Alive)
        return Index;

    std::size_t Oldest = 0;
    for (std::size_t Index = 1; Index < Capacity; ++Index)
      if (Particles[Index].Age > Particles[Oldest].Age)
        Oldest = Index;
    return Oldest;
  }

  std::array<Particle, Capacity> Particles{};
};

} // namespace moduleui::particles
