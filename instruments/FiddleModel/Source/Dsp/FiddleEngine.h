#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace fiddle
{
struct Controls
{
    float pressure = 0.5f;
    float speed = 0.5f;
    float attack = 0.5f;
    float position = 0.5f;
    float balance = 0.0f;
    float vibratoWidth = 0.0f;
    float vibratoPace = 0.5f;
};

enum class BodyMaterialPreset
{
    Traditional = 0,
    LightStiffComposite,
    DenseExperimental,
    RigidComposite
};

enum class BowStickPreset
{
    PernambucoLike = 0,
    CarbonLike,
    LightRigidExperimental,
    FlexibleExperimental
};

enum class ContactMaterialPreset
{
    HorsehairMediumRosin = 0,
    DryLightGrip,
    HighGripRosin,
    SyntheticHair
};

struct MaterialSettings
{
    BodyMaterialPreset body = BodyMaterialPreset::Traditional;
    BowStickPreset bowStick = BowStickPreset::PernambucoLike;
    ContactMaterialPreset contact = ContactMaterialPreset::HorsehairMediumRosin;
};

struct DebugState
{
    std::array<float, 4> contactNormalForceN{};
    std::array<float, 4> speakingFrequencyHz{};
    std::array<bool, 4> sticking{};
    float bowSpeedMps = 0.0f;
    float bridgeVelocity = 0.0f;
    float bowAngleDeg = 0.0f;
    float vibratoOffsetCents = 0.0f;
    int bowDirection = 1;
    int bowPairLowerString = 1;
    int primaryString = 1;
};

class FiddleEngine
{
public:
    FiddleEngine();
    ~FiddleEngine();

    FiddleEngine(const FiddleEngine&) = delete;
    FiddleEngine& operator=(const FiddleEngine&) = delete;

    void prepare(double sampleRate);
    void reset();

    void beginBowStroke(bool alternateDirection) noexcept;
    void noteOn(float frequencyHz, float velocity);
    void retune(float frequencyHz);
    void noteOff();

    void setControls(const Controls& controls) noexcept;
    void setMaterials(const MaterialSettings& materials) noexcept;

    // Adds output into left/right. No allocation, locking or I/O occurs here.
    void process(float* left, float* right, std::size_t numSamples) noexcept;

    [[nodiscard]] DebugState debugSnapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fiddle
