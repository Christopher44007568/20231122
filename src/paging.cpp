#include "paging.h"

#include <algorithm>

#include "util.h"

namespace tar {

PageTable::PageTable(const PagingConfig& cfg)
    : cfg_(cfg),
      layer_of_page_(cfg.num_pages, -1),
      page_of_layer_(cfg.num_layers, -1) {}

FramePromotions PageTable::BeginFrame() {
  FramePromotions out;
  for (int page : pending_) {
    if (layer_of_page_[page] == -1) {
      PagingResult r = PageIn(page);
      out.promoted.emplace_back(page, r.layer);
      if (r.evicted_page >= 0) out.evicted.push_back(r.evicted_page);
    }
  }
  pending_.clear();
  return out;
}

PagingResult PageTable::PageIn(int page) {
  PagingResult res;
  res.faulted = true;
  int layer;
  auto& order = cfg_.extension_enabled ? fifo_ : lru_;
  if (static_cast<int>(order.size()) >= cfg_.num_layers) {
    layer = cfg_.extension_enabled ? order.front() : order.back();
    if (cfg_.extension_enabled) {
      order.pop_front();
    } else {
      order.pop_back();
    }
    res.evicted_page = page_of_layer_[layer];
    layer_of_page_[res.evicted_page] = -1;
  } else {
    layer = static_cast<int>(order.size());
  }
  page_of_layer_[layer] = page;
  layer_of_page_[page] = layer;
  order.push_back(layer);
  res.layer = layer;
  return res;
}

PagingResult PageTable::Acquire(int page) {
  if (page < 0 || page >= cfg_.num_pages) {
    throw PipelineError("page out of range: " + std::to_string(page));
  }
  PagingResult res;
  if (layer_of_page_[page] != -1) {
    res.layer = layer_of_page_[page];
    if (!cfg_.extension_enabled) {
      lru_.erase(std::remove(lru_.begin(), lru_.end(), res.layer), lru_.end());
      lru_.push_back(res.layer);  // back = most recently used
    }
    return res;
  }
  if (cfg_.extension_enabled) {
    // Speculative path: serve a placeholder now, page in next frame.
    res.faulted = true;
    pending_.push_back(page);
    res.layer = 0;  // ignored: shader draws the placeholder pattern
    res.placeholder = true;
    return res;
  }
  // Baseline path: evict (LRU) and page in synchronously.
  return PageIn(page);
}

std::vector<int> PageTable::ResidentPages() const {
  std::vector<int> out;
  for (int p = 0; p < cfg_.num_pages; ++p) {
    if (layer_of_page_[p] != -1) out.push_back(p);
  }
  return out;
}

uint64_t PageTable::ResidentBytes(int page_bytes) const {
  return static_cast<uint64_t>(ResidentPages().size()) *
         static_cast<uint64_t>(page_bytes);
}

}  // namespace tar
