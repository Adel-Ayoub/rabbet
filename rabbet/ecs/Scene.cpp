#include "rabbet/ecs/Scene.h"

namespace rb {

Entity Scene::create() {
    Entity::Index index;
    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        index = static_cast<Entity::Index>(m_versions.size());
        m_versions.push_back(0u);
    }
    ++m_aliveCount;
    return Entity{index, m_versions[index]};
}

void Scene::destroy(Entity e) {
    if (!alive(e)) {
        return;
    }
    const Entity::Index index = e.index();
    for (std::unique_ptr<IPool>& p : m_pools) {
        if (p && p->contains(index)) {
            p->remove(index);
        }
    }
    ++m_versions[index];
    m_freeIndices.push_back(index);
    --m_aliveCount;
}

bool Scene::alive(Entity e) const noexcept {
    const Entity::Index index = e.index();
    return index < m_versions.size() && m_versions[index] == e.version();
}

std::vector<Entity> Scene::entities() const {
    std::vector<bool> isFree(m_versions.size(), false);
    for (const Entity::Index index : m_freeIndices) {
        assert(index < isFree.size());
        isFree[index] = true;
    }
    std::vector<Entity> result;
    result.reserve(m_aliveCount);
    for (Entity::Index index = 0; index < m_versions.size(); ++index) {
        if (!isFree[index]) {
            result.push_back(Entity{index, m_versions[index]});
        }
    }
    return result;
}

void Scene::clear() {
    // Bump every slot's version so any handle taken before the clear is rejected by
    // alive(), then free all indices for reuse. Resetting versions to 0 instead would
    // let a stale pre-clear handle alias a freshly created entity.
    m_pools.clear();
    m_freeIndices.clear();
    m_aliveCount = 0;
    for (Entity::Index index = 0; index < m_versions.size(); ++index) {
        ++m_versions[index];
        m_freeIndices.push_back(index);
    }
}

} // namespace rb
