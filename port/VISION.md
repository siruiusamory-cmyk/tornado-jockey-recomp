# Design vision

Long-range direction, not a work order: each item becomes a stage only through a frozen stage
contract. These are design goals, not claims about what the recovered executable supports.

## Ground rules

- Keep the fast arcade loop: start weak, feed on destruction, become a monster. Not a slow
  weather sim.
- A static recomp translates the original logic and adds nothing by itself. New graphics,
  physics and gameplay are deliberate additions around the recovered logic, with the original
  as the baseline; a faithful mode must always exist.
- Preserve: six levels across three difficulty tiers (the three-pass campaign) and Tornado Alley
  (replay unlocked levels with a chosen starting Fujita rating).
- Address: repetitive progression, few environments, awkward navigation around tall buildings,
  shallow rewards for destruction.

## Tiers

Tier 1, modernize without changing the core game:

- native Windows compatibility and input (keyboard remap, controller, mouse sensitivity);
- resolution (native 1440p/4K, ultrawide, windowed/borderless, UI scaling), uncapped frame rate
  with fixed simulation timing, camera fixes (zoom, see-through), target markers and radar;
- improved tornado rendering, lighting and particles;
- campaign and Tornado Alley preserved;
- saves (autosave, slots, checkpoints), settings, audio sliders, accessibility (contrast, screen
  shake, motion, subtitles), replay (record inputs and simulation events, free-camera playback,
  debug and perf tool).

Tier 2, make it feel new:

- progressive structural destruction (components, staged collapse), material-specific breakage
  (wood, sheet metal, glass, concrete), persistent wreckage that changes navigation and becomes
  ammunition;
- debris and vehicle physics (mass, drag, lift; light objects fly first; cars slide, roll, go
  airborne);
- chain reactions (power lines, fuel, industrial equipment);
- dynamic weather and wind, Arcade and Dynamic Storm profiles (energy budget, funnel width,
  rotation, debris density, surges, wind, rain, lightning, outflows, dust and smoke);
- environment reactions (trees bend and fall, crops flatten, water spray and waterspouts, fires,
  power outages);
- responsive enemies, emergency vehicles, light civilian and traffic simulation;
- abilities with trade-offs: satellite vortices, lightning, hail, wind surge, firestorm,
  precision debris grab/orbit/launch, boost, sharper control, wide-circulation mode;
- more maps (suburbs, dense city, farms, coast, airport, refinery, industry, mountain valley) and
  objectives.

Tier 3, long-term sandbox:

- Endless Destruction, Superstorm Survival, Sandbox, Scenario Creator with shareable files, map
  editor and mod interface;
- elaborate weather evolution, persistent environmental damage;
- photo mode, replay sharing, ghost runs, daily challenges, local and optional online
  leaderboards with score validation;
- progression: credits unlock variants, abilities, effects, starting strength, modifiers; stats
  (damage, buildings, largest collapse, longest chain); classic campaign kept, an expanded
  campaign separate.

## Three ideas to push furthest

1. Destruction feeds back into gameplay: collapse makes debris, changes terrain, exposes
   neighbours, supplies ammunition.
2. The tornado looks and behaves differently at each strength level (narrow translucent funnel
   to broad condensation funnel, multiple vortices, debris cloud, lightning, rain curtains),
   driven by the storm's real state.
3. Indefinite play: scenario editor, sandbox conditions, more objectives.

## What this means for the stages

- The d3d8 call log and the renderer map are the foundation for Tier 1 rendering and any later
  replacement.
- Recovering the sim tick, tornado state, object/debris tables and save format is what Tier 1
  (fixed timestep, saves, replay) and every Tier 2 system builds on.
- Faithful mode is a hard requirement: every addition switchable back, and the original
  behaviour stays the regression oracle.
