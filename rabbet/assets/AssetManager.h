#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/assets/AssetMeta.h"
#include "rabbet/core/TypeId.h"
#include "rabbet/core/Uuid.h"

namespace rb {

namespace detail {

struct AssetFamily {};

template <typename T>
[[nodiscard]] std::size_t assetTypeId() noexcept {
    return typeId<AssetFamily, T>();
}

class IAssetStore {
public:
    IAssetStore() = default;
    IAssetStore(const IAssetStore&) = delete;
    IAssetStore& operator=(const IAssetStore&) = delete;
    IAssetStore(IAssetStore&&) = delete;
    IAssetStore& operator=(IAssetStore&&) = delete;
    virtual ~IAssetStore() = default;
};

template <typename T>
class AssetStore final : public IAssetStore {
public:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 0;
        Uuid id;
    };

    std::uint32_t insert(T value, Uuid id) {
        if (!m_free.empty()) {
            const std::uint32_t index = m_free.back();
            m_free.pop_back();
            Slot& slot = m_slots[index];
            slot.value.emplace(std::move(value));
            slot.id = id;
            return index;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(m_slots.size());
        Slot& slot = m_slots.emplace_back();
        slot.value.emplace(std::move(value));
        slot.id = id;
        return index;
    }

    [[nodiscard]] T* get(std::uint32_t index, std::uint32_t generation) {
        Slot* slot = at(index, generation);
        return slot != nullptr ? &*slot->value : nullptr;
    }

    [[nodiscard]] std::uint32_t generation(std::uint32_t index) const {
        return index < m_slots.size() ? m_slots[index].generation : 0u;
    }

    [[nodiscard]] Uuid uuidAt(std::uint32_t index, std::uint32_t generation) {
        const Slot* slot = at(index, generation);
        return slot != nullptr ? slot->id : Uuid{};
    }

    void erase(std::uint32_t index, std::uint32_t generation) {
        if (Slot* slot = at(index, generation)) {
            slot->value.reset();
            ++slot->generation;
            m_free.push_back(index);
        }
    }

    [[nodiscard]] std::size_t size() const {
        std::size_t count = 0;
        for (const Slot& slot : m_slots) {
            if (slot.value.has_value()) {
                ++count;
            }
        }
        return count;
    }

private:
    Slot* at(std::uint32_t index, std::uint32_t generation) {
        if (index >= m_slots.size()) {
            return nullptr;
        }
        Slot& slot = m_slots[index];
        if (!slot.value.has_value() || slot.generation != generation) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_free;
};

} // namespace detail

// Owns assets keyed by generational handles, with a stable UUID per asset and a
// path-keyed load cache. Loading the same path twice returns the same handle.
class AssetManager {
public:
    template <typename T>
    AssetHandle<T> add(T asset, Uuid id = Uuid::generate()) {
        detail::AssetStore<T>& store = storeFor<T>();
        const std::uint32_t index = store.insert(std::move(asset), id);
        const AssetHandle<T> handle{index, store.generation(index)};
        if (id.valid()) {
            m_located[id] = Located{detail::assetTypeId<T>(), index, handle.generation};
        }
        return handle;
    }

    template <typename T>
    [[nodiscard]] T* get(AssetHandle<T> handle) {
        if (!handle.valid()) {
            return nullptr;
        }
        detail::AssetStore<T>* store = tryStoreFor<T>();
        return store != nullptr ? store->get(handle.index, handle.generation) : nullptr;
    }

    template <typename T>
    [[nodiscard]] bool valid(AssetHandle<T> handle) {
        return get<T>(handle) != nullptr;
    }

    template <typename T>
    void erase(AssetHandle<T> handle) {
        detail::AssetStore<T>* store = tryStoreFor<T>();
        if (store == nullptr || !handle.valid()) {
            return;
        }
        const Uuid id = store->uuidAt(handle.index, handle.generation);
        store->erase(handle.index, handle.generation);
        if (id.valid()) {
            m_located.erase(id);
        }
    }

    template <typename T>
    [[nodiscard]] std::size_t count() {
        detail::AssetStore<T>* store = tryStoreFor<T>();
        return store != nullptr ? store->size() : 0u;
    }

    template <typename T>
    [[nodiscard]] Uuid uuidOf(AssetHandle<T> handle) {
        detail::AssetStore<T>* store = tryStoreFor<T>();
        return store != nullptr ? store->uuidAt(handle.index, handle.generation) : Uuid{};
    }

    template <typename T>
    [[nodiscard]] AssetHandle<T> find(Uuid id) {
        const auto it = m_located.find(id);
        if (it == m_located.end() || it->second.typeId != detail::assetTypeId<T>()) {
            return AssetHandle<T>{};
        }
        return AssetHandle<T>{it->second.index, it->second.generation};
    }

    template <typename T, typename Importer>
    AssetHandle<T> load(const std::filesystem::path& path, Importer&& importer) {
        const Uuid id = assetmeta::loadOrCreateUuid(path);
        if (const AssetHandle<T> existing = find<T>(id); existing.valid()) {
            return existing;
        }
        std::optional<T> imported = std::forward<Importer>(importer)(path);
        if (!imported.has_value()) {
            return AssetHandle<T>{};
        }
        return add<T>(std::move(*imported), id);
    }

private:
    struct Located {
        std::size_t typeId = 0;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    template <typename T>
    detail::AssetStore<T>& storeFor() {
        const std::size_t id = detail::assetTypeId<T>();
        if (id >= m_stores.size()) {
            m_stores.resize(id + 1);
        }
        if (!m_stores[id]) {
            m_stores[id] = std::make_unique<detail::AssetStore<T>>();
        }
        return *static_cast<detail::AssetStore<T>*>(m_stores[id].get());
    }

    template <typename T>
    detail::AssetStore<T>* tryStoreFor() {
        const std::size_t id = detail::assetTypeId<T>();
        if (id >= m_stores.size() || !m_stores[id]) {
            return nullptr;
        }
        return static_cast<detail::AssetStore<T>*>(m_stores[id].get());
    }

    std::vector<std::unique_ptr<detail::IAssetStore>> m_stores;
    std::unordered_map<Uuid, Located> m_located;
};

} // namespace rb
