# Browser and loops

The **Browser** gathers three lists in one place. (Native: on the right; Vulkan: on the left.)

| List | Contents | Where you can drop it |
| --- | --- | --- |
| **Loops** | Audio files of the loops folder (a name such as `100 BPM` shows the tempo) | An Audio track, or an empty area of the arrangement (a new Audio track is created) |
| **Instruments** | Synth, Drum Kit, installed VST3 instruments | A Software Instrument track, or an empty area (a new track is created) |
| **Effects** | Built-in effects, MiniDAW plug-ins, VST3 effects | Any track (added as an insert) |

## How to use it
- Type in the **search box** to see only items whose name or vendor matches. `Esc` clears it.
- **One click**: a loop is auditioned at once. (**Preview** can be switched off: *View > Browser: Preview on Click* in Native, the *Preview* box in Vulkan.)
- **Double-click**: a loop goes to the playhead, an instrument or effect to the selected track.
- **Drag and drop**: a name tag follows the cursor and dropping works as in the table above.
- Click a heading (Loops, Instruments, Effects) to fold that list.
- `L` (Native) or `B` (Vulkan) hides and shows the whole browser.

## The loops folder
MiniDAW first looks for `loops` in the installation folder. To use another folder, choose it in **Preferences > Loops folder** (or with *Choose Loops Folder...*).
`.wav .mp3 .flac .ogg .m4a .aif .aiff` files, including those in sub-folders, appear in the list. (OGG may not open with the default Windows codecs.)

## Matching a loop to the tempo
An audio region plays at its original speed. To fit it to the project tempo
1. Select the region and open the **inspector**.
2. Switch on **Follow project tempo**. If the tempo is not known yet, it is analysed automatically.
3. If it does not fit, check the analysis with **Functions > Detect Tempo...**.

Or go the other way round (Vulkan): **Region > Adapt Project Tempo to Region...** changes the project tempo so that the loop lasts exactly the number of beats you give.
