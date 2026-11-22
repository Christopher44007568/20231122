#include "PagingModel.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace tpd {
namespace {

bool layerMapsPage(const LayerEntry& layer, int pageId) {
    return layer.primaryPage == pageId;
}

}  // namespace

PagingModel::PagingModel(PagingConfig config, std::vector<PageDefinition> pages)
    : config_(config) {
    pages_.reserve(pages.size());
    for (PageDefinition& definition : pages) {
        PageEntry entry;
        entry.contentId = definition.contentId;
        entry.empty = definition.empty || definition.contentId < 0;
        entry.label = std::move(definition.label);
        pages_.push_back(std::move(entry));
    }
    if (config_.pageColumns <= 0 || config_.pageRows <= 0 ||
        config_.pageColumns * config_.pageRows != static_cast<int>(pages_.size())) {
        throw std::invalid_argument("page grid does not match page definition count");
    }
    if (config_.layerCount <= 0 || config_.uploadSlots == 0 ||
        config_.uploadSlots > static_cast<uint32_t>(config_.layerCount)) {
        throw std::invalid_argument("invalid layer/upload configuration");
    }
    initialize();
}

void PagingModel::reset() {
    initialize();
}

void PagingModel::initialize() {
    requestCount_ = 0;
    currentTick_ = 0;
    focusPage_ = -1;
    nextUploadId_ = 1;
    uploads_.clear();
    traces_.clear();
    startedUploads_.clear();

    for (size_t i = 0; i < pages_.size(); ++i) {
        PageEntry& page = pages_[i];
        page.id = static_cast<int>(i);
        page.empty = page.contentId < 0 || page.empty;
        if (page.empty) {
            page.contentId = -1;
            page.state = PageState::Empty;
        } else {
            page.state = PageState::Cold;
        }
        page.layer = kInvalidLayer;
        page.reservedLayer = kInvalidLayer;
        page.mappingVersion = 0;
        page.requestCount = 0;
        page.hitCount = 0;
        page.lastRequestTick = 0;
        page.lastRequestSeq = 0;
        if (page.label.empty()) {
            page.label = "P" + std::to_string(page.id);
        }
    }

    layers_.clear();
    layers_.resize(static_cast<size_t>(config_.layerCount));
    for (int i = 0; i < config_.layerCount; ++i) {
        layers_[static_cast<size_t>(i)].id = i;
    }
    initialized_ = true;
}

SwitchTrace PagingModel::requestPage(int pageId, uint64_t tick) {
    if (pageId < 0 || pageId >= static_cast<int>(pages_.size())) {
        throw std::out_of_range("page id out of range");
    }
    if (!initialized_) {
        throw std::logic_error("paging model is not initialized");
    }
    currentTick_ = tick;
    focusPage_ = pageId;
    ++requestCount_;

    PageEntry& page = pages_[static_cast<size_t>(pageId)];
    ++page.requestCount;
    page.lastRequestTick = tick;
    page.lastRequestSeq = requestCount_;

    SwitchTrace trace;
    trace.seq = requestCount_;
    trace.pageId = pageId;
    trace.contentId = page.contentId;
    trace.empty = page.empty;
    trace.requestTick = tick;
    traces_.push_back(trace);
    SwitchTrace& stored = traces_.back();

    if (page.empty) {
        resolveTrace(stored, tick, kInvalidLayer);
        stored.outcome = RequestOutcome::EmptyPage;
        return stored;
    }

    const int readyLayer = findReadyLayerByContent(page.contentId);
    if (readyLayer != kInvalidLayer) {
        LayerEntry& layer = layers_[static_cast<size_t>(readyLayer)];
        stored.duplicate = !layerMapsPage(layer, pageId);
        page.state = PageState::Resident;
        page.layer = readyLayer;
        page.reservedLayer = kInvalidLayer;
        ++page.mappingVersion;
        ++page.hitCount;
        stored.hitLayer = readyLayer;
        resolveTrace(stored, tick, readyLayer);
        stored.outcome = stored.duplicate ? RequestOutcome::DuplicateHit
                                          : RequestOutcome::Hit;
        return stored;
    }

    const int pendingLayer = findAnyLayerByContent(page.contentId);
    if (pendingLayer != kInvalidLayer) {
        LayerEntry& layer = layers_[static_cast<size_t>(pendingLayer)];
        stored.duplicate = !layerMapsPage(layer, pageId);
        page.state = PageState::Queued;
        page.layer = kInvalidLayer;
        page.reservedLayer = pendingLayer;
        for (const UploadItem& item : uploads_) {
            if (item.layer == pendingLayer && item.state == UploadState::Uploading) {
                page.state = PageState::Uploading;
                break;
            }
        }
        return stored;
    }

    enqueuePage(pageId, tick);
    startQueuedUploads(tick);
    return stored;
}

std::vector<UploadItem> PagingModel::advanceTo(uint64_t tick) {
    if (tick < currentTick_) {
        return {};
    }
    startedUploads_.clear();
    while (currentTick_ < tick) {
        ++currentTick_;
        startQueuedUploads(currentTick_);
        completeUploads(currentTick_);
    }
    return takeStartedUploads();
}

std::vector<UploadItem> PagingModel::takeStartedUploads() {
    std::vector<UploadItem> result;
    result.swap(startedUploads_);
    return result;
}

int PagingModel::sampleLayer(int pageId) const {
    if (pageId < 0 || pageId >= static_cast<int>(pages_.size())) {
        return kInvalidLayer;
    }
    const PageEntry& page = pages_[static_cast<size_t>(pageId)];
    if (page.state != PageState::Resident || page.layer < 0 ||
        page.layer >= static_cast<int>(layers_.size())) {
        return kInvalidLayer;
    }
    const LayerEntry& layer = layers_[static_cast<size_t>(page.layer)];
    if (!layer.ready || layer.contentId != page.contentId) {
        return kInvalidLayer;
    }
    return page.layer;
}

const PageEntry& PagingModel::page(int pageId) const {
    return pages_.at(static_cast<size_t>(pageId));
}

const LayerEntry& PagingModel::layer(int layerId) const {
    return layers_.at(static_cast<size_t>(layerId));
}

const SwitchTrace* PagingModel::pendingTrace() const {
    for (auto it = traces_.rbegin(); it != traces_.rend(); ++it) {
        if (!it->resolved) {
            return &*it;
        }
    }
    return nullptr;
}

uint32_t PagingModel::activeUploadCount() const {
    uint32_t count = 0;
    for (const UploadItem& item : uploads_) {
        if (item.state == UploadState::Uploading) {
            ++count;
        }
    }
    return count;
}

int PagingModel::findReadyLayerByContent(int contentId) const {
    for (const LayerEntry& layer : layers_) {
        if (layer.ready && !layer.uploading && layer.contentId == contentId) {
            return layer.id;
        }
    }
    return kInvalidLayer;
}

int PagingModel::findAnyLayerByContent(int contentId) const {
    for (const LayerEntry& layer : layers_) {
        if (layer.contentId == contentId && (layer.ready || layer.uploading)) {
            return layer.id;
        }
    }
    return kInvalidLayer;
}

int PagingModel::allocateLayer(int pageId, int contentId, uint64_t tick) {
    for (LayerEntry& layer : layers_) {
        if (!layer.ready && !layer.uploading && layer.primaryPage < 0) {
            layer.primaryPage = pageId;
            layer.contentId = contentId;
            layer.uploading = true;
            layer.lastUsedTick = tick;
            ++layer.mappingVersion;
            return layer.id;
        }
    }

    int victim = kInvalidLayer;
    uint64_t bestScore = std::numeric_limits<uint64_t>::max();
    for (const LayerEntry& layer : layers_) {
        if (!layer.ready || layer.uploading) {
            continue;
        }
        bool containsFocus = false;
        if (focusPage_ >= 0) {
            for (const PageEntry& page : pages_) {
                if (page.layer == layer.id && page.id == focusPage_) {
                    containsFocus = true;
                    break;
                }
            }
        }
        const uint64_t score =
            layer.lastUsedTick + (containsFocus ? (uint64_t{1} << 60) : 0);
        if (score < bestScore) {
            bestScore = score;
            victim = layer.id;
        }
    }
    if (victim == kInvalidLayer) {
        throw std::runtime_error("no evictable texture-array layer");
    }

    evictPageMappings(victim);
    LayerEntry& layer = layers_[static_cast<size_t>(victim)];
    layer.primaryPage = pageId;
    layer.contentId = contentId;
    layer.ready = false;
    layer.uploading = true;
    layer.lastUsedTick = tick;
    ++layer.mappingVersion;
    return victim;
}

void PagingModel::evictPageMappings(int layerId) {
    for (PageEntry& page : pages_) {
        if (page.layer == layerId) {
            page.layer = kInvalidLayer;
            page.state = PageState::Cold;
            ++page.mappingVersion;
        }
    }
}

void PagingModel::enqueuePage(int pageId, uint64_t tick) {
    PageEntry& page = pages_[static_cast<size_t>(pageId)];
    const int assignedLayer = allocateLayer(pageId, page.contentId, tick);
    page.state = PageState::Queued;
    page.layer = kInvalidLayer;
    page.reservedLayer = assignedLayer;

    UploadItem item;
    item.id = nextUploadId_++;
    item.pageId = pageId;
    item.contentId = page.contentId;
    item.layer = assignedLayer;
    item.enqueueTick = tick;
    item.state = UploadState::Queued;
    uploads_.push_back(item);
}

void PagingModel::startQueuedUploads(uint64_t tick) {
    uint32_t active = activeUploadCount();
    for (UploadItem& item : uploads_) {
        if (active >= config_.uploadSlots) {
            break;
        }
        if (item.state != UploadState::Queued) {
            continue;
        }
        item.state = UploadState::Uploading;
        item.startTick = tick;
        item.readyTick = tick + config_.uploadLatencyTicks;
        ++active;
        for (PageEntry& page : pages_) {
            if (page.reservedLayer == item.layer &&
                page.contentId == item.contentId) {
                page.state = PageState::Uploading;
            }
        }
        startedUploads_.push_back(item);
    }
}

void PagingModel::completeUploads(uint64_t tick) {
    std::vector<uint64_t> readyIds;
    for (const UploadItem& item : uploads_) {
        if (item.state == UploadState::Uploading && item.readyTick <= tick) {
            readyIds.push_back(item.id);
        }
    }
    std::sort(readyIds.begin(), readyIds.end());
    for (uint64_t id : readyIds) {
        auto it = std::find_if(uploads_.begin(), uploads_.end(),
                               [id](const UploadItem& item) { return item.id == id; });
        if (it != uploads_.end()) {
            commitUpload(*it, tick);
        }
    }
}

void PagingModel::commitUpload(UploadItem& item, uint64_t tick) {
    LayerEntry& committedLayer = layers_[static_cast<size_t>(item.layer)];
    committedLayer.primaryPage = item.pageId;
    committedLayer.contentId = item.contentId;
    committedLayer.ready = true;
    committedLayer.uploading = false;
    committedLayer.lastUsedTick = tick;
    ++committedLayer.mappingVersion;
    item.state = UploadState::Complete;
    item.readyTick = tick;

    for (PageEntry& page : pages_) {
        if (page.contentId != item.contentId ||
            page.reservedLayer != item.layer) {
            continue;
        }
        page.state = PageState::Resident;
        page.layer = item.layer;
        page.reservedLayer = kInvalidLayer;
        ++page.mappingVersion;
        resolveTracesForPage(page.id, item.layer, tick);
    }
}

void PagingModel::resolveTracesForPage(int pageId, int layerId, uint64_t tick) {
    for (SwitchTrace& trace : traces_) {
        if (!trace.resolved && trace.pageId == pageId) {
            resolveTrace(trace, tick, layerId);
            trace.outcome = trace.duplicate ? RequestOutcome::DuplicateHit
                                            : RequestOutcome::MissQueued;
        }
    }
}

void PagingModel::resolveTrace(SwitchTrace& trace, uint64_t tick, int finalLayer) {
    trace.resolveTick = tick;
    trace.finalLayer = finalLayer;
    trace.resolved = true;
}

std::vector<std::string> PagingModel::validateInvariants() const {
    std::vector<std::string> errors;
    const int layerCount = static_cast<int>(layers_.size());
    for (const PageEntry& page : pages_) {
        if (page.empty && page.state != PageState::Empty) {
            errors.push_back("empty page has non-empty state: " +
                             std::to_string(page.id));
        }
        if (page.state == PageState::Resident) {
            if (page.layer < 0 || page.layer >= layerCount) {
                errors.push_back("resident page layer out of range: " +
                                 std::to_string(page.id));
            } else {
                const LayerEntry& host = layers_[static_cast<size_t>(page.layer)];
                if (!host.ready || host.contentId != page.contentId) {
                    errors.push_back("resident page/layer content mismatch: " +
                                     std::to_string(page.id));
                }
            }
        } else if (page.layer != kInvalidLayer) {
            errors.push_back("non-resident page keeps a sampled layer: " +
                             std::to_string(page.id));
        }
    }
    for (const LayerEntry& layer : layers_) {
        if (layer.ready && layer.uploading) {
            errors.push_back("layer is ready and uploading: " +
                             std::to_string(layer.id));
        }
        if ((layer.ready || layer.uploading) && layer.contentId < 0) {
            errors.push_back("active layer has no content: " +
                             std::to_string(layer.id));
        }
    }
    for (const SwitchTrace& trace : traces_) {
        if (!trace.resolved) {
            continue;
        }
        if (trace.finalLayer < kInvalidLayer || trace.finalLayer >= layerCount) {
            errors.push_back("trace final layer out of range: " +
                             std::to_string(trace.seq));
        }
        if (trace.empty && trace.finalLayer != kInvalidLayer) {
            errors.push_back("empty trace sampled a layer: " +
                             std::to_string(trace.seq));
        }
        if (!trace.empty && trace.finalLayer == kInvalidLayer) {
            errors.push_back("payload trace has no final layer: " +
                             std::to_string(trace.seq));
        }
    }
    return errors;
}

const char* toString(PageState state) {
    switch (state) {
        case PageState::Empty: return "EMPTY";
        case PageState::Cold: return "COLD";
        case PageState::Queued: return "QUEUED";
        case PageState::Uploading: return "XFER";
        case PageState::Resident: return "RESIDENT";
    }
    return "?";
}

const char* toString(UploadState state) {
    switch (state) {
        case UploadState::Queued: return "QUEUED";
        case UploadState::Uploading: return "XFER";
        case UploadState::Complete: return "READY";
    }
    return "?";
}

const char* toString(RequestOutcome outcome) {
    switch (outcome) {
        case RequestOutcome::EmptyPage: return "EMPTY";
        case RequestOutcome::Hit: return "HIT";
        case RequestOutcome::DuplicateHit: return "DUP";
        case RequestOutcome::MissQueued: return "WAIT";
    }
    return "?";
}

}  // namespace tpd
