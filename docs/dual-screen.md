# Dual screen and touch design

The rules:
1. **The top screen is the camera**: the game's 3D/BG view, at ×0.8 scale, without HUD clutter.
2. **The bottom screen is the hands**: commands, menus, choices, maps. Everything there can be tapped, and
   everything there can also be done with buttons. Touch is never required.
3. When game code draws UI, it is redirected (rendered to the bottom screen in 2D) rather than rewritten. Game
   logic and cursor state stay in the decomp code. Touch writes into the same cursor state, or injects the same
   pad bits.

Phase plan: in **M2–M4** everything the PS1 drew renders on the top screen, scaled (playable, ugly text), and the
bottom screen is a debug console plus generic touch buttons. **M5** moves each UI below, one mode at a time, in
the order of the table in §3.

## 1. Buttons

| DS | PS1 equivalent | notes |
|---|---|---|
| A | ✕ (confirm) | DS convention: confirm is on A |
| B | ○ (cancel; hold to walk in fields) | |
| X | △ (menu) | |
| Y | □ (cards / Moogle / dig) | |
| L / R | L1 / R1 | |
| L+R held | L2+R2 (flee in battle) | the same chord FF4 DS uses |
| Select | Select (ATE, help) | |
| Start | Start (pause) | |
| touch buttons | L2, R2, and everything above | for any L2/R2 use other than fleeing (R12) |

Remapping happens in the PS1 pad report, so the game's own button-configuration menu still works on top of it.

## 2. Touch primitives
- **Virtual stick**: dragging on any empty area of the bottom screen moves the character relative to the
  stylus-down point, as an 8-way d-pad (FF9 is digital). Lifting stops.
- **Tap a list row**: moves the cursor there and confirms in one tap. In lists where a first tap shows help (equip,
  abilities), the first tap moves and the second confirms.
- **Tap a tab or arrow**: L1/R1 or a page flip.
- **Long-press**: shows the help text, like Select.
- The corners of the bottom screen hold a fixed strip for Menu (△), Back (○) and context buttons. On a 3DS,
  both screens' backlights stay on.

## 3. Per mode

| mode (`g_gameState->mode`) | overlay | top screen | bottom screen |
|---|---|---|---|
| 1 field | `0a` | scene, dialogue windows in place (speech tails point at speakers) | location name; virtual stick; context buttons ("!" Talk/Check, "?" when a prompt appears, Cards when a challenger is near, ATE when available); **dialogue choices** as big tappable rows; party HP summary |
| 2 battle | `0b` | battle scene, **without** the HUD and command windows (the PS1's bottom third is freed) | ATB/HP/MP/Trance bars per member; command menu (Attack/Steal/Skill/Item/Defend/Change); targets as a tappable list of enemy and ally names with a cursor that mirrors the top-screen arrow; help line; a Flee button (shown as holding L+R) |
| 3 world map | `0c` | world view | the **minimap**, always on, tappable to read place names. Chocobo/airship controls (ascend/descend as touch buttons) |
| menus (`32`–`3e`, main menu) | | detail pane: the selected character's stats, item/ability description, equip comparison | the navigable list itself, with tabs as buttons |
| 9 Tetra Master | `0d` | opponent's hand, score, the large card detail of the selected or hovered card, battle numbers | the 4×4 board and your hand (tap a card, then tap a cell; drag also works) |
| 4 Blackjack | `15` | table | Hit / Stand / Double / Split / bet buttons |
| 0 opening, FMV | `14` | video | black, or chapter art (D3) |
| 7 / 8 (battle results? `65`) | `65` | spoils summary | EXP/AP/item list, "Next" |
| save/load | `39` | slot detail | slot list (15 per card ×2), tap to pick |
| name entry | `3a` | entered name | the keyboard: tap letters, which is better than the PS1 grid |
| Chocograph / Hot & Cold | `32` + field | field | the Chocograph list; a "Dig" button |

Mini-games driven by field scripts that need mashing or timing (jump rope, the Hippaul race, the Mognet
deliveries are fine) get on-screen buttons that map to the button the script is polling. That needs the script
opcodes that read the pad (R20).

## 4. Text and windows
- Window frames and text are drawn by the port's 2D renderer, with the game's own glyphs, onto main-engine BG1
  for top-screen windows (alpha-blended over 3D like the PS1 windows) or onto the sub engine for the bottom
  screen.
- **Top-screen dialogue** uses the ×0.8 anti-aliased font at the scaled window position. If legibility turns out
  poor in the M5 prototype, the fallback is dialogue on the bottom screen at 1:1 with the speaker's name, and a
  small marker over the speaker on top (D2).
- **The bottom screen** uses the 1:1 font (PS1 pixel size), so menu text is as crisp as the original.
- The bottom screen is 256 px wide, the PS1 320. The extractor measures every string's width in every text bank
  with the real font metrics and reports which ones overflow at 1:1. Menus that overflow get a hand-made layout;
  lists scroll.

## 5. What the decomp needs to expose for this
Each bottom-screen UI needs three things: **(a)** where the game draws that UI (to suppress it on top),
**(b)** the state variables (cursor index, list contents, current menu, selected target), and **(c)** where input
is read (to inject touch). Those are R15–R21 in `decomp-requests.md`.
