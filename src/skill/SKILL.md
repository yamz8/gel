---
name: gel
description: |
  Quantify and annotate agarose gel and Western blot images with Gel: detect
  lanes and bands, size bands against a ladder, measure background-subtracted
  band volumes, normalize to a loading control, and export a publication
  figure or a CSV. Use for ANY request to analyze, quantify, size, or make a
  figure from a gel, blot, or .gel session.
---

# Gel

A Gel session is a `.gel` JSON file saved next to its image (`blot.tif` →
`blot.gel`). It holds the lanes, bands, ladder, quantification settings, view
settings, and annotations. Measurements are never stored; every command
recomputes them from the image, which Gel never modifies.

## Commands (no display needed, all take `--json`, exit 1 on failure)

- `gel lanes <image> [--save]`: detect lanes. `--save` writes the session with
  lanes and detected bands.
- `gel quant <session|image> [--save]`: one row per band with size (from the
  ladder), raw volume, normalized volume, and saturated pixel count. An image
  with no session gets lanes and bands detected.
- `gel export <session> --png|--svg|--pdf|--csv <out> [--dpi N] [--width-mm N]`:
  the figure (cropped and adjusted gel, lane labels, ladder sizes, annotations;
  display settings in the metadata) or the results as CSV. Default 300 dpi,
  85 mm (one column).
- `gel check <session>`: every problem with a session. Errors (exit 1): missing
  image, lanes or bands outside the image, bad ladder. Warnings: changed
  image, saturated bands, ladder count mismatch.
- `gel open [file]`: the window, for a person.

## Workflow

1. `gel lanes blot.tif --save --json`, then read `blot.gel`.
2. Edit `blot.gel` directly with your file tools:
   - Lane labels: `lanes[i].label`.
   - Ladder: set `ladder.lane` (zero-based lane index), `ladder.preset`
     (`1 kb`, `100 bp`, `1 kb Plus`, `Protein kDa`), `ladder.sizes` (largest
     first) and `ladder.unit` (`bp` or `kDa`).
   - Normalization: `quantification.normalization` is `none`, `lane` (divide by
     the reference lane's total), or `band` (divide by one band, such as a
     loading control); set `referenceLane` and `referenceBand` (zero-based).
   - Background: `quantification.background` is `rolling-ball` (with
     `ballRadius` in pixels, larger than the widest band), `lane-edges`, or
     `none`.
   - Annotations: `{"kind": "arrow", "x", "y", "toX", "toY"}` or
     `{"kind": "text", "x", "y", "text"}` in image pixels.
   - Bands: `{"position", "lower", "upper"}` in image rows; mark hand-placed
     ones `"manual": true` so detection keeps them.
3. `gel check blot.gel --json`, fix every error.
4. `gel quant blot.gel --json` for the numbers; `gel export blot.gel --pdf fig.pdf`
   and `--csv results.csv` for the files.

## Reading the numbers

Volume is the sum of background-subtracted pixel values in the band, so it is
only comparable within one image. Bands with saturated pixels are
underestimated; say so and suggest a shorter exposure. Sizes marked
`extrapolated` lie outside the ladder and are unreliable.
