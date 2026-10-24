#include "TestSupport.hpp"
#include "engine/ecs/World.hpp"
#include "ecs/relation/HierarchyDepthGuard.hpp"

#include <iostream>
#include <array>
#include <vector>

namespace kb::tests {

void RunEcsHierarchyDepthTests() {
    kb::ecs::World world;
    constexpr std::size_t depthLimit = 8192U;
    kb::tests::Require(kb::ecs::HierarchyDepthLimit() == depthLimit, "The supported entity hierarchy depth must remain 8192 parent links");
    std::vector<kb::ecs::Entity> chain;
    chain.reserve(depthLimit + 1U);
    chain.push_back(world.CreateEntity());
    for (std::size_t depth = 1U; depth <= depthLimit; ++depth) {
        const auto child = world.CreateEntity();
        world.SetParent(child, chain.back());
        kb::tests::Require(world.Parent(child) == chain.back(), "The supported hierarchy depth must remain usable");
        chain.push_back(child);
    }
    std::cout << "ecs_hierarchy_depth supported=" << depthLimit << std::endl;
    const auto extra = world.CreateEntity();
    world.SetParent(extra, chain.back());
    kb::tests::Require(!world.Parent(extra).IsValid(), "Excessive hierarchy depth must be rejected before backend mutation");
    world.SetParent(extra, chain.front());
    const auto above = world.CreateEntity();
    world.SetParent(chain.front(), above);
    kb::tests::Require(!world.Parent(chain.front()).IsValid(), "Hierarchy depth validation must include the child's descendants");
    const std::array children{extra, chain.front()};
    const std::array parents{chain.back(), above};
    world.SetParents(children, parents);
    kb::tests::Require(world.Parent(extra) == chain.front() && !world.Parent(chain.front()).IsValid(),
        "Rejected batch parenting must preserve old parents and account for descendants");
    const auto branch = world.CreateEntity();
    const auto branchLeaf = world.CreateEntity();
    world.SetParent(branchLeaf, branch);
    const auto retiredChild = world.CreateEntity();
    world.SetParent(retiredChild, branchLeaf);
    world.DestroyEntity(retiredChild);
    world.SetParent(branch, chain[depthLimit - 2U]);
    kb::tests::Require(world.Parent(branch) == chain[depthLimit - 2U] && world.Parent(branchLeaf) == branch,
        "A subtree ending exactly at the supported depth must remain usable");
    world.SetParent(branch, chain[depthLimit - 1U]);
    kb::tests::Require(world.Parent(branch) == chain[depthLimit - 2U],
        "Excessive subtree depth must preserve the previously assigned parent");
    const std::array mixedChildren{extra, branch};
    const std::array mixedParents{chain.back(), chain[depthLimit - 3U]};
    world.SetParents(mixedChildren, mixedParents);
    kb::tests::Require(world.Parent(extra) == chain.front() && world.Parent(branch) == chain[depthLimit - 3U],
        "Batch depth validation must retain supported edges and refuse excessive edges in order");
    world.DestroyEntity(branchLeaf);
    world.DestroyEntity(branch);
    world.DestroyEntity(extra);
    world.DestroyEntity(above);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) world.DestroyEntity(*it);
}

} // namespace kb::tests
