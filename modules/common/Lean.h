#pragma once

// moduleui::lean - the third-person lean shared by HUD plates that read as
// world-anchored panels (abcui.division).
//
// Pure screen-pixel math: no RmlUi, no engine, no allocation. The caller owns
// the projection, the follow smoothing and the applied plate box; this header
// only rebuilds the world-anchored hip x from the plate's applied position
// and turns it into the normalised lean amount.
//
// The lean deliberately reads the APPLIED plate (post follow solver, post
// viewport clamp), never the raw projection: plate and lean then share one
// smoothed trajectory, so a stepped projection target cannot reach the
// transform on its own - the lean moves exactly as fast as the plate does.

#include <algorithm>
#include <cmath>

namespace moduleui::lean {

// Rebuilds the hip x the plate is anchored to from its applied top-left.
// `gap` is the fixed hip offset the plate keeps to the model: on the left
// side the plate's right edge sits one gap left of the hip, on the right
// side its left edge sits one gap right of it.
inline float hipXFromApplied(float appliedX, float clusterWidth, float gap,
                             bool sideRight) {
  return sideRight ? appliedX - gap : appliedX + clusterWidth + gap;
}

// Normalised horizontal offset of the hip from the camera axis (-1..1); 0
// keeps the plate flat. A degenerate viewport leans nothing.
inline float amount(float hipX, float viewportWidth) {
  const float halfWidth = viewportWidth * 0.5f;
  if (halfWidth <= 0.0f)
    return 0.0f;
  return std::clamp((hipX - halfWidth) / halfWidth, -1.0f, 1.0f);
}

} // namespace moduleui::lean
