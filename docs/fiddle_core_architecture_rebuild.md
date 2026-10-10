# Fiddle Core Architecture Rebuild — Decision and Acceptance Criteria

Status: **DSP core fails musical acceptance; redesign required** (2026-10-11)
Baseline: `12676f16b08460b60677a197ea716c263d9501ec` (#809).
Scope: sound-producing physical engine. Preserve JUCE VST3/Standalone plumbing, Play Mode MIDI semantics, and original reel as integration fixtures, but **do not use a successful build/test result as evidence of acceptable timbre**.

## Evidence and architectural defects in the current engine

1. **No moving finger/string contact junction.** `FiddleEngine.cpp::setFingeringLayout()` maps note frequency to `speakingFrequency[].target`. The audio loop shortens both `bridgeDelay[]` and `nutDelay[]` by changing `oneWay`, while the old wave histories remain in the same delay rails. A finger is not solved as a spatially moving contact/termination with force, compliance, displacement and energy transfer. This is especially problematic under sustained bowing and slurs; lowering only the tuning-smoother time constant cannot repair the missing interaction.
2. **Bow contact/geometry depends on instantaneous MIDI string routing.** `makeBowForces`, `pairLower`, `primaryString`, and `singleStringIsolation` rearrange contact forces. #809 adds a special 75 ms A/E crossing hold. This is a local workaround, not a unified physical bow-position/angle/indentation model. Right-hand motion must be continuous and independent from the left-hand note selection.
3. **Nonlinear friction numerical regime is incompletely constrained.** `detail/BowContact.h` performs one-sample root finding for a reduced friction curve. If the root is not bracketed, it substitutes the static limit (`usedStaticFallback`), producing potentially unphysical injected energy/edge content. Earlier D-string probes found roughly three release events per bowed period for F#4 and A4 versus ~one for neighbouring notes. Fix the friction/junction method and check an energy balance, not only output spectra.
4. **Bridge and body radiativity are approximate, unvalidated for this instrument.** The current two-coordinate shared bridge and modal/acoustic radiation banks use representative modes and shaped profiles. They have not been fitted and assessed against aligned real violin/fiddle bow gestures. Their contribution to the pluck-like high-string timbre has not been ruled out.
5. **Acceptance tests are too weak.** Existing 14 portable regressions, Windows build, and processor MIDI tests establish software robustness and selected acoustic metrics. They do not establish realistic E-string crossing, same-bow slurs, consistent timbre, pitch-specific string identity, or the absence of audible "pon"/pluck-like notes. #806 and #809 passed while a listener still reports the same defect.

**Conclusion:** the current plugin is a functioning approximation, but it is NOT an acceptable bowed-fiddle physical instrument. Stop local parameter/EQ/noise fixes to this engine until a new coherent model is validated.

## New core architecture (separate engine, no silent replacement)

### 1. String mechanics, explicit finger contact

Build a prototype with an energy-aware time-domain numerical string (candidate: two-polarisation finite differences, with stiffness and frequency-dependent damping) and a moving compliant fingertip/fingerboard contact at physical string coordinate x. Keep string displacement and velocity states continuous through note changes. Pitch is **a consequence of finger contact location** rather than a direct write to a delay length. Prototype must first demonstrate stable fundamental, controlled high partials and legato without pitch-bend oscillator artifacts.

A waveguide with a moving, impedance-controlled finger scattering junction is an alternative **only if it demonstrably preserves wave history and passivity at moving contacts**. Benchmark both before committing to the final real-time solver; a simple variable-delay retune is not sufficient.

### 2. Physical bow and friction junction

Represent bow velocity, position measured from bridge, angle/height, normal force, bow-hair compliance and rosin contact history as continuous independent states. Use a well-posed non-linear stick/slip friction contact with event-time handling and stability/energy checks. Build a reference friction regime map over speed, pressure and bow position; in the normal playing area seek stable Helmholtz-like bowing (typically one release per period), without enforcing it on intentionally scratchy/special gestures. Numerical solver fallback must be counted and cause an explicit test failure if it enters a normal stable-playing region.

### 3. Crossing and coupling

Use ONE geometrical bow across all four strings. A string's contact force is derived from global bow angle/hair indentation and bridge curvature, **never teleported by changing MIDI `primaryString`**. Crossing A to E should redistribute traction continuously, with physical overlap permitted; a single string need not be muted at the bridge. Couple strings through a physically passive shared bridge and body; bow and finger interactions update the same evolving string states.

### 4. Acoustic body/radiation

First validate string and bridge velocity/force in isolation; then add calibrated body modes and directivity. Compare recordings under matching note, bow speed/pressure, articulation, and playing position. The radiated timbre must not be rescued with note-specific EQ, hidden samples, a secondary synth tone, or artificial note-on plucks.

### 5. MIDI / Play Mode remains a gesture interpreter

Keep the existing note and bow-action keys as **commands for physical gestures**, not direct sound resets. An active down/up-bow must persist through finger changes, slurs and string crossings; finger release and landing should have independently modeled timing and contact force. Bow change remains a separate gesture. Drone/double-stop produces two strings driven by the one physical bow.

## Required acceptance tests before new VST3 claims

- **One-string sustained bow:** G, D, A, E open and stopped; clean pitch, stable Helmholtz-like period in intended bow regime, no forced amplitude envelope or synthetic layer. Sweep pressure, speed and contact position; detect numerical fallback and energy anomalies.
- **Moving finger while bow stays on:** multiple ascending/descending changes, same physical string and unchanged bow state. No unwanted extra bow onset, detached pulse, spurious glissando, lost low harmonics, or energy discontinuity.
- **String crossing:** repeated A→E→A and D→A→D, with continuous right-hand geometry; measure contact-force derivative, bridge-force spikes, residual old-string energy, and actual audible attacks.
- **Sympathetic unison:** stopped A-string E5 versus open E; bridge coupling permitted, uncontrolled secondary bell/pluck not permitted. E-string intentionally bowed and A/E double stop must still work.
- **Reel integration:** render the exact original 32-bar musical MIDI through the final VST3 processor, not only a stand-alone numerical core. Compare old/new WAV and matched real fiddle reference with blinded listening in addition to spectral/temporal diagnostics.
- **Sample rates and runtime:** verify 44.1k/48k and common buffer sizes, bounded CPU and no allocation/locking in audio thread, no crashes/NaNs/unstable energy; preserve pitch through legato.
- **Release condition:** *do not call the new core a tonal improvement until a listener can hear sustained-bow slurs and repeated E-string crossings without the recurring "pon" sound*. Passing CI alone is insufficient.

## Execution boundaries

1. Freeze `#809` as a legacy control. No more changes to its normal timbre path in search of a numerical green score.
2. Prototype new 1-string bow/finger engine and emit isolated WAV plus physical-energy/time plots; compare to a recorded reference.
3. Validate 2-string shared bridge and repeated adjacent-string crossings.
4. Extend to G/D/A/E and cello-like drones/double stops as applicable; connect Play Mode.
5. Integrate JUCE and full reel, audition and benchmark against #809. Replace the old engine only after musical acceptance.

## Literature

- C. Desvages & S. Bilbao (2016), *Two-Polarisation Physical Model of Bowed Strings with Nonlinear Contact and Friction Forces, and Application to Gesture-Based Sound Synthesis*, Applied Sciences 6(5), 135. https://doi.org/10.3390/app6050135
- C. Desvages (2018), *Physical modelling of the bowed string and applications to sound synthesis*, University of Edinburgh. https://era.ed.ac.uk/items/60cf4338-33b4-4943-8d25-43e574ff5a2e
- J. Woodhouse & P. Galluzzo (2025), *Enhanced Tribological Modelling of Violin Rosin*. https://doi.org/10.1007/s11249-025-02062-4

This is an architecture decision and test specification, **not** a claim that the new physical solver already exists or that the recurring E-string defect is fixed.

## Verified 2026-10-11 code-level junction inconsistency

This is more specific than the design concerns above. In the legacy
`FiddleEngine.cpp`, the bow's incoming velocity is:

```
v_in = incomingBridge + incomingNut;
v_proxy = v_in + finiteWidthBlend * (finiteWidthAverage - v_in);
v_solved = BowContact::solve(v_proxy, ...);
injection = v_solved - v_proxy;
outgoing waves = incoming waves + injection;
```

The waves therefore reconstruct the **actual** string velocity
`v_actual = v_in + injection = v_solved - (v_proxy - v_in)`, not
`v_solved`. The contact solver computes friction and slip from
`v_solved`, but the waves exchange power at `v_actual`. For a
nonzero virtual-width correction `delta = v_proxy - v_in`, the
unaccounted local power term has magnitude `F_contact * delta`
(with sign depending on the power convention). No second spatial
injection or elastic storage is present to explain this difference.

This does **not** prove that this single term is the audible E-string
"pon"; it does prove the local numerical contact/propagation closure
is not exactly the one solved by the friction model. Correcting the
sign of a coefficient or altering frequency-dependent EQ cannot
supply the missing contact consistency.

A separate discontinuity risk is visible in `BowContact.h`: when
the sliding equation does not bracket a root it substitutes the
static limit. When the static and sliding regimes switch, that
fallback can inject sharp transitions. The current legacy diagnostics
expose the fallback bit but normal-playing tests have not established
that it is always absent.

## Experimental code now available

`Source/Dsp/experimental/ContinuousStringCore.h` implements an
**isolated one-string experiment**, not a replacement or tonal upgrade:

- fixed spatial coordinates, with second-order finite-difference
  displacement and wave propagation (adaptive internal substeps to
  keep the Courant number below 0.9);
- moving, implicit compliant finger/string contact, with the existing
  displacement field carried between note commands;
- force-coupled bow junction solved against that same displacement
  field, with a bounded monotone sliding root and no synthetic
  Note-On pluck;
- independent bridge reaction-force output for numerical probing;
- discrete free-string energy computation to test the underlying
  unforced update.

`Tests/FiddlePhysicalStringPrototype.cpp` covers passive-wave energy,
frequency, the unchanged state at a finger command, and finite
excited bow trajectories. Its CTest registration does **not** change
the sound engine used by the VST3 or original reel.

**Important limitations:** the prototype has one transverse
polarisation only, a prescribed bow rather than a moving hair/contact
mass, a rigid bridge boundary, an empirical static/dynamic friction
switch, and no violin body radiation. Passive energy of the free wave
is necessary, not sufficient: the next milestone is a coupled
energy/work ledger for bow and finger, then body admittance and
shared-string crossings. No subjective tonal improvement is claimed.
