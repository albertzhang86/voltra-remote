# Mode artwork

Editable SVG reconstructions of the symbols shown in Beyond Power's official
[VOLTRA page](https://www.beyond-power.com/pages/voltra), checked 2026-10-03.
These are traced/reconstructed shapes, not manufacturer-supplied vector files.

- Weight Training: five separate dumbbell pieces, rounded plates.
- Resistance Band: asymmetric loop, large left lobe and small lower right lobe.
- Damper: sideways parachute, scalloped canopy, four cords and tow point.
- Isokinetic: three filled triangular rays pointing left.

References:

- [Weight](https://www.beyond-power.com/cdn/shop/files/training_mode_01_eac83c7f-e19d-4349-84c3-a361cf6c79e6.png?v=1698994768&width=3840)
- [Band](https://www.beyond-power.com/cdn/shop/files/training_mode_02_a4f27a13-453e-4e9b-b7a8-bb44e4364e27.png?v=1698994766&width=3960)
- [Damper](https://www.beyond-power.com/cdn/shop/files/training_mode_03_7c6c2dad-2657-4267-81a9-d9c8a018114d.png?v=1698994768&width=3960)
- [Isokinetic](https://www.beyond-power.com/cdn/shop/files/Isokinetic_banner.jpg?v=1731066569&width=3500)

`node tools/build_mode_assets.cjs` (requires sharp) compiles the SVGs into
36×36 antialiased alpha masks in `firmware/VoltraKnob/src/ui/mode_assets.c`.
The generated file is checked in; ordinary firmware builds need no Node packages.
All mode icons in the picker, center and control tabs share the same masks.
Color is applied at runtime, including dimming while unloaded.
The reference photographs are excluded from the shareable release.
Symbol designs belong to Beyond Power; this is an independent remote project.
