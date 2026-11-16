# Asset provenance

Minimap HD preserves the game's visual language by combining new UI additions with the game's existing map resources. These categories are distinct; native maps and icons are not presented as newly created artwork.

## Project-created artwork

The following SVG sources were drawn for this project and rasterized into the graphic pack's generated texture data:

| Element | Source | Purpose |
| --- | --- | --- |
| Folded map | `assets/map-hide.svg` | Show/hide shortcut |
| Open map with magnifier and blue plus | `assets/map-zoom.svg` | Expand shortcut and marine availability hint |
| Open map with magnifier and red minus | `assets/map-reduce.svg` | Reduce shortcut |
| Faded open map | `assets/map-unknown.svg` | Undiscovered-sector hint aboard |
| Faded folded map | `assets/map-hide-undiscovered.svg` | Undiscovered-island hint on foot |
| Parchment frame | `assets/map-frame.svg` | Permanent map border with a transparent interior |
| Rounded corner masks | `assets/dungeon-corner-*.svg` | Background corner treatment |

These illustrations are new project artwork inspired by the game's interface style. They are not extracted island or dungeon textures. The black backdrop and plain white utility texture are generated UI primitives.

## Adapted visual design

| Element | Origin | Project changes |
| --- | --- | --- |
| Wind arrow outline | Native `MenuMap` wind-arrow silhouette | Redrawn as `assets/map-wind.svg`, with a transparent center, warm paper-colored contour, and adjusted motion amplitude |
| Undiscovered-area question mark | Native `CKingMain` font | Rendered through the game's text renderer with a red fill, custom outline, and HUD-dependent attenuation |

The wind outline is an adaptation of the game's design rather than an unrelated new symbol. The question mark uses the existing game font; it is not a newly drawn typeface or a packaged extracted glyph texture.

## Native resources reused at runtime

| Element | Source in the installed game | How it is used |
| --- | --- | --- |
| Island map artwork | Native local-map resources | Existing plan artwork rendered on the TV minimap |
| Dungeon floors and room artwork | Native dungeon-map renderer | Current floor and native map availability followed dynamically |
| Treasure and Triforce chart artwork | Native treasure-chart resources | Eligible owned charts displayed beside the minimap |
| Link, boat, chests, and other markers | Native map marker resources and logic | Native visibility retained; no player marker added to treasure charts |
| Dungeon compass icon | `MapItemIcon_02^l.bflim` | Native menu compass used for the dungeon zoom shortcut |
| Treasure-chart title ribbon and shadow | `TitleBase_00^t.bflim`, `TitleBaseShadow_00^t.bflim`, native title layout | Native ribbon and localized chart title reused |
| Fonts and localized text | Native font and message resources | Existing game language retained |
| Wind displacement and animation tracks | `WindArrowID_00+r.bflim`, `MenuMap_00_WindowNowLoop.bflan` | Native wave resources reused with adjusted scale and motion |
| HUD layouts and materials | Native `Main_00`, `Shortcut_00`, `TreMap_00`, and `MenuMap_00` resources | Private map layouts reconstructed from validated resources and extended with the project additions |

The distributable patch contains project additions, layout modification instructions, resource signatures, and placeholders for resources resolved from the installed game. Original Nintendo texture pixels, font atlases, localized message archives, and wind-animation keys are not bundled as standalone assets. The compass and wind displacement data are populated from native resources at runtime.

## Credits and licensing scope

Project development, visual direction, and testing: **Chironeyes**. Coding and artwork assistance: **GPT-6 Astra**. Original game artwork, fonts, characters, interface designs, and map systems: **Nintendo** and their respective rights holders.

The MIT license applies to original project contributions. It does not grant ownership of or relicense Nintendo's original resources, game code, characters, or visual designs. Attribution to the original game is retained independently of the project's license.

The SVG files document the source artwork used by the project. Changing them does not automatically rebuild the texture data embedded in the regional assembly files; `tools/package.py` only packages the existing installable pack.
