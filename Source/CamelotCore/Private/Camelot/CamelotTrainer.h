// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace camelot
{
/// Native server training entry with its category-local index and next-rank cost.
struct TrainerLine
{
    uint8_t index = 0, level = 0, nextCost = 0;
    bool available = true;
    std::string name;
};
/// Native trainer interaction, choices, acknowledgement queue and refresh state.
struct TrainerState
{
    uint64_t revision = 0;
    uint16_t object = 0, specPoints = 0, realmPoints = 0;
    bool open = false, specsKnown = false, realmKnown = false, pending = false, refreshRequired = false;
    std::vector<TrainerLine> specs, realm;
    std::string feedback;
};
/// Estimates specialization cost or one realm upgrade; invalid allocations return -1.
inline int TrainingCost(int current, int desired, int nextCost, bool realm)
{
    if (current < 0 || desired < current || desired > 255 || nextCost < 0)
        return -1;
    if (desired == current)
        return 0;
    if (realm)
        return desired == current + 1 ? nextCost : -1;
    const int ranks = desired - current;
    return ranks * nextCost + ranks * (ranks - 1) / 2;
}
} // namespace camelot
