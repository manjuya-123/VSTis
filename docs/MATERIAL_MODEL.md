# FiddleModel material model

The material controls are intended to change the physical model, not to behave like an EQ preset.

## Body

The current body material profile changes three model quantities together:

- modal frequency scale — a compact proxy for effective stiffness-to-mass ratio
- modal damping scale — how quickly body resonances lose energy
- modal admittance scale — how strongly the bridge/body moves for a given force

Current profiles:

- **Traditional spruce/maple** — reference profile.
- **Light stiff composite** — slightly higher modal frequencies, lower damping, slightly stronger mobility.
- **Dense experimental** — slightly lower modal frequencies, higher damping, lower mobility.
- **Rigid composite** — higher modal frequencies and lower damping.

These are deliberately broad physical profiles. They are not measured reproductions of a specific violin, maker, wood sample or carbon layup.

## Bow stick

The present bow-stick material model affects effective acceleration/reversal response.

- **Pernambuco-like** — reference response.
- **Carbon-like** — slightly faster response.
- **Light rigid experimental** — deliberately fast, low-inertia response for exploring bowing that would be difficult to realise with an ordinary bow.
- **Flexible experimental** — slower response.

This is currently a handling/dynamic model; the stick itself is not yet a radiating acoustic resonator.

## Hair / Rosin

Hair and rosin act directly at the nonlinear string contact. Profiles change:

- static grip limit
- sliding friction magnitude
- contact-state relaxation rate

Current profiles:

- **Horsehair + medium rosin** — reference profile.
- **Dry / light grip** — lower static and sliding grip, quicker state relaxation.
- **High-grip rosin** — stronger grip and slower state relaxation.
- **Synthetic hair** — slightly reduced grip with a modestly faster state response.

The contact model is still an engineering approximation. The 2025 Woodhouse/Galluzzo rate-and-state model motivates the separation between sliding-rate behaviour and a slower contact state; a fuller temperature-state implementation is a later milestone.

## UI rule

The normal interface shows material names and player-facing consequences. Raw modal scalars, friction coefficients, state time constants and other solver parameters remain diagnostics/internal implementation details.

Experimental presets must be labelled as experimental instead of implying that they are calibrated measurements of a real commercial material.
