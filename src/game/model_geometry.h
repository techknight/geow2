#pragma once

#include <vector>

#include "data/flask.h"
#include "game/math.h"

namespace geow2 {

// One line of a model part, as sub_820623B0 records it: both endpoints
// (w = 1 before the transform) and the colour premultiplied by its alpha,
// alpha stored as 1.
struct ModelLine {
  Vec4 p0, p1;
  float r, g, b, a;
};

struct ModelGeometry {
  std::vector<std::vector<ModelLine>> parts;  // one list per Model.parts entry
};

// Builds a Model's line lists the way sub_820628C0 does: the Model's
// transform list composed onto the identity, then every section of every
// part walked (sub_82062460). `model` is a decoded Model struct.
ModelGeometry build_model_geometry(const FlaskStruct& model);

}  // namespace geow2
