# Enclosure / hinge notes — Tab5 clamshell (deferred)

Working notes for the 3D-printed **clamshell hinge** (Nintendo-DS style) that
closes the two modules together. Not started — the printer arrives ~summer 2026,
and it depends on the still-open "where does CSI capture happen" decision (what
actually needs to be housed). This file just preserves everything needed to pick
it up cold later.

## Goal

Adapt an existing MakerWorld model into a clamshell that holds the **Tab5** as
the top screen and the **CardputerZero** as the bottom (keyboard) half, joined by
a hinge. The inter-module electrical link must be **short, simple, solid** (runs
through/near the hinge).

- Starting reference model (user-chosen): "Cardputer-Adv XL external 2.8 display
  mod" — https://makerworld.com/fr/models/2795834-cardputer-adv-xl-external-2-8-display-mod#profileId-3109591
- ⚠️ **Licensing:** that model is a third-party MakerWorld design. Do NOT commit
  its STL into this repo or redistribute it without checking its MakerWorld
  license. We keep only measurements + a link here. The user's uploaded copy
  (`Body_all.stl`) lives outside the repo.

## Measured facts (reference STL `Body_all.stl`)

Parsed directly (binary STL, MakerWorld header `MW 1.0 2795834`):
- Triangles: 18,830
- Bounding box: **133.65 × 148.79 × 27.00 mm** (X × Y × Z)
- It's a *baked mesh* — no parametric features. Editing a specific cavity in it
  is fragile; treat it as reference geometry, not an editable model.

## Tab5 physical (verify against the official mechanical drawing before cutting)

- Chassis ≈ **128 × 80 × 12 mm**, corner radius ~R4 (from online specs — M5Stack
  usually publishes a STEP/DXF; get it for exact bezel, screen window, USB-C and
  button positions). Source: docs.m5stack.com/en/core/Tab5.
- 5" 1280×720 capacitive touchscreen; its own USB-C + buttons to keep accessible.

### Why this is a redesign, not a tweak

The reference body is *portrait-tall* (148 mm) built around a small **2.8"
external** display for a Cardputer-Adv. The Tab5 is a **5" ~128×80** unit with
its own touch, ports and depth (12 mm). Screen window, pocket depth, port cutouts
and mounting bosses all differ. We realistically reuse only the **hinge** (and
the general aesthetic); the display-holding shell is designed fresh for the Tab5.

## What I'll need to actually produce a part (checklist for later)

1. **Decision on the device lineup** (open): is there a dedicated ESP32 CSI-sensor
   node to house too, or is it CardputerZero + Tab5 only? Changes what the shell
   must contain. (See requirements.md §1 / the CSI-source question.)
2. **Hinge interface dimensions** from the reference — pin diameter, knuckle
   width + spacing + count, any screw/boss pattern. Either:
   - I extract them by programmatically slicing the hinge region out of the STL
     (needs a mesh lib installed — see Tooling), or
   - the user points to the hinge zone / provides the reference's source.
3. **CardputerZero mechanical** — external dims + port/keyboard face layout
   (14-pin GPIO header, Grove, 3× USB, USB-C, HDMI, IR window) so the bottom half
   and the hinge side line up. Get M5Stack's drawing when the unit ships.
4. **Chosen inter-module connector** (short/simple/solid): current lean is
   **USB-C ↔ USB** for bandwidth (needed if the Tab5 acts as a streamed 2nd
   screen), with **Grove UART (4-wire)** as the simplest low-rate radar/command
   fallback. Whichever is picked drives the cable channel through the hinge.
5. **Print constraints** — target printer Bambu P1S (256×256×256 build volume,
   fine), material (PETG/PLA?), wall/tolerance prefs.

## Tooling situation (this sandbox)

- **No** CAD apps (OpenSCAD/FreeCAD/Blender) and **no** mesh libs (trimesh,
  numpy-stl, numpy) are installed; Python3 is available. I parsed the STL with a
  hand-rolled binary reader.
- I have **no dedicated 3D-modeling skill**. What I can do:
  - measure/analyze STLs (done); isolate regions if a mesh lib is installed;
  - author **parametric OpenSCAD source** for a Tab5 bezel + hinge knuckle driven
    by variables, which the user renders locally (Bambu/OpenSCAD);
  - optionally attempt to install OpenSCAD/trimesh in the sandbox to emit an STL
    + PNG preview (subject to network).
- Honest expectation: I can deliver a **solid parametric starting point**, not a
  first-try perfect print — it will need a test print + iteration.

## Recommended first step when we resume

Get the **official STEP/DXF** for the Tab5 (and later the CardputerZero) and the
**hinge cotes** from the reference; then I write a parametric OpenSCAD design
(Tab5 faceplate + matching hinge) as the base to iterate from.
