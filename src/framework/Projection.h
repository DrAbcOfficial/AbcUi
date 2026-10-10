#pragma once

// framework::projection - the world-to-screen helper the old utility/util.cpp
// VEC_WorldToScreen provided (ABCEnchance "sprite draw helpers stay UI domain"
// split, abc/Render.h header note). Screen-space output in Rml coordinates
// (top-left origin). Consumers: batch B Crosshair (third-person center),
// GIndicator, PopNum.

namespace framework {

namespace projection {

// Projects a world point; screen[0]/[1] receive the pixel position. The old
// clamps to >=0 stand (negative results only occur off-view; every consumer
// runs a front-facing dot check first). Returns the raw pTriAPI result
// (non-zero = point behind the camera).
int worldToScreen(const vec3_t world, float screen[2]);

}

}
