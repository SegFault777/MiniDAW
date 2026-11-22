# Logic Pro projects (.logicx) - import pipeline

```
.logicx folder
  -> package reader      core/LogicImport        scanLogicPackage: Media/, Bounces/, Alternatives/*/ProjectData, plists
  -> plist reader        core/Bplist             MetaData.plist, ProjectInformation.plist (binary plists)
  -> ProjectData parser  core/LogicProjectData   chunk walk + media pool (+ pluggable extra decoders)
  -> arrangement decoder core/LogicArrangement   tempo, meter, markers, audio regions, MIDI regions + notes
  -> project model       core/LogicModel
  -> apply to session    core/LogicImport        applyLogicProject: tracks, regions at their positions, notes, tempo / meter map, markers
```

## Credit
Track names: **logicxkit** by Mike Farr (Apache-2.0, https://github.com/phierceweb/logicxkit) - see `THIRD-PARTY.md`.


The byte layouts of the arrangement (record framing, position origins 34560 / 38400, placement events, note events, gRuA / lFuA)
are taken from the reverse-engineering notes of **LogicProFormatWriter** by Jon Kubis (MIT licence, see `THIRD-PARTY.md`,
https://github.com/jonkubis/LogicProFormatWriter, `PROJECTDATA_FORMAT.md` and `logicx/session.py`). The code here is a C++
re-implementation, checked against that project's own decoder (see "Verification"). Not affiliated with Apple.

## ProjectData in short
- 24-byte header (magic `23 47 C0 AB`), then records until the end of the file. Record = 36-byte header (4-letter tag, role @+6,
  object index @+8, payload size u32 @+0x1c) + payload. The chain ends exactly at the end of the file.
- Positions are 960 per quarter note. Region placements use origin 34560 (`34560 + tick`), tempo / meter / marker / note events 38400.
- Tempo: qSvE whose payload starts `60 00 00 00`, 32-byte events, BPM x 10000 as u32 @+0x10.
- Meter: qSvE starting `30 00 00 00`, 80-byte header (initial signature: denominator exponent @+0x0b, numerator @+0x0c) + 48-byte changes.
- Markers: qSvE starting `12 00 00 00` (48-byte events) + names in qSxT records (RTF, `\cf2 NAME}`), linked by id.
- Audio: lFuA (file name, UTF-16LE) and gRuA (trim-in @pay+0x06, length @pay+0x16, name) records; 80-byte placement events (`0x24`) in any qSvE
  give track (u16 @+0x14), position, file index (@+0x2c / 4) and region number (@+0x28).
- MIDI: 80-byte placements (`0x20`), content = the note qSvE of the child sequence (@+0x20 / 4, else @+0x08 / 4), 32-byte notes
  (`90 00 00 00`, position region-relative, velocity @+0x0b, pitch @+0x0c, length @+0x1c). `*Automation` sequences are not regions.

## MIDI region length
u32 in 960-PPQ ticks inside the region's first `qeSM` record, at `0x78 + (even(nameLength) - 6)` (the name field, u8/u16 length @0x34 + text, sits in front of it
and shifts the field; the reference notes give 0x78 for the name "Inst 1"). Accepted when a multiple of 240 ticks and below 2000 bars. Checked: 39 of 39
regions of the reference fixtures are plausible and at least as long as their notes; 25 of 26 in the real project (the other is an empty region with a different
layout and uses the fallback). A 4-bar region of the real project (track 8, beat 16) reads as 16 beats.

## Track names
The arrange list is a run of `karT` rows (runs are separated by zero-size `karT` records; the run with NumberOfTracks + 1 rows is the arrange
list). Row i (1-based) is Logic's track number i - the number the placement events carry. A row's object id (u32 @row+8) is the object id
(u32 @+16) of an Environment record (`ivnE`), whose name is a u16 length @+158 followed by UTF-8 text. Found in the **logicxkit** project
(Apache-2.0, `logic/services/arrange/environment.py`, `tracklist.py`), re-implemented in `readLogicTrackNames`. The row-type byte (+51) is
not used: audio regions also sit on software instrument tracks (bounce in place).

## What is imported
Track names, tempo map, time signatures, markers, every audio region (file, position, trim-in, length) on one track per Logic track number, every
MIDI region with its notes, one software-instrument track per Logic track number. Audio files are looked up by file name in `Media`; each file is
decoded once. Without a readable ProjectData the recordings are imported one track each (the older behaviour).

## What is not (yet)
- **MIDI region length** is read from the region's `qeSM` (see above); when that field is missing or implausible (1 empty region of the real sample) the notes' extent rounded up to whole 4/4 bars is used (empty region: 1 bar).
- **Take folders / comps**, region fades, gain, flex, plug-ins, software instrument choice, mixer, automation, extended notes
  (note format byte 0x40), sysex.
- Only 44.1 kHz-based sessions were checked end to end; trim / length are scaled by the project sample rate.
- Verified on Logic Pro X 10.5 (real project) and on the reference project's 89 fixtures (Logic 11 / 12); other versions may differ.

## Verification
- Track names: `tests/data/logicx_tracknames.tsv` holds the names logicxkit reads on the same 89 fixtures; the C++ reader gives the same
  1443 names. On the real project the names match Logic's window capture (1 `CH_digital_piano [NICKY ROMERO]`, 3 `Studio Violins 1`, 6 `Intro`,
  43 `vox`, 44 `ending`, 60 `KSHMR Clock 05 - Countdown`).
- `tests/core_tests.cpp`: binary plist reader, chunk walker, synthetic files, the package reader and the pipeline with a stand-in decoder.
- `tests/data/logicx_reference.tsv`: what the reference decoder returns on its 89 fixtures; with
  `MINIDAW_LOGICX_FIXTURES=<LogicProFormatWriter folder>` the C++ decoder must match every row (audio, MIDI, notes, tempo, markers; meter counts
  are reported). Result: 89 / 89, 0 differences.
- `MINIDAW_LOGICX_SAMPLE=<a .logicx folder>`: full pipeline on a real project. The Logic Pro X 10.5 sample (111 tracks) gives 255 audio regions,
  28 MIDI regions, 912 notes, 82 BPM, one marker - the same numbers as the reference decoder - and the positions match Logic's own window
  capture (a vocal region at bars 9 and 33 on track 43, marker at bar 9).
- No real project is kept in the repository.

## Adding decoders for other layouts
`core::registerProjectDataDecoder(fn)` (tried before the built-in one): `bool(const uint8_t*, size_t, LogicProject&, std::string& name)`.
