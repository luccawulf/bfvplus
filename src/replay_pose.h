// A soldier's or vehicle's pose from the 16 floats at obj+0x94 (rows a, b, c, then the position and
// w = 1). The rows are the transpose of the rotation the server recorder's quaternion describes
// (Session A, 110 rotations). Platform-independent.
#pragma once
#include "replay_format.h"

rf::Pose poseFromBody(int id, const float rows[16]);
