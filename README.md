# DistanceLODder

Unreal Engine 5 plugin that picks static mesh LODs by distance to the VR player instead of by the view's screen size.

Built for headsets with foveated quad views (e.g. Varjo with OpenXR `PRIMARY_STEREO_WITH_FOVEATED_INSET`). There, UE's LOD view is the gaze-following focus view, whose projection changes as the eyes move. Every mesh near an LOD threshold can then switch when the user looks around or turns their head, and the narrow focus view picks much more detailed LODs than the editor viewport.

DistanceLODder replaces that magnification with a fixed reference FOV, adds hysteresis and forces the result with `SetForcedLodModel`. LODs then depend only on where the HMD is.

Tested against UE 5.7.

## Installation

Clone or copy into your project's `Plugins/` folder and build the project:

```
cd YourProject/Plugins
git clone https://github.com/groatse/DistanceLODder.git
```

The plugin is enabled by default once it's in the project.

## What it affects

Tracked: `UStaticMeshComponent`s with 2 or more LODs whose mobility is Static or Stationary.

Skipped:
- Movable components
- Instanced and hierarchical instanced static meshes (ISM/HISM)
- Nanite meshes
- Components whose `ForcedLodModel` is already set by something else
- Actors or components with the opt-out tag

It runs in game and PIE worlds only, never in editor worlds, so forced LODs are never saved into levels. Meshes are picked up at begin play, when streamed levels become visible and when actors spawn.

## How LODs are chosen

UE uses LOD *i* while a mesh's screen size, `M × R / D`, is below the mesh's LOD screen size `S_i`. Here `M` is the projection magnification, `R` the bounds radius and `D` the distance. DistanceLODder uses the same rule with a fixed `M` from **Reference Vertical FOV**, so each LOD becomes a switch distance per component:

```
D_i = DistanceScale × M × R / S_i        M = 1 / tan(ReferenceVerticalFOV / 2)
```

A mesh moves to a coarser LOD only beyond `D_i × (1 + hysteresis)`, and back to a finer one only within `D_i × (1 − hysteresis)`.

LOD changes rebuild the component's render state (a CPU cost), so they're rate-limited:
- LODs are only re-evaluated after the HMD has moved more than **Movement Threshold**.
- The checks are spread over several frames.
- At most **Max LOD Changes Per Frame** changes are applied per frame, nearest meshes first.

The first pass at begin play applies everything at once.

## Settings

Project Settings → Plugins → DistanceLODder:

| Setting | Default | |
|---|---|---|
| Enabled | on | Master switch. When off, nothing runs. |
| Opt Out Tag | `NoDistanceLOD` | Actor or component tag that excludes a mesh. |
| Reference Vertical FOV | 59° | 59° matches the editor viewport at 90° horizontal, 16:9, so screen sizes tuned in the editor behave the same. |
| Distance Scale | 1.0 | Multiplies all switch distances. Lower values switch to coarser LODs sooner. |
| Hysteresis Percent | 10% | Width of the band around each switch distance. |
| Use Global Screen Sizes | off | Use the LOD1–LOD3 screen sizes below for every mesh instead of each mesh's own. Meshes are capped at LOD3. |
| LOD1 / LOD2 / LOD3 Screen Size | 0.5 / 0.25 / 0.125 | Used when global screen sizes are on. |
| Movement Threshold | 25 cm | |
| Max LOD Changes Per Frame | 32 | |
| Max Components Evaluated Per Frame | 4096 | |

Reference FOV, distance scale and hysteresis apply immediately. The global screen size settings apply to meshes picked up after the change.

## Viewpoint

The first local player's pawn: its first camera component with **Lock to HMD** enabled, or the pawn's eye view point if it has none.

## Runtime control

Console:

| Command | |
|---|---|
| `DistanceLODder.Enabled 0/1` | Hands all meshes back to UE's LOD selection, or takes them over again. |
| `DistanceLODder.ShowDebug 0/1/2` | 1: a colored point per tracked mesh, visible in the HMD. 2: point plus LOD text. White LOD0, green LOD1, yellow LOD2, orange LOD3, red LOD4+, grey not yet forced. |
| `DistanceLODder.DebugDistance <cm>` | Draw range for the debug view (default 3000). |
| `stat DistanceLODder` | Tracked count, evaluations, changes and queue size per frame, tick time. |

Blueprint, on the `DistanceLODder Subsystem` world subsystem:
- `Refresh Actor`: re-checks an actor's meshes, for example after changing its tags at runtime. Tags are otherwise only read when a mesh is picked up.
- `Request Full Update`: re-evaluates everything on the next tick.
- `Get Num Tracked Components`

## Limitations

- **Forced LODs pop:** they don't use dithered LOD transitions.
- **Every view gets the forced LOD,** including spectator views and scene captures.
- **Moving actors:** bounds are cached when a mesh is picked up, so a Static or Stationary mesh moved at runtime keeps its old position. Call `Refresh Actor` after moving one.
- **Components added at runtime** to an existing actor aren't picked up until `Refresh Actor` is called.
- **Skeletal meshes, ISM/HISM and Nanite** are not handled.

## Tests

Unit tests for the LOD math (`Source/DistanceLODder/Private/Tests/`) use UE's Automation Spec framework. They're only compiled in builds with `WITH_DEV_AUTOMATION_TESTS`. One test checks the switch distances against the engine's own `ComputeBoundsDrawDistance`.

Run them in the editor under Tools → Test Automation (`DistanceLODder.*`), or headless:

```
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests DistanceLODder;Quit" -unattended -nullrhi -nopause -testexit="Automation Test Queue Empty"
```

## License

MIT, see [LICENSE](LICENSE).
