# TODO — implementation sequence for the pupil-to-limbus ratio fix

This task list is ordered for the actual implementation path that is required by the live renderer. It is intentionally restricted to the smallest change that makes the current renderer obey the normalized pupil-to-limbus model without replacing the polar iris shader path.

## 1. Establish the normalized center and eye geometry

### 1.1 Define the fixed eye center
- Add the explicit runtime contract that the normalized eye center is always `(100, 100)` for the 200×200 plate.
- Preserve the current generated-art convention that re-centers the source image on the pupil midpoint.
- Why: the renderer is already centered on the iris disc; the ratio model must not introduce drift as the pupil changes size.
- Files: `src/eye.h`, `src/eye.cpp`, `create/normalize_eye.py`
- Proof: tests confirm center invariance at all dilation states.

### 1.2 Capture eye-specific limbus geometry
- Add a per-eye calibration record holding the eye-specific limbus diameter and the fixed center.
- Keep limbus geometry out of the LDR or sensor path.
- Why: `pupilDiameter = limbusDiameter × pupilRatio` must be scale-aware, but the input source is still only normalized dilation.
- Files: `src/eye.h`, `src/eye.cpp`, `src/main.cpp`
- Proof: cross-eye proportionality tests pass with different limbus diameters at the same dilation.

## 2. Separate normalized dilation from geometry

### 2.1 Keep normalized dilation as a separate signal
- Leave `PupilResponseModel` as the layer that converts light to normalized pupil target values in `[0.0, 1.0]`.
- Do not fold the LDR or sensor mapping into the eye geometry.
- Why: the geometry is calibrated for the eye, not for the sensor.
- Files: `src/PupilResponseModel.h`, `src/PupilResponseModel.cpp`, `src/LDRSerialInput.cpp`
- Proof: LDR input still feeds normalization, while per-eye calibration remains independent.

### 2.2 Introduce the ratio calibration contract
- Add `Rmin` and `Rmax` to the eye calibration layer as the minimum and maximum pupil-to-limbus ratios.
- Why: the normalized dilation must map to a ratio before any pixel scale is applied.
- Files: `src/eye.h`, `src/eye.cpp`
- Proof: `D = 0.0 => pupilRatio = Rmin`, `D = 1.0 => pupilRatio = Rmax`.

## 3. Implement the pupil-to-limbus interpolation

### 3.1 Add the exact interpolation formula
- Implement:

```text
D = clamp(dilation, 0.0, 1.0)
pupilRatio = Rmin + D × (Rmax - Rmin)
```

- Why: this preserves the normalized dilation model and makes the ratio independent of arbitrary pixel units.
- Files: `src/eye.h`, `src/eye.cpp`
- Proof: midpoint and clamp tests pass.

### 3.2 Convert ratio to pupil diameter and radius
- Compute:

```text
pupilDiameter = limbusDiameter × pupilRatio
pupilRadius = pupilDiameter / 2
```

- Why: the final pupil boundary must be an eye-specific geometric quantity, not a raw art value.
- Files: `src/eye.h`, `src/eye.cpp`
- Proof: proportionality tests across two eye sizes pass.

## 4. Rebind the existing polar renderer to the calibrated boundary

### 4.1 Replace the raw `irisScale` threshold logic with calibrated radius logic
- Preserve the existing polar iris + sclera fallback path, but derive the actual cutoff radius from the calibrated `pupilRadius`.
- Why: the renderer already knows how to reveal sclera when the iris stops; it simply needs the mathematically correct pupil boundary.
- Files: `src/eye.cpp`, `src/eye.h`
- Proof: render output remains stable while pupil geometry tracks the normalized ratio model.

### 4.2 Keep the current texture mapping and artifact pipeline intact
- Do not replace the generated iris strip or remove the polar lookup.
- Why: the asset generation is still valid; only the pupil boundary needs to be normalized.
- Files: `src/eye.cpp`, `create/EYE_ASSET_SPEC.md`
- Proof: image output still uses the existing artwork, only the pupil cutoff changes.

## 5. Add validation tests

### 5.1 Add minimum / midpoint / maximum dilation tests
- Validate all three canonical states.
- Why: the model is only valid if it behaves at the bounds and midpoint exactly.
- Files: `tests/` or the smallest unit-test target in the project
- Proof: explicit assertions for the three expected ratio values.

### 5.2 Add clamp and center tests
- Test `D < 0`, `D > 1`, and fixed center `(100,100)`.
- Why: the renderer must remain stable outside nominal input ranges and center invariant.
- Files: `tests/`
- Proof: clamp and center assertions pass.

### 5.3 Add cross-eye proportionality tests
- Use two calibration objects with different `limbusDiameter` values but identical `Rmin`, `Rmax`.
- Why: the ratio model must preserve proportionality across eyes, even when pixel diameter differs.
- Files: `tests/`
- Proof: `PupilA / L_A == PupilB / L_B` at the same dilation.

## 6. Visual validation and migration

### 6.1 Run visual smoke tests for the existing eye assets
- Validate minimum dilation, mid dilation, and maximum dilation on each shipped eye asset.
- Why: the ratio model needs to work across existing generated eyes without rewriting the art pipeline.
- Files: `src/eye.cpp`, `tests/`, `README.md`
- Proof: no asset breaks visually and the pupil remains centered.

### 6.2 Remove obsolete assumptions from the implementation notes
- Update notes and docs so they describe ratio-based pupil geometry instead of hidden raw threshold scaling.
- Why: the ambiguity is the actual bug; documentation must match the implementation path.
- Files: `BUILDPLAN.md`, `TODO.md`, `create/EYE_ASSET_SPEC.md`
- Proof: the build plan and source docs agree on the exact final model.

## 7. Final verification checklist

- `D = 0.0` yields `pupilRatio = Rmin`
- `D = 0.5` yields the midpoint ratio
- `D = 1.0` yields `pupilRatio = Rmax`
- clamp behavior remains correct
- eye center remains `(100,100)`
- pupil never leaves the calibrated boundary
- current renderer still uses the polar iris/sclera reveal path
- no render-time code is added that draws a separate black circle apart from the iris cutoff

## 8. Implementation order summary

1. Establish fixed center and limbus calibration.
2. Keep normalized dilation separate from eye calibration.
3. Add `Rmin` / `Rmax` interpolation.
4. Compute `pupilDiameter` and `pupilRadius` from `limbusDiameter` and ratio.
5. Bind the calibrated radius back into the existing polar iris cutoff.
6. Validate clamp, midpoint, max, and center invariance.
7. Validate cross-eye proportionality.
8. Run visual smoke tests across existing eye assets.
9. Update plan and documentation to match the final implementation.

## 9. Files involved in the fix

- `src/eye.h`
- `src/eye.cpp`
- `src/PupilResponseModel.h`
- `src/PupilResponseModel.cpp`
- `src/LDRSerialInput.cpp`
- `src/main.cpp`
- `tests/`
- `BUILDPLAN.md`
- `TODO.md`

## 10. Files not involved in the geometry fix

- `create/normalize_eye.py` remains the source-art measurement tool
- `create/EYE_ASSET_SPEC.md` remains an asset contract document
- the generated polar tables remain the existing art representation
- the LDR sensor normalization stays a separate input layer
- the project does not add a new black pupil layer or replace the iris renderer architecture
