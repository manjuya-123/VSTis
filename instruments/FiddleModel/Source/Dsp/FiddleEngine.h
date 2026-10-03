#pragma once

#include <cstddef>

namespace fiddle
{
struct Controls
{
    float pressure = 0.5f;
    float speed = 0.5f;
    float attack = 0.5f;
    float position = 0.5f;
    float balance = 0.0f;
};

class FiddleEngine
{
public:
    void prepare(double sampleRate);
    void reset();
    void noteOn(float frequencyHz, float velocity);
    void noteOff();
    void setControls(const Controls& controls) noexcept;
    void process(float* left, float* right, std::size_t numSamples) noexcept;

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float frequencyHz_ = 440.0f;
    float velocity_ = 0.0f;
    float envelope_ = 0.0f;
    bool gate_ = false;
    Controls controls_{};
};
}
