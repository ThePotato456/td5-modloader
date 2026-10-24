# Documentation visuals

[Documentation home](../README.md) · [Update commands](../building.md#update-documentation-visuals)

## Choose the format for the information

| Format | Use | Examples |
| --- | --- | --- |
| Real PNG screenshot | Show the manager's actual layout and controls | [Manager workflow](../manager-revamp.md) |
| PNG artwork | Inspect the sample's real base and upgraded appearances | [Custom tower appearances](../../samples/custom-tower-mod/README.md#actual-artwork) |
| Animated GIF | Preview a short sequence from the sample's real firing frames | [Sample catalog](../../samples/README.md#visual-examples) |
| Mermaid in Markdown | Explain event timing, decisions, and lifecycle | [Cash updates](../../samples/round-budget-mod/README.md#synchronous-cash-update), [life-loss decision](../../samples/lives-guardian-mod/README.md#life-loss-decision) |
| Markdown table | Compare settings, defaults, costs, and outcomes | [Profile settings](../manager-revamp.md#details), [tower upgrades](../../samples/custom-tower-mod/README.md#plasma-monkey-upgrades) |
| Fenced text | Keep expected logs selectable and searchable | [Hello World](../../samples/hello-world-mod/README.md#expected-message-text) |

## Screenshot provenance

[`manager-preview.png`](manager-preview.png) is the existing captured manager
window. Displayed package versions and profile state belong to that session;
it is not evidence of a new release or live acceptance run.

## Custom tower artwork

The three PNGs and firing GIF under the Custom Towers sample's `assets/previews/`
come from its original `assets/textures/plasma_monkey.png` atlas. The previews
use the atlas coordinates and nearest-neighbor enlargement to twice native
size. The GIF loops the three source firing frames over 0.2 seconds; in game,
firing triggers the animation rather than a continuous preview loop.

[render-doc-assets.py](../../scripts/render-doc-assets.py) regenerates these
files with Python and Pillow. `--check` compares them without writing files.
They are documentation assets, not game-rendered captures or acceptance results.

## Maintain the diagrams and images

1. Edit Mermaid diagrams and tables directly in Markdown, alongside their prose.
2. Keep a short text explanation with every diagram for readers whose renderer
   does not support Mermaid. Use basic flow/sequence syntax and no custom theme.
3. Keep expected logs as text; label example values rather than simulating an app window.
4. After an atlas change, regenerate the PNG/GIF previews and review transparency,
   frame order, timing, and readability on light and dark backgrounds.
5. Run the documentation checks and restage edited sample packages. Release checks
   compare package and bundled source bytes to the source example.
6. For new real screenshots, capture the relevant app/game window, review it for
   personal paths/account details, and label its session/build accurately.
