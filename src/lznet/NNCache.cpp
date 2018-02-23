/*
    This file is part of Leela Zero.
    Copyright (C) 2017 Michael O

    Leela Zero is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Leela Zero is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"
#include <functional>

#include "NNCache.h"
#include "Utils.h"

NNCache::NNCache(int size) : m_size(size) {}

NNCache& NNCache::get_NNCache(void) {
    static NNCache cache;
    return cache;
}

template <class T>
inline size_t hash_combine(size_t seed, const T& v) {
    std::hash<T> hasher;
    return seed ^ (hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

static size_t compute_hash(const Network::NNPlanes& features) {
    auto hash = size_t{0};
    for (const auto& p : features) {
        hash = hash_combine(hash, p);
    }
    return hash;
}

bool NNCache::lookup_policy(const Network::NNPlanes& features, Network::Prob& move_prob) {
    auto hash = compute_hash(features);

    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_lookups;

    auto iter = m_policy_cache.find(hash);
    if (iter == m_policy_cache.end()) {
        return false;  // Not found.
    }

    const auto& entry = iter->second;
    if (entry->features != features) {
        // Got a hash collision.
        ++m_collisions;
        return false;
    }

    // Found it.
    ++m_hits;
    move_prob = entry->move_prob;
    return true;
}

bool NNCache::lookup_value(const Network::NNPlanes& features, float& value) {
    auto hash = compute_hash(features);

    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_lookups;

    auto iter = m_value_cache.find(hash);
    if (iter == m_value_cache.end()) {
        return false;  // Not found.
    }

    const auto& entry = iter->second;
    if (entry->features != features) {
        // Got a hash collision.
        ++m_collisions;
        return false;
    }

    // Found it.
    ++m_hits;
    value = entry->value;
    return true;
}

void NNCache::insert_policy(const Network::NNPlanes& features,
                            const Network::Prob& move_prob) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto hash = compute_hash(features);
    if (m_policy_cache.find(hash) != m_policy_cache.end()) {
        return;  // Already in the cache.
    }

    m_policy_cache.emplace(hash, std::make_unique<Policy>(features, move_prob));
    m_policy_order.push_back(hash);
    ++m_inserts;

    // If the cache is too large, remove the oldest entry.
    if (m_policy_order.size() > m_size) {
        m_policy_cache.erase(m_policy_order.front());
        m_policy_order.pop_front();
    }
}

void NNCache::insert_value(const Network::NNPlanes& features,
                           const float& value) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto hash = compute_hash(features);
    if (m_value_cache.find(hash) != m_value_cache.end()) {
        return;  // Already in the cache.
    }

    m_value_cache.emplace(hash, std::make_unique<Value>(features, value));
    m_value_order.push_back(hash);
    ++m_inserts;

    // If the cache is too large, remove the oldest entry.
    if (m_value_order.size() > m_size) {
        m_value_cache.erase(m_value_order.front());
        m_value_order.pop_front();
    }
}

void NNCache::resize(int size) {
    m_size = size;
    while (m_policy_order.size() > m_size) {
        m_policy_cache.erase(m_policy_order.front());
        m_policy_order.pop_front();
    }
    while (m_value_order.size() > m_size) {
        m_value_cache.erase(m_value_order.front());
        m_value_order.pop_front();
    }
}

void NNCache::set_size_from_playouts(int max_playouts) {
    // cache hits are generally from last several moves so setting cache
    // size based on playouts increases the hit rate while balancing memory
    // usage for low playout instances. 50'000 cache entries is ~250 MB
    auto max_size = std::min(50'000, std::max(6'000, 3 * max_playouts));
    NNCache::get_NNCache().resize(max_size);
}

void NNCache::dump_stats() {
    Utils::myprintf("NNCache: %d/%d hits/lookups = %.1f%% hitrate, %d inserts, %u policy, %u value, %d collisions\n",
        m_hits, m_lookups, 100. * m_hits / (m_lookups + 1),
        m_inserts, m_policy_cache.size(), m_value_cache.size(), m_collisions);
}
