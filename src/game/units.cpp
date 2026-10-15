#include "game/units.h"

namespace geow2 {
namespace {

const FlaskStruct* as_struct(const FlaskValue* v) {
  auto* p = v ? std::get_if<std::shared_ptr<FlaskStruct>>(&v->v) : nullptr;
  return p ? p->get() : nullptr;
}

float as_float(const FlaskValue* v) {
  auto* d = v ? std::get_if<double>(&v->v) : nullptr;
  return d ? float(*d) : 0.0f;
}

void colour(const FlaskValue* v, float out[4]) {
  if (const FlaskStruct* c = as_struct(v)) {
    out[0] = as_float(c->find("r"));
    out[1] = as_float(c->find("g"));
    out[2] = as_float(c->find("b"));
    out[3] = as_float(c->find("a"));
  }
}

UnitStyle style(const FlaskValue* v) {
  // The schema's enum order: None, TiltVelocity, TiltPlayer, Custom.
  auto* s = v ? std::get_if<std::string>(&v->v) : nullptr;
  if (!s) return UnitStyle::kNone;
  if (*s == "TiltVelocity") return UnitStyle::kTiltVelocity;
  if (*s == "TiltPlayer") return UnitStyle::kTiltPlayer;
  if (*s == "Custom") return UnitStyle::kCustom;
  return UnitStyle::kNone;
}

}  // namespace

Units Units::load(const Flask& db, const GameMath& gm) {
  Units u;
  for (uint32_t r = 0; r < db.records().size(); ++r) {
    if (db.types()[db.records()[r].type].name != "Unit") continue;
    const FlaskValue value = db.decode(r);
    const FlaskStruct* s = as_struct(&value);
    if (!s) continue;
    UnitInfo info;
    const FlaskValue* name = s->find("name");
    if (auto* n = name ? std::get_if<std::string>(&name->v) : nullptr) info.name = *n;
    info.style = style(s->find("style"));
    if (const FlaskStruct* up = as_struct(s->find("up")))
      info.up = {as_float(up->find("x")), as_float(up->find("y")), as_float(up->find("z")), 0.0f};
    colour(s->find("tint"), info.tint);
    colour(s->find("glowColour"), info.glow);
    info.glow_size = as_float(s->find("glowSize"));
    if (const FlaskStruct* k = as_struct(s->find("killed"))) {
      if (k->type == "EffectExplosion") info.killed.kind = UnitEffect::kExplosion;
      if (k->type == "EffectRandomExplosion") info.killed.kind = UnitEffect::kRandomExplosion;
      const FlaskValue* count = k->find("count");
      if (auto* c = count ? std::get_if<int64_t>(&count->v) : nullptr) info.killed.count = int(*c);
      info.killed.radius = as_float(k->find("radius"));
      colour(k->find("colour0"), info.killed.colour0);
      colour(k->find("colour1"), info.killed.colour1);
    }
    const FlaskValue* model_ref = s->find("model");
    if (auto* ref = model_ref ? std::get_if<FlaskRef>(&model_ref->v) : nullptr) {
      const FlaskValue model = db.decode(ref->record);
      if (const FlaskStruct* m = as_struct(&model)) {
        info.model = build_model_geometry(*m, gm);
        info.has_model = !info.model.parts.empty();
      }
    }
    u.units_.push_back(std::move(info));
  }
  return u;
}

const UnitInfo* Units::find(std::string_view name) const {
  for (const UnitInfo& u : units_)
    if (u.name == name) return &u;
  return nullptr;
}

}  // namespace geow2
