#pragma once

// moduleui::bulletfx - the fired-round effect behind the ammo readouts
// (abcui.division and abcui.ammo).
//
// A shot emits a small copy of the weapon's own ammo sprite - the
// server-provided spricon sprite the readout already draws - at the ammo
// icon; the round falls under gravity, spins and fades out. The motion is
// moduleui::particles' generic integration; this header only defines the
// round's parameters and a deterministic spread.

#include "ParticleSystem.h"

#include <algorithm>
#include <cstddef>

namespace moduleui::bulletfx {

inline constexpr std::size_t kMaxRounds = 6;
inline constexpr float kLifetime = 0.65f;  // seconds until a round is gone
inline constexpr float kGravity = 420.0f;  // px/s^2
inline constexpr float kFallSpeed = 30.0f; // initial downward speed
inline constexpr float kDrift = 26.0f;     // horizontal spread
inline constexpr float kSpin = 300.0f;     // deg/s

// Deterministic 0..1 stream: the spread must not depend on global RNG state.
class Spread {
public:
  [[nodiscard]] float next() {
    State = State * 1664525u + 1013904223u;
    return static_cast<float>(State >> 8) / 16777215.0f;
  }

private:
  unsigned State = 0x9e3779b9u;
};

class Bullets {
public:
  // Emits one round at Position (normally the ammo icon's bottom edge) and
  // returns the slot index so the caller can give that element the sprite.
  std::size_t fire(particles::Vec2 Position) {
    const float Drift = Random.next() * 2.0f - 1.0f;

    particles::Emission Spec;
    Spec.Position = Position;
    Spec.Velocity = {Drift * kDrift, kFallSpeed};
    Spec.Acceleration = {0.0f, kGravity};
    Spec.Rotation = Random.next() * 30.0f;
    Spec.AngularVelocity = (Random.next() * 2.0f - 1.0f) * kSpin;
    Spec.Lifetime = kLifetime;
    return Rounds.emit(Spec);
  }

  bool update(float DeltaTime) {
    return Rounds.update(DeltaTime);
  }

  void clear() {
    Rounds.clear();
  }

  [[nodiscard]] const particles::Particle &at(std::size_t Index) const {
    return Rounds.at(Index);
  }

  [[nodiscard]] std::size_t capacity() const {
    return kMaxRounds;
  }

private:
  particles::System<kMaxRounds> Rounds;
  Spread Random;
};

// Fade for the renderer: 1 at emission, 0 at expiry.
[[nodiscard]] inline float opacity(const particles::Particle &Round) {
  return std::clamp(1.0f - Round.Age / Round.Lifetime, 0.0f, 1.0f);
}

} // namespace moduleui::bulletfx
