# Earthrise

`earthrise.jpg` is the Apollo 8 Earthrise photograph credited to **NASA / Bill Anders**.

- Source: https://science.nasa.gov/resource/apollo-8s-iconic-earthrise/
- Download: https://assets.science.nasa.gov/dynamicimage/assets/science/psd/solar/2023/09/a/as08-14-2383-1.jpg
- NASA media guidelines: https://www.nasa.gov/nasa-brand-center/images-and-media/

The downloaded image is stored unchanged. The application crops the Earth and lunar foreground at render time and blends them into the theme at low opacity. The composition is an artistic interpretation, not the photograph's original apparent Earth size. This image is credited separately from the project's MIT-licensed code. No endorsement by NASA is implied.

# Yutping Cyber Letters

`YutpingCyberLetters.ttf` is the user-approved Draft 05 lettering study, reconstructed in VectorCraft from the supplied Cyberpunk-style alphabet reference, with optical pair spacing and a local C-tip adjustment. It is not Cyberway Riders, the original stock artwork font, or an official Cyberpunk font.

- Reference artwork: https://www.shutterstock.com/image-vector/cyberpunk-2077-font-vector-cyberfont-perfect-1423096409
- Font family: Yutping Cyber Reference Study; version 0.005.
- Only composition letters use it. Annotation text, digits, and page labels retain their existing fonts.
- The reference artwork and this derivative font are excluded from the code's MIT license. No commercial or redistribution permission for the reference artwork or derivative font is asserted by this repository; the font's embedded notice is retained.

# Minimal theme marks

The Lucy header mark is `theme-icon-concepts/lucy-a-afterglow.vectorcraft` (exported as the matching `.svg`), traced from the user's ending-sequence hair silhouette and baked into `src/LucyMark.h`; the native window renders those Bezier paths and gradients directly, with no image resource. `minimal-theme-marks.vectorcraft` is the editable source for the David and combined fallback header marks; `minimal-theme-marks.svg` is the exported design sheet. The native candidate window renders the same simple geometric shapes and existing theme colors, with no additional runtime image dependency.

The combined theme now uses the user's revised Blender embrace model, baked into `src/EmbraceMark.h`. `david-lucy-ime-mark.svg` and `david-lucy-ime-mark.vectorcraft` contain the matching editable vector artwork, including its single, continuous seven-stop blue hair gradient and mint-green tips. The underlying referenced characters and derivative artwork are not covered by the code's MIT license.

# NPP Blue Marble (2012)

`blue-marble-2012.jpg` is stored unchanged from NASA SVS. Credit: **NASA/NOAA/GSFC/Suomi NPP/VIIRS/Norman Kuring**.

- Source: https://svs.gsfc.nasa.gov/30002/
- Download: https://svs.gsfc.nasa.gov/vis/a030000/a030000/a030002/npp-cropped_print.jpg

The David & Lucy Embrace theme applies a cool tint and 32% display opacity at render time, with the user's geometric embrace at 22%. The Large Earth variant uses the same globe without the embrace figure. The globe is enlarged and cropped as an artistic composition, and extends through the composition header. The lunar foreground retains the separately credited Earthrise image. The original David & Lucy theme remains available with its earlier Earthrise composition. No endorsement by NASA or the other credited organizations is implied.

# Lucy hair restoration candidates

`lucy-original.jpg` is the unchanged original hair reference supplied by the user (original reference: https://imgur.com/VRjMle6). It is embedded for the Lucy · Afterglow background; scaling, a screen blend at 85% (black stays clear) and a left-to-right dark scrim are applied only at render time.

`lucy-hair-options/` holds separate image-generation restorations, with their prompts in `prompts.json` and revision prompt files. Only the selected third option, `03-flowing.png`, is embedded for the Lucy · Flow background, screen-blended at 70% behind the same scrim. The other restoration candidates are not embedded.

The source anime imagery and derivative restorations are not presented as original character designs or covered by the code's MIT license. No ownership or redistribution rights in the underlying character artwork are asserted.

# Capri · Redline lightning

`capri-redline.svg` reconstructs the narrow red lightning motif from the poster supplied by the user for the Capri / Project Capricorn theme. It was traced in VectorCraft; `capri-redline.vectorcraft` is its editable core-and-glow source, and `capri-redline-glow.svg` is the matching glow export. No character portrait or complete poster is embedded in the application.

`capri-redline.png` and `capri-redline-glow.png` are transparent 4× exports embedded in the native renderer. The idle core is displayed at 35%; the glow export contains a 35% core and a 65% blurred red copy (VectorCraft blur radius 10), with no glare. Both states use the same fixed crop and placement. The underlying poster and derived motif are credited separately from, and excluded from, the code's MIT license; no ownership or redistribution rights in the underlying artwork are asserted.

David & Lucy · Earthrise now uses 105% display scale and 35% opacity; the source photograph remains unchanged. Embrace and Blue Marble retain their previous art placement and opacity.
