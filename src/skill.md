---
name: cellcount
description: |
  Count cells or colonies on microscope and plate images with cellcount. Use for
  any request to count, summarize, validate or export a .cellcount session, to
  find candidate cells or colonies in a PNG, JPEG or TIFF image, or to work out
  viability or a hemocytometer concentration from one.
---

# cellcount

A cellcount session is a `.cellcount` JSON file saved beside its image
(`plate.tif` → `plate.tif.cellcount`). It holds the classes (up to 9, each with a
name and marker shape), the markers (`x`, `y` in image pixels, 0-based `class`),
an optional region of interest, the scale in pixels per µm, and the
hemocytometer settings. Only markers inside the region are counted.

No command needs a display. Each takes `--json`, exits 0 on success and 1 on
failure, and writes errors to stderr.

## Commands

- `cellcount check <session> --json` validates the file and its image. Fix
  every `error`; `warning`s are worth reporting.
- `cellcount summary <session> --json` prints counts and percentages per class,
  the total, viability (Live / (Live + Dead)), the area in px² and µm² when a
  scale is set, and cells/mL when squares are set.
- `cellcount detect <image> --json [--threshold N] [--min-area px] [--max-area px] [--polarity auto|dark|light]`
  prints candidate points: round spots that stand out from their surroundings,
  each with an `area` in pixels and a `contrast` in gray levels.
  `--polarity dark` finds darker cells, `light` brighter ones; trypan-blue live
  cells are `light` and dead ones `dark`. Raise `--min-area` to the size of the
  cells to drop debris. These are only suggestions, so say so when reporting
  them, and never present them as a count.
- `cellcount export <session> --csv markers.csv --summary-csv summary.csv --png annotated.png --json`
  writes any of the three. The original image is never modified.

## Editing a session

You may edit the JSON directly: add markers as `{"x": 10.5, "y": 20, "class": 0}`,
rename classes, set `"pixelsPerUm"`, or set `"hemocytometer": {"dilution": 2,
"squares": 4, "squareVolumeMl": 0.0001}`. A region is
`{"shape": "rect" | "circle", "x", "y", "width", "height"}` in image pixels; a
circle's width and height are equal. `"imageSize": [width, height]` is optional;
without it the size is read from the image. Run `cellcount check` afterwards.

## Working with the person

Counting by eye is the person's call. `cellcount open <image|session>` opens the
window for them to review and correct markers; it needs their desktop, so do
not wait on it.
