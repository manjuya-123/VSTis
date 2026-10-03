#pragma once

#include <array>
#include <cstdint>

namespace fiddle
{
struct NoteSelection
{
    bool active = false;
    bool changed = false;
    int note = -1;
    float velocity = 0.0f;
};

class MidiNoteStack
{
public:
    void reset() noexcept
    {
        held_.fill(false);
        velocity_.fill(0.0f);
        order_.fill(0);
        serial_ = 0;
        currentNote_ = -1;
    }

    NoteSelection noteOn(int note, float velocity) noexcept
    {
        if (note < 0 || note >= 128)
            return current(false);

        const auto index = static_cast<std::size_t>(note);
        held_[index] = true;
        velocity_[index] = velocity;
        order_[index] = ++serial_;
        const bool changed = currentNote_ != note;
        currentNote_ = note;
        return current(changed);
    }

    NoteSelection noteOff(int note) noexcept
    {
        if (note < 0 || note >= 128)
            return current(false);

        const auto index = static_cast<std::size_t>(note);
        held_[index] = false;

        // Releasing a background note must not interrupt the currently sounding note.
        if (currentNote_ != note)
            return current(false);

        int newest = -1;
        std::uint64_t newestOrder = 0;
        for (int candidate = 0; candidate < 128; ++candidate)
        {
            const auto ci = static_cast<std::size_t>(candidate);
            if (held_[ci] && order_[ci] >= newestOrder)
            {
                newest = candidate;
                newestOrder = order_[ci];
            }
        }

        currentNote_ = newest;
        return current(true);
    }

    [[nodiscard]] NoteSelection current(bool changed = false) const noexcept
    {
        NoteSelection result;
        result.changed = changed;
        result.note = currentNote_;
        result.active = currentNote_ >= 0;
        if (result.active)
            result.velocity = velocity_[static_cast<std::size_t>(currentNote_)];
        return result;
    }

private:
    std::array<bool, 128> held_{};
    std::array<float, 128> velocity_{};
    std::array<std::uint64_t, 128> order_{};
    std::uint64_t serial_ = 0;
    int currentNote_ = -1;
};
} // namespace fiddle
