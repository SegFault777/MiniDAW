# Mixing and effects

## The mixer
The **Mixer** tab (the default) of the bottom panel has a channel strip for every track.
- The coloured pill on top: the track name
- Four insert slots: the names of the effects on the track
- Pan knob: drag up or down to move left and right; double-click to return to the centre.
- **Fader**: volume (dB). It sticks near 0 dB; double-click returns it to -1.9 dB (the default). The two bars beside it are the left and right level meters.
- **M / S / R**: mute / solo / record arm
- **Master** on the far right: overall volume and output level

## Inspector
Change the **colour**, volume, pan, **send A (reverb)**, **send B (echo)**, instrument (Synth, Drum Kit, VST3 instrument), instrument preset and the **insert list** of the selected track,
and the properties of the selected region (name, mute, gain, fade in / out, reverse, normalize, follow project tempo, transpose) in one place.

## Devices (effect and instrument knobs)
The **Devices** tab shows the instrument and the insert effects as **cards in signal-flow order**, from left to right.
- **Drag a knob up or down** to change it. Hold `Shift` for fine control; **double-click** for the default value.
- Top right of a card: **B** bypasses the effect for a moment, **x** deletes it, **E** opens the window of a VST3 plug-in.
- With more than 8 parameters, page through them with `<` `>` under the card.
- The **Add an effect** card at the end adds effects.

Built-in effects: Channel EQ, Compressor, Echo, Reverb, Overdrive, Chorus, Filter. (Send A goes to the reverb, send B to the echo.)
**View > Aux A: Reverb...** and **Aux B: Echo...** (Native) or the buttons in the Devices tab (Vulkan) edit the parameters of these two shared effects.

## Automation
In the **Automation** tab choose one of **Volume / Pan / Send A / Send B / Tempo**.
- Click an empty spot to add a point, drag points to move them, **right-click** to delete. Lines join the points.
- Positions follow the grid (Snap).
- **Tempo** is the change of the project tempo. One point means a constant tempo; add points and the tempo changes gradually.

## Tempo and time signature
- **Tempo...**: project tempo (20 to 300 BPM)
- **Tap Tempo** (`T`): tap along with the beat and the average becomes the tempo
- **Time Signature...**: changes the time signature from the current bar on. The ruler and the accents of the metronome follow.

## Markers
`M` adds a marker at the current position, `[` and `]` jump to the previous / next marker. Drag a marker on the ruler to move it; right-click to rename or delete it.

## MixConsole Snapshots (Vulkan version, from Cubase)
The **Snapshots** tab stores the level, pan, sends, mute and solo of every channel plus the master volume under a name.
- **Save Snapshot** (`Ctrl+Shift+Enter`) saves the current balance. **Recall** (or `Alt+1` ... `Alt+9`) brings it back; one `Ctrl+Z` undoes the recall.
- Rename with **Aa**, delete with **x**. Snapshots are saved inside the project file.

Use them to compare mixes, to keep a "verse" and a "chorus" balance, or to try something risky.

## Bouncing the final result
**File > Bounce to WAV...** (Native) / **Bounce / Export Audio...** (Vulkan), `Ctrl+B`
- Range: whole project / cycle range
- Format: 16-bit PCM, 24-bit PCM, 32-bit float
- For 16 bit: **dither**; if you want, **normalize** (target dBFS) and a 2 second **tail** for reverb and echo
- **Track > Bounce in Place**: renders the selected instrument track to audio.
