#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nmts {

constexpr std::array<char, 4> kMagic = {'N', 'M', 'T', 'S'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kHeaderSize = 40;
constexpr std::uint32_t kInstanceSize = 56;
constexpr std::uint32_t kPixelSize = 4;
constexpr std::uint32_t kResultRecordSize = 132;
constexpr std::uint32_t kRepeatCount = 8;

enum CheckBit : std::uint32_t {
  kCheckTangentOrthogonal = 1u << 0,
  kCheckTangentUnit = 1u << 1,
  kCheckHandedness = 1u << 2,
  kCheckTbnNonDegenerate = 1u << 3,
  kCheckNormalLength = 1u << 4,
  kCheckNormalZ = 1u << 5,
  kCheckGreenFlip = 1u << 6,
  kCheckAll = 0x7Fu,
};

enum BackendKind : std::uint32_t {
  kBackendCpu = 1,
  kBackendVulkan = 2,
};

enum DegradeReason : std::uint32_t {
  kDegradeNone = 0,
  kDegradeNoPhysicalDevice = 1,
  kDegradeNoComputeQueue = 2,
  kDegradeAllocationFailed = 3,
  kDegradePipelineFailed = 4,
  kDegradeGlfwUnavailable = 5,
  kDegradeForcedByFlag = 6,
};

#pragma pack(push, 1)
struct MaterialHeader {
  char magic[4];
  std::uint32_t version;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t instance_count;
  std::uint32_t pixel_offset;
  std::uint32_t instance_offset;
  float eps_orthogonal;
  float eps_unit;
  float eps_normal_z;
};
static_assert(sizeof(MaterialHeader) == kHeaderSize, "header must be 40 bytes");

struct InstanceRecord {
  std::uint32_t case_id;
  std::uint32_t repeat_index;
  float tangent[3];
  float bitangent[3];
  float normal_geo[3];
  std::uint32_t pixel_index;
  std::uint32_t green_flip;
  std::uint32_t reserved;
};
static_assert(sizeof(InstanceRecord) == kInstanceSize,
              "instance must be 56 bytes");

struct ResultRecord {
  std::uint32_t instance_offset_lo;
  std::uint32_t instance_offset_hi;
  std::uint32_t evidence_offset_lo;
  std::uint32_t evidence_offset_hi;
  char check_digest[32];
  std::uint32_t instance_index;
  std::uint32_t case_id;
  std::uint32_t fail_mask;
  std::uint32_t backend;
  float worst_metric;
  float metrics[7];
  std::uint32_t tail_pad[9];

  std::uint64_t instance_offset() const {
    return std::uint64_t(instance_offset_lo) |
           (std::uint64_t(instance_offset_hi) << 32);
  }
  std::uint64_t evidence_offset() const {
    return std::uint64_t(evidence_offset_lo) |
           (std::uint64_t(evidence_offset_hi) << 32);
  }
  void set_instance_offset(std::uint64_t v) {
    instance_offset_lo = std::uint32_t(v);
    instance_offset_hi = std::uint32_t(v >> 32);
  }
  void set_evidence_offset(std::uint64_t v) {
    evidence_offset_lo = std::uint32_t(v);
    evidence_offset_hi = std::uint32_t(v >> 32);
  }
};
static_assert(sizeof(ResultRecord) == kResultRecordSize,
              "result must be 132 bytes, matching the std430 array stride");
#pragma pack(pop)

struct Material {
  MaterialHeader header{};
  std::vector<std::uint8_t> pixels;
  std::vector<InstanceRecord> instances;
  std::string source_path;
};

struct Checkpoint {
  std::uint32_t format_version = kFormatVersion;
  std::uint32_t next_instance = 0;
  std::uint32_t total_instances = 0;
  std::uint32_t backend = 0;
  std::uint64_t sequence = 0;
  std::uint32_t failed_checks = 0;
  std::string created_at;
  std::string material_sha256;
  std::string run_id;
  std::string started_at;
};

std::string iso_utc_now();
std::string to_hex(const std::uint8_t* data, std::size_t len);
std::string to_hex_lower(const std::vector<std::uint8_t>& data);

}  // namespace nmts
