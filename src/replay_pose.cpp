// A pose from the client's transform rows (see replay_pose.h).
#include "replay_pose.h"
#include <cmath>

rf::Pose poseFromBody(int id, const float r[16])
{
    // The rotation R the server's quaternion describes is the transpose of the stored rows:
    // R[i][j] = row j, component i.
    const float m00 = r[0], m01 = r[4], m02 = r[8];
    const float m10 = r[1], m11 = r[5], m12 = r[9];
    const float m20 = r[2], m21 = r[6], m22 = r[10];
    float x, y, z, w;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        w = 0.25f / s; x = (m21 - m12) * s; y = (m02 - m20) * s; z = (m10 - m01) * s;
    } else if (m00 > m11 && m00 > m22) {
        float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
        w = (m21 - m12) / s; x = 0.25f * s; y = (m01 + m10) / s; z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
        w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25f * s; z = (m12 + m21) / s;
    } else {
        float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
        w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25f * s;
    }
    const float len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len > 0.0f) { x /= len; y /= len; z /= len; w /= len; }
    if (w < 0.0f) { x = -x; y = -y; z = -z; w = -w; }   // one sign throughout, so samples interpolate the short way
    return rf::Pose{ id, r[12], r[13], r[14], x, y, z, w };
}
