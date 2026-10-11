# Regenerating eye assets

**Generated headers are build artifacts. Never hand-edit them.**

Every file in `src/generated/` is produced by the two-stage pipeline below from
a source photo. Each is ~17k–20k lines / 1.2–1.4 MB of hex tables. A hand edit
is lost the next time the asset is regenerated, and a merge conflict in one of
these files is not a conflict anyone can resolve by reading it.

The source of truth is:

| Layer | Lives in | Hand-edited? |
| --- | --- | --- |
| Source photo | `create/<name>.png` | no |
| Normalised parts | `Uncanny_Eyes/convert/<name>/*.png` | no |
| Hex header | `src/generated/<name>Eye.h` | **never** |
| Registry entry | `src/artwork_<name>.cpp` | yes — names, dims, iris range, pupil shape |

`src/artwork_<name>.cpp` is the only hand-maintained part. It is a thin adapter
that includes the generated header and supplies the metadata the header cannot
know: display name, iris animation clamp, and pupil geometry.

## The pipeline

Stage 1 — `create/normalize_eye.py` measures the source photo and writes the
normalised parts:

```sh
cd create
python3 normalize_eye.py <name>.png --name <name> --framing <limbus|eyeball>
```

Add `--dry-run` first to see the measurements without writing anything. That is
always worth doing: the derived `IRIS_SIZE` depends on the photo, and a wrong
value shows up as the iris texture bleeding into the sclera as the eye dilates.

Stage 2 — `Uncanny_Eyes/convert/tablegen.py` packs the parts into a header:

```sh
python3 Uncanny_Eyes/convert/tablegen.py \
    Uncanny_Eyes/convert/<name>/sclera.png \
    Uncanny_Eyes/convert/<name>/iris.png \
    Uncanny_Eyes/convert/<name>/lid-upper-symmetrical.png \
    Uncanny_Eyes/convert/<name>/lid-lower-symmetrical.png \
    Uncanny_Eyes/convert/<name>/lid-upper.png \
    Uncanny_Eyes/convert/<name>/lid-lower.png \
    <IRIS_SIZE> > src/generated/<name>Eye.h
```

`<IRIS_SIZE>` is printed by stage 1 and must be copied from its output. A slit
pupil additionally appends the pupil map argument.

## Framing: the choice that matters

- `--framing limbus` (default) scales the eyeball so the limbus lands at
  `--limbus-radius` (40 px), synthesising a sclera margin. This reproduces the
  upstream `defaultEye` proportion and yields `IRIS_SIZE 80`.
- `--framing eyeball` lets the eyeball itself fill the 200 px plate, so
  `IRIS_SIZE` is derived from the real measured limbus and comes out much
  larger.

The two are not interchangeable, and the difference is visible: eyeball framing
gives a proportionally correct iris, limbus framing is only right for photos
whose limbus sits where the default variant's does.

## Determinism (TODO X1)

The pipeline is deterministic. Regenerating `fire` from `create/fire.png`
reproduces both the normalised parts and `src/generated/fireEye.h`
**byte for byte**, which is what keeps these very large files from churning on
every rebuild.

Two things preserve that:

1. **Always pass `--framing` explicitly.** The default is `limbus`; relying on
   it silently changes `IRIS_SIZE` for any asset that was built with
   `eyeball`.
2. **Prefer `--pupil-centre X,Y` over automatic pupil detection.** Automatic
   detection infers the pupil centre from the darkest blob, which is stable for
   these photos but is an inference. A black pupil is full of scattered
   exactly-zero texels, so a baked `#000000` seed dot is deliberately *not*
   auto-detected: any flood fill from the dot merges with those specks and
   stops being compact or centred. Bake the dot if you want one, and pass its
   fixed position via `--pupil-centre`.

## Verified recipes for the shipped variants

| Variant | Source | Framing | IRIS_SIZE | Header |
| --- | --- | --- | --- | --- |
| `coldBlue` | `create/cold_blue.png` | limbus | 80 | `src/generated/coldBlueEye.h` |
| `chalkBlue` | `create/chalk_blue.png` | eyeball | 118 | `src/generated/chalkBlueEye.h` |
| `fire` | `create/fire.png` | eyeball | 108 | `src/generated/fireEye.h` |
| `default` | upstream | — | 80 | `Uncanny_Eyes/uncannyEyes/graphics/defaultEye.h` |
| `cat` | upstream | — | — | `Uncanny_Eyes/uncannyEyes/graphics/catEye.h` |
| `dragon` | upstream | — | — | `Uncanny_Eyes/uncannyEyes/graphics/dragonEye.h` |
| `customDragon`, `customDragonA`, `customDragonB` | `create/raw/` | eyeball | — | `src/generated/customDragonEye*.h` |

`default`, `cat` and `dragon` are unmodified upstream Adafruit artwork and are
**not** produced by this pipeline; do not regenerate them.

The `customDragon*` family is known to have historical artwork-conversion
defects (see `STATE.md`). It is kept for compatibility, not as a reference for
how a variant should look.

`fire` carries a reptilian slit pupil that is **not** stored as a
`pupilMap.png`. A pupil map makes the polar distance a function of angle alone,
which collapses the iris strip to one row per angle and renders as radial spokes.
Shaped pupils are instead applied by the renderer through
`EyeArtwork::pupilShape` / `slitRatio`, which keeps the iris strip intact. See
`EYE_ASSET_SPEC.md` §3.1.

## Verifying a regeneration

Regenerate, then confirm the header did not change unexpectedly:

```sh
git diff --stat src/generated/
```

An unexpected diff means the recipe drifted — check `--framing` first, then the
`IRIS_SIZE` copied from stage 1.

Then run the validation harness, which covers every registered design across
every mode:

```sh
cmake --build build-ldr --parallel
ctest --test-dir build-ldr
./build-ldr/uncanny-eyes --design <name> --smoke-test
```
