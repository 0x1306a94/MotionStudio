# Motion Studio Icon Composer source

All artwork uses a 1024 × 1024 canvas. Import the numbered directories into Icon Composer so each directory becomes a group.

Layer order from back to front:

1. `00_Background`
2. `10_Motion`
3. `20_MorphFrames`
4. `30_Keyframes`

`Preview.svg` is a flat source-art preview and must not be imported as an additional layer.

## Icon Composer settings

- Platforms: iOS and macOS; watchOS off
- Background: opaque; no Liquid Glass effect
- Motion group: Combined glass; medium translucency; low refraction; soft shadow
- Morph Frames group: Individual glass; medium translucency; medium refraction; automatic specular highlights
- Keyframes group: Combined glass; low translucency; low refraction; automatic specular highlights
- Default appearance: use source colors
- Dark appearance: reduce background luminance and lower magenta saturation
- Mono appearance: keep the same geometry and use neutral foreground annotations

Keep blur, shadows, specular highlights, and refraction out of the SVG source. Configure those effects in Icon Composer so the system can render them dynamically.
