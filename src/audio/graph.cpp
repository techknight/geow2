#include "audio/graph.h"

#include <cstdio>

#include "audio/components.h"
#include "extract/bytes.h"

namespace geow2::audio {

namespace {

// A component type the parser knows but no shipped object uses: it loads,
// takes no part in the update and has no output.
class Inert : public Component {};

}  // namespace

Triple triple_at(const AobjRecord& r, size_t k) {
  if (k + 2 >= r.body.size()) throw FormatError("aobj: triple past the end of a record");
  return {int32_t(r.body[k]), r.body[k + 1], word_float(r.body[k + 2])};
}

Triple pair_at(const AobjRecord& r, size_t k) {
  if (k + 1 >= r.body.size()) throw FormatError("aobj: pair past the end of a record");
  return {int32_t(r.body[k]), r.body[k + 1], 0.0f};
}

Object::Object(SharedState& s, const Aobj& def, const WaveStore* waves) : shared(s), name(def.name) {
  comps.reserve(def.records.size());
  for (const AobjRecord& r : def.records) {
    std::unique_ptr<Component> c = make_control_component(r, def);
    if (!c) c = make_source_component(r, def, waves);
    if (!c) c = make_mixer_component(r, def);
    if (!c) c = make_effect_component(r, def);
    if (!c) c = std::make_unique<Inert>();
    c->type = r.type;
    c->index = uint32_t(comps.size());
    c->name = r.body.empty() ? std::string() : def.string_at(r.body[0]);
    comps.push_back(std::move(c));
  }
  // sub_82105DC8: list membership from the flags, in component order.
  for (auto& c : comps) {
    const uint32_t f = c->flags();
    if (f & kFlagScript) scripts_.push_back(c.get());
    else if (f & kFlagUpdate) updated_.push_back(c.get());
    if (f & kFlagGenerate) generate_.push_back(c.get());
    if (f & kFlagPrepare) prepare_.push_back(c.get());
  }
  for (auto& c : comps) c->init_state(*this);
}

float Object::dt() const { return (float(n) * 2.08333331e-05f) * 256.0f; }

void Object::start(int32_t c) {
  if (c < 0 || size_t(c) >= comps.size()) return;
  Component& t = *comps[size_t(c)];
  if (t.flags() & kFlagStartable) t.start(*this);
  // else: the original logs that the target is not an event and does nothing.
}

void Object::event(int32_t c, uint32_t pin) {
  if (c < 0 || size_t(c) >= comps.size()) return;
  comps[size_t(c)]->receive_event(*this, pin);
}

bool Object::pull(int32_t c) {
  if (c < 0 || size_t(c) >= comps.size()) return false;
  Component& t = *comps[size_t(c)];
  if (t.out_channels() == 0) return false;
  t.out.channels = t.out_channels();
  return t.render(*this);
}

void Object::generate_frame() {
  // sub_820F6368: the control-period position and the ramp context.
  sub = uint16_t(sub + 1);
  if (sub >= kUpdatePeriod) sub = 0;
  n = kUpdatePeriod;
  ramp_span = 1.0f / float(kUpdatePeriod);  // fres((float)N), exact for N = 2
  ramp_start = ramp_span * float(sub);
  for (Component* c : prepare_) c->prepare(*this);
  for (Component* c : generate_) c->generate(*this);
}

void Object::control_update() {
  for (Component* c : scripts_) c->update(*this);
  for (Component* c : updated_) c->update(*this);
}

int32_t Object::find(const std::string& wanted, uint32_t flag) const {
  for (size_t i = 0; i < comps.size(); ++i)
    if ((comps[i]->flags() & flag) && comps[i]->name == wanted) return int32_t(i);
  return -1;
}

}  // namespace geow2::audio
