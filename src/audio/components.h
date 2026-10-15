#pragma once

#include <memory>

#include "audio/graph.h"

namespace geow2::audio {

// The component classes, by family. Each returns null for a type it does
// not handle.
std::unique_ptr<Component> make_control_component(const AobjRecord& r, const Aobj& def);
std::unique_ptr<Component> make_source_component(const AobjRecord& r, const Aobj& def, const WaveStore* waves);
std::unique_ptr<Component> make_mixer_component(const AobjRecord& r, const Aobj& def);
std::unique_ptr<Component> make_effect_component(const AobjRecord& r, const Aobj& def);

// The audio inputs and outputs that join objects (0x0C, 0x0D, 0xB8,
// 0xB9): an output adds what it pulls to `destination` (an input's
// accumulator, or the renderer's buffer).
class AudioInput : public Component {};
bool connect_audio_output(Component& output, AudioBuffer* destination);
AudioBuffer* audio_input_accumulator(Component& input);

// The game's interface components (0x00, 0x01, 0x02).
bool parameter_in_set(Component& c, float v);
float parameter_out_value(Component& c);
int32_t action_in_target(Component& c);

// The mixers' square root: x * frsqrte(x) refined by two Newton steps.
float core_sqrt(float x);

}  // namespace geow2::audio
