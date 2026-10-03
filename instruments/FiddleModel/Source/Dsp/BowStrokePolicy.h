#pragma once

namespace fiddle
{
enum class BowStrokeMode
{
    FiddleAuto = 0,
    Connected,
    Alternate
};

inline bool shouldAlternateBow(BowStrokeMode mode, bool hadHeldNote) noexcept
{
    switch (mode)
    {
        case BowStrokeMode::FiddleAuto:
            return !hadHeldNote;
        case BowStrokeMode::Connected:
            return false;
        case BowStrokeMode::Alternate:
            return true;
    }
    return false;
}
} // namespace fiddle
