#include "verify_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "sha256.hpp"

namespace nmts {
namespace {

struct V3 {
  float x, y, z;
};

V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len(V3 a) { return std::sqrt(dot(a, a)); }
V3 scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 normalize(V3 a) {
  float l = len(a);
  if (l <= 0.0f) return {0.0f, 0.0f, 0.0f};
  return scale(a, 1.0f / l);
}

}  // namespace

float quantize_metric(float v) {
  if (!std::isfinite(v)) v = 9.999999e9f;
  return std::round(v * 1.0e6f) * 1.0e-6f;
}

ResultRecord verify_one(std::uint32_t instance_index,
                        const VerifyInput& in) {
  const InstanceRecord& q = in.instance;
  V3 t{q.tangent[0], q.tangent[1], q.tangent[2]};
  V3 b{q.bitangent[0], q.bitangent[1], q.bitangent[2]};
  V3 ng{q.normal_geo[0], q.normal_geo[1], q.normal_geo[2]};

  float metrics[7] = {0, 0, 0, 0, 0, 0, 0};
  std::uint32_t fail = 0;

  // 0: T.B must be ~0.
  metrics[0] = std::fabs(dot(t, b));
  if (metrics[0] > in.eps.orthogonal) fail |= kCheckTangentOrthogonal;

  // 1: tangent must be unit length.
  metrics[1] = std::fabs(len(t) - 1.0f);
  if (metrics[1] > in.eps.unit) fail |= kCheckTangentUnit;

  // 2: handedness of (T, B, N_g) must be right-handed.
  metrics[2] = dot(cross(t, b), ng);
  if (metrics[2] <= 0.0f) fail |= kCheckHandedness;

  // 3: TBN must span volume; |det| away from zero.
  metrics[3] = std::fabs(dot(cross(t, b), ng));
  if (metrics[3] <= in.eps.orthogonal) fail |= kCheckTbnNonDegenerate;

  // Decode tangent-space normal from the RGBA normal map.
  float nx = (float(in.pixel[0]) / 255.0f) * 2.0f - 1.0f;
  float ny = (float(in.pixel[1]) / 255.0f) * 2.0f - 1.0f;
  if (q.green_flip) ny = -ny;
  float nz_raw = (float(in.pixel[2]) / 255.0f) * 2.0f - 1.0f;
  V3 nts{nx, ny, 0.0f};
  float nz_sq = 1.0f - dot(nts, nts);
  float nz = nz_sq > 0.0f ? std::sqrt(nz_sq) : 0.0f;
  V3 nt{nx, ny, nz};

  // 4: reconstructed world normal must be unit length (frame degeneracy
  //     makes this fail even when the stored normal is valid).
  V3 tn = normalize(t);
  V3 bn = normalize(b);
  V3 nn = normalize(ng);
  // Explicit TBN columns: Nw = Tn*nx + Bn*ny + Nn*nz.
  V3 world{tn.x * nt.x + bn.x * nt.y + nn.x * nt.z,
           tn.y * nt.x + bn.y * nt.y + nn.y * nt.z,
           tn.z * nt.x + bn.z * nt.y + nn.z * nt.z};
  metrics[4] = std::fabs(len(world) - 1.0f);
  if (metrics[4] > in.eps.unit) fail |= kCheckNormalLength;

  // 5: normal z component (tangent space, raw channel) must be positive.
  metrics[5] = nz_raw;
  if (metrics[5] <= in.eps.normal_z) fail |= kCheckNormalZ;

  // 6: green-channel flip consistency. Only meaningful when the flip is set:
  //     using the raw z channel (no reconstruction) must stay unit length.
  if (q.green_flip) {
    V3 gf{nx, ny, nz_raw};
    metrics[6] = std::fabs(len(gf) - 1.0f);
    if (metrics[6] > in.eps.unit) fail |= kCheckGreenFlip;
  }

  ResultRecord r{};
  std::memset(&r, 0, sizeof(r));
  r.instance_index = instance_index;
  r.case_id = q.case_id;
  r.fail_mask = fail;
  r.backend = kBackendCpu;
  r.worst_metric = 0.0f;
  for (int i = 0; i < 7; ++i) {
    metrics[i] = quantize_metric(metrics[i]);
    r.metrics[i] = metrics[i];
    r.worst_metric = std::max(r.worst_metric, std::fabs(metrics[i]));
  }
  r.worst_metric = quantize_metric(r.worst_metric);

  // Digest binds the result to the exact original bytes of the instance
  // plus the pixel it references.
  Sha256 h;
  h.update(reinterpret_cast<const std::uint8_t*>(&q), sizeof(q));
  h.update(in.pixel, 4);
  auto d = h.finish();
  std::memcpy(r.check_digest, d.data(), 32);

  // Offsets into the ORIGINAL material file are filled by the pipeline layer.
  r.set_evidence_offset(0);
  r.set_instance_offset(0);
  return r;
}

}  // namespace nmts
