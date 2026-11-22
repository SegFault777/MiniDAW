# Recording

## Recording audio
1. Pick the *Input device* in **Preferences** (`Ctrl+,`). The default is the system default device.
2. Press **R** on the header of the Audio track you want to record so that it turns red.
3. If you like, switch on **Count-in** (it counts one bar before recording). The metronome is `K`.
4. Press **record** (`R`) to start; press stop (space) and the region is created.

### When the recording lands a little late
Audio devices have some latency, so the recorded sound can arrive after the beat. Enter a millisecond value in **Preferences > Recording latency compensation** and recorded regions are moved earlier by that amount.
Record one hand clap against the metronome, look at how late the waveform is, and use that value.

## Recording with a MIDI keyboard
1. Choose the keyboard in **Preferences > MIDI input**.
2. Select a Software Instrument track and arm it with R.
3. Press record and play. The keyboard plays the instrument of the selected track, and while recording the notes are written into a MIDI region.

## Playing with the computer keyboard (musical typing)
Native: **View > Musical Typing**. Vulkan: `Ctrl+Shift+K`. With it on, `A W S E D F T G Y H U J K` become the keys of a piano (C to the C above); `Z` / `X` lower and raise the octave. In this mode playing has priority over letter shortcuts.

## Cycle recording
Switch on cycle with **C** and set the range (Native: **Shift+drag** on the ruler; Vulkan: drag in the strip above the ruler), then record to practise the same section over and over.

## Capture MIDI (Vulkan version)
You played something great but were not recording? Select the instrument track and press `Ctrl+Shift+C` (**MIDI > Capture MIDI**). MiniDAW keeps what you played on the track during the last minute, even when the transport was stopped, and
puts it into a new MIDI region at the playhead with the original timing - the idea of Ableton Live's *Capture MIDI*.
