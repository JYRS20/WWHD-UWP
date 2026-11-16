# Minimap HD UWP adaptation

Source: [Chironeyes/The.Wind.Waker.HD-Minimap.HD](https://github.com/Chironeyes/The.Wind.Waker.HD-Minimap.HD), version 1.3, commit `ba91da26470999767e26d3fecc9467ad21bb344d`.

Original code and interface artwork: Chironeyes, MIT. Original upstream assistance credit: GPT-6 Astra. See LICENSE, NOTICE.md and ASSET-PROVENANCE.md. Native Nintendo maps, markers, fonts, compass pixels and wind-animation resources are resolved from the user's Wii U game at runtime.

The translated C module contains only the upstream mod's PowerPC instructions, layout modifications, generated interface primitives and resource placeholders. It contains no RPX, original game functions, extracted map archive, key or save. The USA v0 assembly SHA-256 is `9798ef3fc50bacaacede591de4453c49ebf579a6e6c4006cfb20a6d948e88ab0`.

UWP integration translates the assembly to the existing guest ABI, replaces 11 function entries, guards four original call sites by their guest link register, and updates one native draw callback only after module validation. Displaced stack allocation executes exactly once in the original function. Emulated `mftb` instructions retain the runtime timebase. The original code/data ordering and resource alignment are preserved.

The module's TV clipping uses the runtime's logical 1280x720 guest canvas; the Vulkan backend applies resolution scaling. Using the native physical 1920x1080 query here would scale the scissor twice. This is isolated to the mod and does not change game resolution.

Both curated minimaps are selectable in Settings > Mods > Minimap. Changes apply after restarting the app. The HD adapter currently supports USA v0; the original GameCube minimap and its prepared data remain available. Native HD dungeon maps require the Map item and preserve Compass and discovery rules.

## UWP presentation revision 2

The user's dungeon zoom/rotation screenshot revealed a second physical/logical mismatch: the original dungeon scissor builder queries physical dimensions internally. The private dungeon draw now supplies an inward-rounded, logical-TV min/max rectangle from the current frame square, including its slide position. Empty offscreen rectangles skip only this private map draw.

Compact size defaults to 160 logical pixels (previously 195.7); expanded size is 1.5 times compact. A saved size slider ranges from 120 to 240. Sailing fit centers the complete island-picture quad with padding and applies the same affine transform to Link and ship marker siblings. Original native pane transforms are restored before the next animation tick and retained in guest-backed module state, preventing cumulative shrinking. The private map's release clears that saved transform owner.

The wind arrow reads the live world wind vector and converts it to the north-up map's counter-clockwise pane angle. No camera or boat-heading offset is added: changing boat heading alone does not change world wind.

UWP presentation extensions in presentation.c.in are MPL-2.0; original Minimap HD contributions retain their MIT license. This revision was compiled only, at the user's request; gameplay verification is delegated to the user.
