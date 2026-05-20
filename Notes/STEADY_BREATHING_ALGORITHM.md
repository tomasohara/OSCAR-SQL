# Steady Breathing Algorithm

Source: `oscar/SleepLib/calcs.cpp` — `FlowParser::calcSteadyBreathingWaveform()` and `FlowParser::flagSteadyBreathing()`

Inspired by Löwenstein PrismaLine "Deep Sleep" event detection.
Only compiled when `STEADY_BREATHING` is defined.

---

## Phase 1: Build the Steadiness Waveform (`calcSteadyBreathingWaveform`)

The goal is to produce a waveform (`CPAP_SteadyBreathing`) that represents how much
breathing amplitude is fluctuating over time.

### Step 1 — Chunk the flow waveform into 20-second windows

The raw flow rate signal is divided into non-overlapping 20-second segments.

### Step 2 — RMS per chunk

For each 20-second chunk, the RMS (root mean square) of all flow samples is calculated,
giving a single number representing the *amplitude* of breathing in that window.

### Step 3 — Percentage change between consecutive chunks

The relative change between one chunk's RMS and the previous chunk's RMS:

```
difference = 100 × (current_RMS − previous_RMS) / previous_RMS
```

This measures how much breathing amplitude *changed* from one 20-second period to the next.

### Step 4 — RMS over a 2-minute rolling buffer

The percentage-difference values are fed into a 120-sample (120 × 20 s = 2-minute) circular
buffer. The RMS of that buffer is taken, averaging out variability over a 2-minute window.

### Step 5 — Exponential low-pass filter

A leaky integrator further smooths the result:

```
filter = filter + (rms_2min − filter) × (1/120)
```

The output is stored as the `CPAP_SteadyBreathing` waveform (one sample per 20-second chunk).

---

## Phase 2: Flag Steady Regions (`flagSteadyBreathing`)

### Threshold = 10%

Any region where the steadiness waveform value is ≤ 10 (breathing amplitude changing by no
more than 10% per 20-second window, averaged over 2 minutes) is considered steady.

### Minimum duration = 60 seconds

Short blips below threshold are ignored. Only contiguous regions lasting at least 60 seconds
are recorded as `CPAP_SteadyBreathingFlag` events.

---

## Design Intent

Deep, regular sleep produces a highly consistent breathing pattern with low cycle-to-cycle
amplitude variation — a low value on this waveform. The two-stage smoothing (2-minute RMS
window + low-pass filter) avoids false positives from brief breath-holding, sighs, or
position changes.

In testing builds (`STEADY_BREATHING_ENHANCED_TESTING`), the chunk duration and threshold
are configurable via `AppSetting->steadyBreathingDuration()` and
`AppSetting->steadyBreathingThreshold()`.
