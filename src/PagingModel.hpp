#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tpd {

constexpr int kInvalidLayer = -1;

struct PageDefinition {
    int contentId = -1;
    bool empty = false;
    std::string label;
};

struct PagingConfig {
    int pageColumns = 4;
    int pageRows = 3;
    int layerCount = 8;
    uint32_t uploadLatencyTicks = 3;
    uint32_t uploadSlots = 2;
    double frameMs = 1000.0 / 60.0;
};

enum class PageState {
    Empty,
    Cold,
    Queued,
    Uploading,
    Resident,
};

enum class UploadState {
    Queued,
    Uploading,
    Complete,
};

enum class RequestOutcome {
    EmptyPage,
    Hit,
    DuplicateHit,
    MissQueued,
};

struct UploadItem {
    uint64_t id = 0;
    int pageId = -1;
    int contentId = -1;
    int layer = kInvalidLayer;
    uint64_t enqueueTick = 0;
    uint64_t startTick = 0;
    uint64_t readyTick = 0;
    UploadState state = UploadState::Queued;
};

struct PageEntry {
    int id = -1;
    int contentId = -1;
    bool empty = false;
    std::string label;
    PageState state = PageState::Cold;
    int layer = kInvalidLayer;
    int reservedLayer = kInvalidLayer;
    uint64_t mappingVersion = 0;
    uint32_t requestCount = 0;
    uint32_t hitCount = 0;
    uint64_t lastRequestTick = 0;
    uint64_t lastRequestSeq = 0;
};

struct LayerEntry {
    int id = -1;
    int primaryPage = -1;
    int contentId = -1;
    bool ready = false;
    bool uploading = false;
    uint64_t lastUsedTick = 0;
    uint64_t mappingVersion = 0;
};

struct SwitchTrace {
    uint64_t seq = 0;
    int pageId = -1;
    int contentId = -1;
    bool empty = false;
    bool duplicate = false;
    uint64_t requestTick = 0;
    uint64_t resolveTick = 0;
    int hitLayer = kInvalidLayer;
    int finalLayer = kInvalidLayer;
    RequestOutcome outcome = RequestOutcome::MissQueued;
    bool resolved = false;

    uint32_t waitTicks() const {
        return resolved ? static_cast<uint32_t>(resolveTick - requestTick) : 0;
    }
};

class PagingModel {
public:
    PagingModel(PagingConfig config, std::vector<PageDefinition> pages);

    void reset();

    SwitchTrace requestPage(int pageId, uint64_t tick);
    std::vector<UploadItem> advanceTo(uint64_t tick);
    std::vector<UploadItem> takeStartedUploads();

    int sampleLayer(int pageId) const;
    const PageEntry& page(int pageId) const;
    const LayerEntry& layer(int layerId) const;
    const std::vector<PageEntry>& pages() const { return pages_; }
    const std::vector<LayerEntry>& layers() const { return layers_; }
    const std::vector<UploadItem>& uploads() const { return uploads_; }
    const std::vector<SwitchTrace>& traces() const { return traces_; }

    const PagingConfig& config() const { return config_; }
    uint64_t currentTick() const { return currentTick_; }
    uint64_t requestCount() const { return requestCount_; }
    int focusPage() const { return focusPage_; }
    const SwitchTrace* pendingTrace() const;
    uint32_t activeUploadCount() const;

    std::vector<std::string> validateInvariants() const;

private:
    uint64_t nextUploadId_ = 1;
    bool initialized_ = false;
    uint64_t requestCount_ = 0;
    uint64_t currentTick_ = 0;
    int focusPage_ = -1;
    PagingConfig config_;
    std::vector<PageEntry> pages_;
    std::vector<LayerEntry> layers_;
    std::vector<UploadItem> uploads_;
    std::vector<SwitchTrace> traces_;
    std::vector<UploadItem> startedUploads_;

    void initialize();
    int findReadyLayerByContent(int contentId) const;
    int findAnyLayerByContent(int contentId) const;
    int allocateLayer(int pageId, int contentId, uint64_t tick);
    void evictPageMappings(int layerId);
    void enqueuePage(int pageId, uint64_t tick);
    void startQueuedUploads(uint64_t tick);
    void completeUploads(uint64_t tick);
    void commitUpload(UploadItem& item, uint64_t tick);
    void resolveTracesForPage(int pageId, int layerId, uint64_t tick);
    void resolveTrace(SwitchTrace& trace, uint64_t tick, int finalLayer);
};

const char* toString(PageState state);
const char* toString(UploadState state);
const char* toString(RequestOutcome outcome);

}  // namespace tpd
