# Working with other programs (Logic Pro, Ableton Live, Cubase and more)

MiniDAW reads and writes the file types that every DAW understands.

## Opening and importing files
**File > Open**, drag and drop onto the window, and "Open with" accept three kinds of files:

| Kind | Files | What happens |
| --- | --- | --- |
| MiniDAW project | `.mdaw` | opens (after asking about unsaved changes) |
| Audio | `.wav` `.aif` `.aiff` `.aifc` `.mp3` `.mp2` `.m4a` `.mp4` `.m4b` `.3gp` `.aac` `.flac` `.wma` `.ogg` `.oga` `.opus` | imported as a new audio track at the playhead (or onto the selected audio track) |
| MIDI | `.mid` `.midi` `.kar` | imported as new tracks; in an empty project the tempo changes, time signatures and markers of the file are adopted too |

WAV and AIFF are read by MiniDAW itself. The other audio formats are decoded by Windows, so a format that Windows has no codec for (for example Ogg Vorbis on some systems) shows an error message for that file only. Files with another sample rate (48, 88.2, 96 kHz ...) are converted to 44.1 kHz with a high quality filter.

## Bounce / Export Audio (Ctrl+B)
| Choice | Options |
| --- | --- |
| Format | WAV 16-bit, WAV 24-bit, WAV 32-bit float, AIFF 16-bit, AIFF 24-bit, MP3 |
| Sample rate | 44.1, 48, 88.2, 96 kHz (MP3: 44.1 or 48 kHz) |
| MP3 bit rate | 128, 160, 192, 224, 256, 320 kbps |
| Tags (MP3) | title, artist, album; the comment "Made with MiniDAW" is added |
| Range | whole project or only the cycle range; optional 2 s tail for reverb and echo |
| Level | optional normalize, optional dither for 16-bit |
| Stems | one file for every track |

MP3 export uses the MP3 encoder of Windows (Media Foundation), so nothing extra is installed. WAV and AIFF are written by MiniDAW itself, so they work on any system.

### Stems (one file for every track)

By default the stems carry the master volume and the limiter of the project, so they sum to the same level as a normal bounce. Switch off **Stems: keep master volume and limiter** to get the stems with only their own track effects (inserts and sends): a mixer in another DAW can then apply its own master processing. Switch off **Stems: keep reverb and echo sends** to get dry stems: the Rev and Echo sends of the tracks are left out, so you can add your own reverb in the other program (the reverb tail is then not part of the files). Stems can be WAV, AIFF or MP3 like any other export; the format choice of the dialog applies to every file.
Tick **One file for every track** and choose a folder. Every track that makes sound becomes a file called `01 Name.wav`, `02 Name.wav` ... All files start at the same point and have exactly the same length, so you can drop them all into another DAW at bar 1 and they line up. Level and panning of the tracks are included, the master effects are too, and the mute / solo state of your project is not changed.

Tips for the other program:
- **Logic Pro**: File > Import > Audio File (or drag the files onto the tracks area); use **AIFF 24-bit** or **WAV 24-bit** at your project's sample rate.
- **Ableton Live, Cubase, Pro Tools, FL Studio**: drag the stems into the arrangement at bar 1.

## MIDI export and import
**File > Export MIDI File...** writes a Standard MIDI File (format 1). Besides the notes it contains:
- the whole **tempo map** (a tempo glide becomes one step per beat, because MIDI has no gliding tempo),
- every **time signature** change,
- the **markers** of the project,
- one track per instrument or Drummer track, with its name (Drummer tracks on channel 10).

**MIDI channels**: choose the channel of an instrument track in the Inspector (*MIDI Ch*, or `Ctrl+K` and type *midi channel 5*); in the Native version use Track > MIDI Channel.... *Auto* (the default) gives the instrument tracks the channels 1, 2, 3 ... in track order, skipping channel 10 (drums) and the channels you chose yourself; after 15 tracks the automatic channels start again at 1. The choice is saved in the project, and a MIDI file whose track plays on one channel gives the new track that channel, so a round trip keeps the channels.

Importing reads the same things back. Logic Pro, Ableton Live, Cubase, Pro Tools, FL Studio and others read and write these files, including their tempo and marker tracks.

**Import options** (File > Import MIDI File...): choose what happens with the tempo map and time signatures of the file - *Adopt if the project is empty* (the default, also used when you drop a file on the window), *Use the file's* (always replace the project's), or *Keep the project's* (never touch them). Tick *One track for every MIDI channel* when a file track mixes several channels (a type 0 file, or a track that plays drums and a bass): each channel becomes its own track, named for example "Piano (ch 2)", and channel 10 becomes a Drummer track. Program changes (sound choices) are not imported, because MiniDAW has its own instruments.

## Logic Pro projects (.logicx)

A `.logicx` file is really a folder (a macOS "package"). MiniDAW reads **the arrangement of a Logic Pro project**:

1. On the Mac, copy the whole `.logicx` folder (or compress it to a zip and unzip it on Windows) so that the folder keeps its name ending in `.logicx`.
2. In MiniDAW choose **File > Import Logic Pro Project Folder...**, or drop the folder on the window, or give it on the command line.
3. You get: the track names, the tempo and time signatures, the markers, every audio region on its track at its position (with the start and length that Logic shows),
   and every MIDI region with its notes. Audio files come from the project's `Media` folder; files in `.caf` format cannot be read (convert them to WAV or AIFF in Logic first).

What is **not** carried over: fades and region gain, Flex, take folders and comps,
software instruments and plug-ins, the mixer and automation. MIDI regions are made as long as their notes (rounded up to whole bars).
For a more complete move, also export **MIDI** (File > Export > All Tracks as MIDI File) and **stems** (File > Export > All Tracks as Audio Files) from Logic and open those here.
Logic's file format is not documented by Apple; this reader is based on community reverse engineering and was checked with Logic Pro X 10.5 and 11/12 test projects.

## What is not exchanged
- Plug-in settings and automation are not part of audio or MIDI files.
- MP3 is a compressed format: use WAV or AIFF to move material between programs, and MP3 only for listening.
