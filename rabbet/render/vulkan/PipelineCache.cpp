#include "rabbet/render/vulkan/PipelineCache.h"

#include "rabbet/render/vulkan/Device.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace rb::vulkan {

namespace {

constexpr std::size_t maximumCacheBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t saveAttempts = 4;

// The file wraps the driver blob in magic, payload length and payload hash so a
// truncated or edited file is rejected before vkCreatePipelineCache sees it.
constexpr std::array<std::uint8_t, 4> cacheMagic{'R', 'B', 'P', 'C'};
constexpr std::size_t envelopeBytes = cacheMagic.size() + sizeof(std::uint64_t) * 2U;

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using File = std::unique_ptr<std::FILE, FileCloser>;

std::vector<std::uint8_t> readCacheFile(const std::string& path, bool& present) {
    present = false;
    File file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        return {};
    }
    present = true;
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        return {};
    }
    const long size = std::ftell(file.get());
    if (size <= 0 || static_cast<std::size_t>(size) > maximumCacheBytes ||
        std::fseek(file.get(), 0, SEEK_SET) != 0) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()) {
        return {};
    }
    return bytes;
}

bool cacheMatchesDevice(std::span<const std::uint8_t> payload,
                        const VkPhysicalDeviceProperties& properties) {
    VkPipelineCacheHeaderVersionOne header{};
    if (payload.size() < sizeof(header)) {
        return false;
    }
    std::memcpy(&header, payload.data(), sizeof(header));
    return header.headerSize == sizeof(header) &&
           header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
           std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

std::span<const std::uint8_t> envelopePayload(const std::vector<std::uint8_t>& fileBytes,
                                              const VkPhysicalDeviceProperties& properties) {
    if (fileBytes.size() < envelopeBytes ||
        std::memcmp(fileBytes.data(), cacheMagic.data(), cacheMagic.size()) != 0) {
        return {};
    }
    std::uint64_t payloadSize = 0;
    std::uint64_t storedHash = 0;
    std::memcpy(&payloadSize, fileBytes.data() + cacheMagic.size(), sizeof(payloadSize));
    std::memcpy(&storedHash, fileBytes.data() + cacheMagic.size() + sizeof(payloadSize),
                sizeof(storedHash));
    if (payloadSize != fileBytes.size() - envelopeBytes || payloadSize == 0U) {
        return {};
    }
    const std::span<const std::uint8_t> payload{fileBytes.data() + envelopeBytes,
                                                static_cast<std::size_t>(payloadSize)};
    if (fnv1a64(payload) != storedHash || !cacheMatchesDevice(payload, properties)) {
        return {};
    }
    return payload;
}

}

std::unique_ptr<PipelineCache> PipelineCache::create(const Device& device,
                                                     const std::string& path) {
    bool present = false;
    const std::vector<std::uint8_t> bytes = readCacheFile(path, present);
    PipelineCacheLoad loadResult = PipelineCacheLoad::fresh;
    std::span<const std::uint8_t> payload;
    if (present) {
        payload = envelopePayload(bytes, device.properties());
        loadResult = payload.empty() ? PipelineCacheLoad::rejected : PipelineCacheLoad::loaded;
    }

    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (loadResult == PipelineCacheLoad::loaded) {
        createInfo.initialDataSize = payload.size();
        createInfo.pInitialData = payload.data();
    }
    VkPipelineCache cache = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(device.handle(), &createInfo, nullptr, &cache) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan pipeline cache creation failed\n");
        return nullptr;
    }
    return std::unique_ptr<PipelineCache>(
        new PipelineCache(device.handle(), cache, path, loadResult, payload.size()));
}

PipelineCache::PipelineCache(VkDevice device, VkPipelineCache cache, std::string path,
                             PipelineCacheLoad loadResult, std::size_t loadedBytes) noexcept
    : m_device(device), m_cache(cache), m_path(std::move(path)), m_loadResult(loadResult),
      m_loadedBytes(loadedBytes) {}

PipelineCache::~PipelineCache() {
    if (m_cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_device, m_cache, nullptr);
    }
}

VkPipelineCache PipelineCache::handle() const noexcept {
    return m_cache;
}

PipelineCacheLoad PipelineCache::loadResult() const noexcept {
    return m_loadResult;
}

std::size_t PipelineCache::loadedBytes() const noexcept {
    return m_loadedBytes;
}

bool PipelineCache::save() {
    std::vector<std::uint8_t> bytes;
    bool captured = false;
    for (std::size_t attempt = 0; attempt < saveAttempts; ++attempt) {
        std::size_t size = 0;
        if (vkGetPipelineCacheData(m_device, m_cache, &size, nullptr) != VK_SUCCESS) {
            break;
        }
        if (size == 0U) {
            std::fprintf(stderr, "Vulkan pipeline cache produced no data to save\n");
            return false;
        }
        bytes.resize(size);
        const VkResult result = vkGetPipelineCacheData(m_device, m_cache, &size, bytes.data());
        if (result == VK_SUCCESS) {
            bytes.resize(size);
            captured = true;
            break;
        }
        if (result != VK_INCOMPLETE) {
            break;
        }
    }
    if (!captured) {
        std::fprintf(stderr, "Vulkan pipeline cache data retrieval failed\n");
        return false;
    }

    const std::string temporaryPath = m_path + ".tmp";
    std::FILE* file = std::fopen(temporaryPath.c_str(), "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "Vulkan pipeline cache file creation failed\n");
        return false;
    }
    const std::uint64_t payloadSize = bytes.size();
    const std::uint64_t payloadHash = fnv1a64(bytes);
    const bool written =
        std::fwrite(cacheMagic.data(), 1, cacheMagic.size(), file) == cacheMagic.size() &&
        std::fwrite(&payloadSize, 1, sizeof(payloadSize), file) == sizeof(payloadSize) &&
        std::fwrite(&payloadHash, 1, sizeof(payloadHash), file) == sizeof(payloadHash) &&
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    // fclose flushes buffered bytes, so its result decides whether the file is whole.
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::fprintf(stderr, "Vulkan pipeline cache file write failed\n");
        std::remove(temporaryPath.c_str());
        return false;
    }
    if (std::rename(temporaryPath.c_str(), m_path.c_str()) != 0) {
        std::fprintf(stderr, "Vulkan pipeline cache file replacement failed\n");
        std::remove(temporaryPath.c_str());
        return false;
    }
    m_savedBytes = bytes.size();
    return true;
}

std::size_t PipelineCache::savedBytes() const noexcept {
    return m_savedBytes;
}

}
