# Editing

## Regions
A region is a "piece" lying on a track. An audio region holds part of a recording or file, a MIDI region holds notes.

| To do this | Native | Vulkan |
| --- | --- | --- |
| Select | click | click |
| Move | drag (follows the Snap grid) | drag (follows the grid; hold `Alt` to move freely) |
| Resize | drag the right edge | drag the right edge |
| Duplicate while moving | - | `Alt`+drag (Logic Pro / Pro Tools) |
| Split | put the playhead there and `Ctrl+T` | `S` at the playhead, or the **Scissors** tool (`3`) and a click |
| Copy / cut / paste | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` (paste goes to the playhead) | same |
| Duplicate | `Ctrl+D` | `Ctrl+D` |
| Delete | `Delete` | `Delete` (the **Eraser** tool `4` deletes by clicking); `Shift+Delete` is a **Ripple Delete** that closes the gap (Pro Tools Shuffle) |
| Mute | `Ctrl+M` | `Ctrl+Shift+M`, or the **Mute** tool (`5`) |
| Rename | `F2` | `F2` |
| Create | double-click an empty part of a Software Instrument / Drummer lane (4 bars) | the same, or draw with the **Pencil** (`2`) |

All editing functions are in the **Functions** menu (Native) or the **Region / MIDI / Audio** menus (Vulkan), and in the right-click menu of a region.

### All regions
- **Repeat Region...** - puts N copies one after another.
- **Join Regions** - merges the region with the next one. MIDI notes are combined; audio from the same recording is joined.

### MIDI region functions
| Function | Description |
| --- | --- |
| Quantize... | Moves notes onto a grid (1/4 to 1/32) with strength (0-100%), swing and an option to quantize lengths too. |
| Scale Quantize... | Moves notes into the key and scale you choose. 12 scales. |
| Humanize... | Loosens timing and velocity a little so it sounds played by a person. |
| Arpeggiate... | Spreads stacked notes into an arpeggio. |
| Chord Trigger... | Turns single notes into chords (10 types). |
| Transpose... | -48 to +48 semitones |
| Crescendo / Decrescendo... | Changes velocity gradually from a start to an end value. |
| Fixed Note Length... | Makes all notes the same length |
| Scale Velocity... | Multiplies velocity (%) and adds an offset |
| Legato | Stretches each note up to the next one. |
| Fix Overlaps | Cuts the earlier note when the same pitch overlaps. |
| Remove Duplicate Notes | Keeps one of two identical notes. |
| Retrograde | Plays backwards |
| Invert Pitches | Mirrors the pitches around the middle of the lowest and highest note. |

### Audio region functions
| Function | Description |
| --- | --- |
| Strip Silence... | Cuts away the parts quieter than a threshold (dB). You set the minimum silence length and padding; the cut edges get a short fade. |
| Detect Tempo... | Analyses the tempo and lets you choose *follow the project tempo* or *set the project tempo to this value*. |
| Reverse Region | Plays backwards |
| Trim Start to Playhead / Trim End to Playhead | Cuts the start or the end of the region at the playhead (Vulkan version; the audio keeps its place, the region only gets shorter). Not for stretched or transposed audio. See [the audio editor](vulkan-interface.md). |
| Normalize Region | Raises the level until the loudest peak reaches the maximum. |
| Measure Loudness | Reports the integrated loudness of the region in LUFS (ITU-R BS.1770 / EBU R128). |

Strip Silence cannot be used on audio that has been stretched or transposed (Follow project tempo, Transpose).

The **gain** and the **fade in / out** of an audio region are set in the inspector (sliders) or, in the Vulkan version, by dragging the fade handles in the [audio editor](vulkan-interface.md).

## Functions borrowed from other DAWs (v0.12, Vulkan version)
| From | Function | What it does |
| --- | --- | --- |
| **Logic Pro** | **Adapt Project Tempo to Region...** (Region menu) | Sets the project tempo so the selected audio region lasts exactly N beats (0 = guess the beats from the audio). Drop in a drum loop and the whole project follows it. |
| **Logic Pro** | **Make Groove Template** / **Apply Groove Template...** (MIDI menu) | Takes the timing and velocity feel of one MIDI region and lays it over another, with a strength control. |
| **Ableton Live** | **Capture MIDI** (`Ctrl+Shift+C`) | Turns what you just played, without recording, into a MIDI region. See [Recording](recording.md). |
| **Ableton Live** | **Fold** and **Scale Highlight** in the piano roll; **Transform** tools | Fold shows only the rows that have notes or belong to the scale; Highlight shades the scale and outlines notes outside it; Stretch x2 / /2, Strum, Double Pattern in the Functions menu. |
| **FL Studio** | **Chord Stamp** (Stamp menu of the piano roll) | One click writes a whole chord (any of 10 types, or a diatonic triad / seventh in the key). |
| **FL Studio** | **Riff Machine...** (`Ctrl+R`, `Ctrl+Alt+R` for a new roll) | Writes a musical melody in the key and scale you pick; the seed decides which riff you get. |
| **Cubase** | **Logical Editor...** (`Ctrl+L`) | Filter notes by pitch, velocity, length and position (two conditions) and then delete, transpose, set or scale velocity and length, move... all the matching notes. |
| **Cubase** | **MixConsole Snapshots** | Save the whole mixer balance under a name and recall it; see [Mixing](mixing-and-effects.md). |
| **Pro Tools** | **Tab to Transient** (`Tab` / `Shift+Tab`) | Jumps the playhead to the next / previous hit of the selected audio region. |
| **Pro Tools** | **Beat Detective...** (Region menu) | Cuts an audio region at its transients and can pull the pieces onto the grid (conform). |

## Piano roll
Double-click a MIDI region, or open the **Piano Roll** tab (`P`, Native) / **Editor** tab (`E`, Vulkan).

- **Add a note**: click an empty cell; drag to the right to set the length.
- **Move**: drag a note (time and pitch). **Resize**: drag the right end of a note.
- **Delete**: right-click a note, or select it and press `Delete`.
- **Select several notes**: `Shift`+click adds / removes, `Shift`+drag on an empty spot draws a rectangle, `Ctrl+A` selects all.
  Selected notes move and resize together, and you can use `Ctrl+C` / `Ctrl+X` / `Ctrl+V` (paste at the playhead) and `Ctrl+D` (duplicate right behind).
- **Velocity**: drag the bars in the lower area up and down.
- **Keys**: click the keys on the left to hear them.
- **Zoom**: `Ctrl`+wheel (horizontal), wheel (vertical scroll), `Shift`+wheel (horizontal scroll)

### The Vulkan piano roll
The Vulkan version's piano roll follows the MIDI editors of Logic Pro and Ableton Live:
- A real **piano** on the left; click or glide across it to hear the notes.
- **Velocity colours** (Logic Pro): blue = soft, green, yellow, red = hard; switch to track colour with the *Colour* menu. Notes show their names when long enough.
- **Region bar** above the ruler (Logic Pro): drag its end to resize the region.
- **Grid** menu with **Auto** (Ableton Live), **Snap**, and a **Quantize** button with its own value (`Q`).
- **Functions** menu: Scale Quantize, Humanize, Legato, Fixed Length, Transpose, Crescendo, Stretch, Strum, Double Pattern, Retrograde, Chord Trigger, Arpeggiator, Logical Editor, Groove ...
- The **chord** of the selected notes (or of what sounds at the playhead) is named in the toolbar: C, Am, G7, Cmaj7, C/E ...
- **Key and scale**, **Highlight**, **Fold**, **Lock** (notes snap to the scale), **Stamp** (chords), **Ghost** notes of the other regions.
- A **velocity lane** you can paint: drag across the notes. Drag the divider above it to make it taller.
- Keys: `B` draw mode, `Q` quantize, arrows nudge in time (left / right) or by a semitone (up / down; `Shift` = octave), `Ctrl+D` duplicate, `Alt`+drag copy, `Delete` remove, `Esc` deselect.
- Two zoom sliders (H and V) in the toolbar and the **info line** in the status bar that describes the note under the mouse.
- `Shift+X` makes the bottom panel as large as possible; `F7` switches to the Edit workspace.

## Step sequencer
Select a Drum Machine track (double-click its header) and the **Step Sequencer** tab opens. Click the cells of 4 rows (Kick, Snare, Hi-Hat, Clap) x 16 steps to switch them on or off; drag to paint several cells.
While playing, the current step has a white outline. The pattern repeats one bar (sixteen 16th notes) at a time.
**Track > Convert Pattern to MIDI Region...** turns the pattern into a MIDI region of any number of bars.

## Drummer
Select a Drummer region and open the **Drummer** tab.
- **Style**: Rock, Pop, Hip-Hop, House, Funk, Disco
- **Complexity / Loudness / Swing**: sliders
- **Fills**: a fill at the end of bars
- **New variation**: the same settings played differently (changes the random seed)
- **Bars**: region length

## Undo
`Ctrl+Z` (undo), `Ctrl+Shift+Z` or `Ctrl+Y` (redo). Tempo and automation changes can be undone too. The last 60 steps are kept. A drag (a fade handle, or the gain and fade sliders of an audio region) is one step, not one per movement.
