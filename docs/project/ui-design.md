# UI design: what we learned from five DAWs

The screens of MiniDAW were designed by **comparing products, taking in the strengths and avoiding the weaknesses**. None of them is copied. The first version of this comparison covered Logic Pro and Ableton Live and shaped the Native interface (v0.11); the Vulkan interface (v0.12) widened it to five DAWs.

## 1. Comparison

| Topic | Logic Pro | Ableton Live | FL Studio | Cubase | Pro Tools |
| --- | --- | --- | --- | --- | --- |
| **Overall layout** | Arrangement + inspector + mixer + editors in one window; screensets | Two views (Session / Arrangement), switch between them | Many floating windows (Playlist, Channel Rack, Mixer, Piano Roll) | Lower zone, right zone and left zone around the project window; workspaces | Edit window and Mix window, switch between them |
| **Strength** | Context stays in one place | Simplicity | Fast pattern-based sketching | Everything can be docked and recalled | Precise editing, speed on audio |
| **Weakness** | Dense at first sight, many windows | Switching views breaks context | Window clutter, unusual project model | Many panels and options | Dated look, steep learning for MIDI |
| **Tracks / clips** | Regions with name bar and waveform | Coloured clips | Clips in a Playlist, patterns | Parts and events | Clips on playlists |
| **Browser** | Library + separate Loop Browser | One **integrated browser** with search and drag and drop | Colour-coded browser and Channel Rack | MediaBay | Workspace browser |
| **Effect chain** | Insert slots + plug-in windows | **Horizontal device chain** with knobs in view | Mixer slots | Inserts in the channel strip and the inspector | Inserts |
| **Piano roll** | Strong; velocity colouring, region bar, Logical-style tools | Fold, scale highlight, Auto grid, ghost notes of other clips | Stamps (chords, scales), Riff Machine, ghost notes | Key editor, Logical Editor | Basic |
| **Step input** | Step Sequencer | Drum Rack | **Step sequencer you paint** | Drum Editor | Limited |
| **Audio editing** | Flex, Smart Tempo | Warping | Edison | Warp, sample editor | **Tab to Transient, Beat Detective, grid / slip modes**, Ripple |
| **Display** | Central **LCD** | Small top bar | Toolbar with song position | Transport panel | Counters |
| **Help** | Quick Help (off by default) | **Info View** always visible | Hint bar | Tooltips | Tooltips |
| **Mix recall** | Alternatives, channel settings | Presets per device | Mixer states | **MixConsole snapshots** | Snapshots |

## 2. Decisions

### Native interface (v0.11)
| Decision | What we took | What we avoided |
| --- | --- | --- |
| **One screen, a bottom panel switched by tabs** (Mixer, Inspector, Devices, Piano Roll, Step Sequencer, Drummer, Automation) | Logic's "context stays in one window" | Logic's many windows, Live's view switching |
| **An integrated browser on the right** (loops, instruments, effects + search + drag and drop) | Live's browser and drag and drop | Logic's scattered windows. It is on the **right** on purpose: the track headers use the left, so the timeline origin never moves |
| **Devices tab = horizontal card chain with knobs** | Live's device chain | Live's minimal mixer; we keep a Mixer tab like Logic |
| **Central LCD transport** | Logic's readability | Logic's shape: a rounded pill in two colours, teal / orange |
| **Three track heights** (Compact / Normal / Tall) | Tall shows the pan like Logic, Compact is as terse as Live | |
| **A colour strip on the track header, coloured regions** | Live's colour coding | Large icons |
| **Status-bar help** (usage when the mouse is over a control) | Live's Info View, but always on | A separate window or setting |
| **Slate and teal look**, flat with rounded corners | | Resembling the colours and forms of the other products |

### Vulkan interface (v0.12)
| Decision | Source | In MiniDAW |
| --- | --- | --- |
| One window with inspector, mixer, editors; **workspaces** `F5` Arrange / `F6` Mix / `F7` Edit | Logic screensets, Cubase workspaces | Workspaces change the layout in one key; every divider can be dragged |
| **Everything is a command**, `Ctrl+K` palette and `F1` sheet | (none of the five has it so completely) | One command table feeds menus, shortcuts, palette and sheet |
| Browser with search and drag and drop; Devices tab with knobs; **Auto grid**; Fold / Highlight / Lock; Capture MIDI | Ableton Live | Same ideas, with our own look |
| Chord **Stamp** menu, **Riff Machine**, ghost notes, painted steps and velocities | FL Studio | In the piano roll and the Steps tab |
| Tool box `1`-`5`, **Logical Editor**, **Snapshots** tab | Cubase | Snapshots store the whole mix and can be recalled |
| **Snap** (grid / slip), **Tab to Transient**, **Beat Detective**, **Ripple Delete**, `Alt`+arrows nudge | Pro Tools | Beat Detective refuses flexed / stretched regions and conforms without gaps |
| LCD, velocity-coloured notes, region bar, **Adapt Project Tempo to Region**, **Groove Template** | Logic Pro | In the transport bar and the piano roll |
| Vector drawing at any display scale | | Sharp text on every monitor, GPU drawing |

## 3. Principles (keep them when the screens change)
1. **One-screen principle:** do not make a new window for a new function. Solve it with a tab of the bottom panel or a dialog.
2. **What you see is what you operate:** change values in place by dragging (knob, slider, point). Typing numbers belongs in dialogs only.
3. **Colour means something:** the track colour continues in the region and the name chip of the mixer and in the knob arcs. Teal means "active now" (playing, selected, focus), orange means "attention / tempo", red means "recording".
4. **Explain on hover:** give every new control a status-bar text (`hintFor` in Native; the info line in Vulkan).
5. **Make slow things visible:** long jobs such as plug-in scanning run in the background and report when done.
6. **Everything must be undoable:** every edit can be undone with `Ctrl+Z`.
7. **One place for one thing:** one command table (Vulkan), one hit-registration per control (Native).

## 4. Not yet taken
- **Session view (clip launcher):** Live's biggest strength, but the engine needs a "start / stop a clip on the beat" function. See the [roadmap](roadmap.md).
- Logic's **Smart Tool** (the tool changes with the mouse position): we have explicit tools `1`-`5`; position decides the action only inside a tool.
- **Track stacks (folders)** and a **Flex Time editor**.
- FL Studio's pattern-based **Playlist** and Cubase's full **Project Logical Editor**.
