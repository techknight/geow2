#pragma once

#include <vector>

#include "game/bullets.h"
#include "game/world.h"

namespace geow2 {

// sub_820523B0's per-shot hit test (sub_82052090): each shot sweeps from
// where it started the step to where it is, and the first entity it
// touches (touchable, not removed, not its owner) is hit; the shot moves to
// the touching point and is removed.
void shot_collisions(World& w, std::vector<Bullet>& shots);

}  // namespace geow2
