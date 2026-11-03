#include "engine/core/ProfilerFrameRegions.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace kb::core {
namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

struct Node {
    ProfilerRegionRow row;
    std::size_t parent = kNone;
    std::vector<std::size_t> children;
};

void Flatten(const std::vector<Node>& nodes, std::size_t index, std::vector<ProfilerRegionRow>& out) {
    out.push_back(nodes[index].row);
    for (const std::size_t child : nodes[index].children) Flatten(nodes, child, out);
}

} // namespace

std::vector<ProfilerRegionRow> ProfilerFrameRegions::Aggregate(const std::vector<ProfilerTimelineEvent>& timeline) {
    // A region is recorded when it ends, so the order of the timeline puts children first; the start
    // time and the depth put every region after the one that contains it.
    std::vector<const ProfilerTimelineEvent*> events;
    events.reserve(timeline.size());
    for (const ProfilerTimelineEvent& event : timeline) events.push_back(&event);
    std::stable_sort(events.begin(), events.end(), [](const ProfilerTimelineEvent* a, const ProfilerTimelineEvent* b) {
        return a->timestampNanoseconds != b->timestampNanoseconds ? a->timestampNanoseconds < b->timestampNanoseconds : a->depth < b->depth;
    });

    std::vector<Node> nodes;
    std::vector<std::size_t> roots;
    std::map<std::pair<std::size_t, std::string>, std::size_t> byParentAndName;
    std::vector<std::size_t> open;  // the regions that contain the next one
    for (const ProfilerTimelineEvent* event : events) {
        while (!open.empty() && nodes[open.back()].row.depth >= event->depth) open.pop_back();
        const std::size_t parent = open.empty() ? kNone : open.back();
        const auto key = std::make_pair(parent, event->name);
        const auto found = byParentAndName.find(key);
        std::size_t index = 0U;
        if (found == byParentAndName.end()) {
            index = nodes.size();
            nodes.push_back(Node{ .row = ProfilerRegionRow{ .name = event->name, .depth = event->depth }, .parent = parent });
            byParentAndName.emplace(key, index);
            (parent == kNone ? roots : nodes[parent].children).push_back(index);
        } else {
            index = found->second;
        }
        nodes[index].row.totalNanoseconds += event->durationNanoseconds;
        ++nodes[index].row.calls;
        if (event->waiting) nodes[index].row.waitNanoseconds += event->durationNanoseconds;
        nodes[index].row.waiting = nodes[index].row.calls == 1U ? event->waiting : nodes[index].row.waiting && event->waiting;
        open.push_back(index);
    }
    // Children are created after their parents, so walking backwards adds each row's waiting time to its parent.
    for (std::size_t index = nodes.size(); index-- > 0U;) {
        Node& node = nodes[index];
        std::uint64_t inside = 0U;
        for (const std::size_t child : node.children) inside += nodes[child].row.totalNanoseconds;
        node.row.selfNanoseconds = node.row.totalNanoseconds > inside ? node.row.totalNanoseconds - inside : 0U;
        if (node.parent != kNone && !node.row.waiting) nodes[node.parent].row.waitNanoseconds += node.row.waitNanoseconds;
        else if (node.parent != kNone) nodes[node.parent].row.waitNanoseconds += node.row.totalNanoseconds;
    }
    std::vector<ProfilerRegionRow> rows;
    rows.reserve(nodes.size());
    for (const std::size_t root : roots) Flatten(nodes, root, rows);
    return rows;
}

void ProfilerFrameRegions::Capture(std::uint64_t frame, const std::vector<ProfilerTimelineEvent>& timeline) {
    frames_.push_back(ProfilerFrameRecord{ .frame = frame, .rows = Aggregate(timeline) });
    if (frames_.size() > HistoryFrames()) frames_.pop_front();
}

const ProfilerFrameRecord* ProfilerFrameRegions::Find(std::uint64_t frame) const noexcept {
    const auto found = std::ranges::find(frames_, frame, &ProfilerFrameRecord::frame);
    return found == frames_.end() ? nullptr : &*found;
}

} // namespace kb::core
