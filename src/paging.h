#pragma once

#include <deque>
#include <vector>

#include "snapshot.h"

namespace tar {

// Emulated "unknown extension" under review. The baseline (v1) path pages
// synchronously with LRU eviction; the extension (v2) path pages
// speculatively: a faulting draw uses a placeholder layer for one frame
// and the page becomes resident on the next frame (FIFO eviction).
inline constexpr const char* kEmulatedExtension = "VK_EXT_texarray_paging_emulated";

struct PagingConfig {
  int num_pages = 12;    // logical tiles in the working set
  int num_layers = 8;    // physical texture-array layers (capacity)
  bool extension_enabled = false;
};

// One page-table lookup for one object in one frame.
struct PagingResult {
  int layer = -1;         // array layer to sample
  bool placeholder = false;
  bool faulted = false;   // page was not resident
  int evicted_page = -1;  // page evicted to make room, -1 if none
};

// Result of promoting pending pages at frame start (v2 path).
struct FramePromotions {
  std::vector<std::pair<int, int>> promoted;  // (page, layer) now resident
  std::vector<int> evicted;                   // pages evicted during promotion
};

class PageTable {
 public:
  explicit PageTable(const PagingConfig& cfg);

  // Returns the layer for `page`, updating residency per the active policy.
  PagingResult Acquire(int page);

  // v2 only: promotes pages that faulted in the previous frame so the
  // renderer can upload their contents.
  FramePromotions BeginFrame();

  std::vector<int> ResidentPages() const;
  uint64_t ResidentBytes(int page_bytes) const;

 private:
  PagingResult PageIn(int page);  // evicts if needed, marks page resident

  PagingConfig cfg_;
  std::vector<int> layer_of_page_;  // page -> layer, -1 = non-resident
  std::vector<int> page_of_layer_;  // layer -> page, -1 = free
  std::deque<int> lru_;             // layers, front = most recent
  std::deque<int> fifo_;            // layers in insertion order (v2)
  std::vector<int> pending_;        // v2: pages to promote next frame
};

}  // namespace tar
