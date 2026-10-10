// SPDX-License-Identifier: GPL-2.0-or-later
#include "CamelotTrainer.h"
#include "CamelotClient.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace camelot
{
namespace
{
void Word(Bytes &b, uint16_t n)
{
    b.push_back(uint8_t(n >> 8));
    b.push_back(uint8_t(n));
}
void Long(Bytes &b, uint32_t n)
{
    Word(b, uint16_t(n >> 16));
    Word(b, uint16_t(n));
}
} // namespace
void Client::CloseTrainer()
{
    const auto revision = trainer_.revision + 1;
    trainer_ = {};
    trainer_.revision = revision;
    interactionTarget_ = 0;
    interactionExpires_ = trainingExpires_ = 0;
    trainingName_.clear();
}
bool Client::NearTrainer(uint16_t object) const
{
    const auto npc = players_.find(object), local = players_.find(objectId_);
    if (state_ != State::Playing || combat_.dead || combat_.attacking || !object || combat_.target != object || npc == players_.end() || !npc->second.npc || local == players_.end() || npc->second.zone != local->second.zone)
        return false;
    const double x = double(npc->second.x) - local->second.x;
    const double y = double(npc->second.y) - local->second.y;
    const double z = double(npc->second.z) - local->second.z;
    return x * x + y * y + z * z <= 192.0 * 192.0;
}
bool Client::Interact(uint16_t object, double now)
{
    if (!NearTrainer(object))
        return false;
    CloseTrainer();
    interactionTarget_ = object;
    interactionExpires_ = now + 8;
    const auto &local = players_.at(objectId_);
    Bytes b;
    Long(b, local.x);
    Long(b, local.y);
    Word(b, sessionId_);
    Word(b, object);
    Send(0x7a, b);
    return true;
}
int Client::MaxTrainingRank(const TrainerLine &line, bool realm) const
{
    if (trainer_.refreshRequired || !line.available || !line.nextCost || (realm && line.index > 155))
        return line.level;
    const int points = combat_.progressKnown ? (realm ? combat_.realmSkillPoints : combat_.skillPoints) : (realm ? trainer_.realmPoints : trainer_.specPoints);
    const int limit = realm ? std::min(255, int(line.level) + 1) : std::max(int(line.level), trainingLevel_ ? std::min(50, int(trainingLevel_)) : int(line.level) + 1);
    int desired = line.level;
    while (desired < limit && TrainingCost(line.level, desired + 1, line.nextCost, realm) <= points)
        ++desired;
    return desired;
}
bool Client::Train(bool realm, uint8_t index, uint8_t desired, uint64_t revision, double now)
{
    if (!trainer_.open || trainer_.pending || trainer_.refreshRequired || trainer_.revision != revision || !NearTrainer(trainer_.object) || !(realm ? trainer_.realmKnown : trainer_.specsKnown))
        return false;
    const auto &lines = realm ? trainer_.realm : trainer_.specs;
    const auto line = std::find_if(lines.begin(), lines.end(), [index](const auto &l) { return l.index == index; });
    if (line == lines.end() || desired <= line->level || desired > MaxTrainingRank(*line, realm))
        return false;
    trainingRealm_ = realm;
    trainingIndex_ = index;
    trainingFrom_ = line->level;
    trainingDesired_ = desired;
    trainingName_ = line->name;
    trainer_.pending = true;
    trainer_.feedback = "Training...";
    ++trainer_.revision;
    return SendTrainingRank(now);
}
bool Client::SendTrainingRank(double now)
{
    if (!NearTrainer(trainer_.object))
    {
        CloseTrainer();
        return false;
    }
    const auto &local = players_.at(objectId_);
    Bytes b;
    Long(b, local.x);
    Long(b, local.y);
    b.insert(b.end(), {0, 0, 0, uint8_t(trainingIndex_ + (trainingRealm_ ? 100 : 0))});
    Send(0x7c, b);
    trainingExpires_ = now + 5;
    return true;
}
void Client::TickTrainer(double now)
{
    if ((trainer_.open || interactionTarget_) && !NearTrainer(trainer_.open ? trainer_.object : interactionTarget_))
        CloseTrainer();
    if (!trainer_.open && interactionTarget_ && now > interactionExpires_)
        CloseTrainer();
    if (trainer_.pending && now > trainingExpires_)
    {
        trainer_.pending = false;
        ++trainer_.revision;
        trainer_.refreshRequired = true;
        trainer_.feedback = "Training was not confirmed. Check the server message "
                            "and interact again.";
        // A late confirmation must not restart the allocation queue.
    }
}
bool Client::HandleTrainer(uint8_t op, const Bytes &b, double now)
{
    // VariousUpdate subtype 3 is the live player sheet, including i50 level-ups.
    if (op == 0x16 && b.size() >= 6 && b[0] == 3 && b[2] == 0)
    {
        size_t p = 4;
        for (size_t i = 0; i < b[1]; ++i)
        {
            if (b.size() - p < 2)
                return true;
            ++p;
            const size_t length = b[p++];
            if (length > b.size() - p)
                return true;
            p += length;
        }
        if (!b[1] || p != b.size() || !b[4])
            return true;
        trainingLevel_ = b[4];
        if (trainer_.open)
            ++trainer_.revision;
        const auto local = players_.find(objectId_);
        if (local != players_.end())
            local->second.appearance.level = trainingLevel_;
        return true;
    }
    if (op != 0x7b)
        return false;
    if (b.size() < 4 || b[2] > 1)
        return true; // Champion/modern preview lists have other formats.
    const bool realm = b[2] == 1;
    std::vector<TrainerLine> lines;
    size_t p = 4;
    for (size_t i = 0; i < b[0]; ++i)
    {
        if (b.size() - p < 4)
            return true;
        TrainerLine line;
        line.index = b[p++];
        line.level = b[p++];
        line.nextCost = b[p++];
        const size_t length = b[p++];
        if (length > b.size() - p)
            return true;
        line.name.assign(b.begin() + p, b.begin() + p + length);
        p += length;
        if (line.name.empty() || line.index >= (realm ? 156 : 100) || std::any_of(lines.begin(), lines.end(), [&](const auto &l) { return l.index == line.index; }))
            return true;
        line.available = !(line.name.front() == '[' && line.name.back() == ']');
        lines.push_back(std::move(line));
    }
    if (p != b.size())
        return true; // Commit only complete, validated snapshots.
    TickTrainer(now);
    const uint16_t object = trainer_.open ? trainer_.object : interactionTarget_;
    if (!object || !NearTrainer(object) || (!trainer_.open && now > interactionExpires_))
        return true;
    trainer_.object = object;
    trainer_.open = true;
    ++trainer_.revision;
    (realm ? trainer_.realm : trainer_.specs) = std::move(lines);
    (realm ? trainer_.realmKnown : trainer_.specsKnown) = true;
    (realm ? trainer_.realmPoints : trainer_.specPoints) = b[1];
    if (!trainer_.pending || realm != trainingRealm_)
        return true;
    const auto &received = realm ? trainer_.realm : trainer_.specs;
    const auto line = std::find_if(received.begin(), received.end(), [&](const auto &l) { return l.index == trainingIndex_; });
    if (line == received.end() || line->name != trainingName_ || line->level != trainingFrom_ + 1)
    {
        // An unchanged refresh is not an acknowledgement. A changed list cancels
        // the queue.
        if (line != received.end() && line->name == trainingName_ && line->level == trainingFrom_)
            return true;
        trainer_.pending = false;
        trainer_.feedback = "Training options changed. Review your allocation.";
        return true;
    }
    trainingFrom_ = line->level;
    if (line->level >= trainingDesired_)
    {
        trainer_.pending = false;
        trainer_.feedback = "Training complete.";
        return true;
    }
    if (MaxTrainingRank(*line, realm) < trainingDesired_)
    {
        trainer_.pending = false;
        trainer_.feedback = "Training stopped. Review the remaining points.";
        return true;
    }
    SendTrainingRank(now);
    return true;
}
} // namespace camelot
