# Keyboard shortcuts

`Ctrl` is Control. With **musical typing** on, letter keys play notes instead of acting as shortcuts.

## Native version (`MiniDAW.exe`)

### Playback and recording
| Key | Action |
| --- | --- |
| `Space` | Play / pause |
| `Enter`, `Home` | To the start |
| `R` | Start / stop recording |
| `C` | Cycle on / off |
| `K` | Metronome |
| `T` | Tap tempo |
| `Left` `Right` | One bar back / forward (with `Shift`: one beat) |
| `[` `]` | Previous / next marker |
| `M` | Add a marker at the current position |

### File
| Key | Action |
| --- | --- |
| `Ctrl+N` / `Ctrl+O` | New project / open |
| `Ctrl+S` / `Ctrl+Shift+S` | Save / save as |
| `Ctrl+I` | Import audio |
| `Ctrl+B` | Bounce to WAV |
| `Ctrl+,` | Preferences |

### Edit
| Key | Action |
| --- | --- |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z`, `Ctrl+Y` | Redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut / copy / paste region |
| `Ctrl+D` | Duplicate |
| `Ctrl+T` | Split at the playhead |
| `Ctrl+M` | Mute region |
| `Delete`, `Backspace` | Delete |
| `F2` | Rename (region or track) |
| `Up` `Down` | Select previous / next track |

### View
| Key | Action |
| --- | --- |
| `X` | Show / hide the bottom panel |
| `P` | Piano Roll tab |
| `I` | Inspector tab |
| `L` | Show / hide the browser |
| `+` `-` | Zoom in / out (or `Ctrl`+wheel) |
| `Esc` | Close menu / dialog, deselect |

### Piano roll (with the Piano Roll tab open)
| Key | Action |
| --- | --- |
| `Ctrl+A` | Select all notes |
| `Shift`+click | Add / remove a note from the selection |
| `Shift`+drag on an empty spot | Rectangle selection |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste notes at the playhead |
| `Ctrl+D` | Duplicate the selection right behind itself |
| `Delete` | Delete the selected notes |

### Mouse
| Action | Result |
| --- | --- |
| `Shift`+drag on the ruler | Set the cycle range |
| `Ctrl`+wheel | Zoom horizontally |
| `Shift`+wheel | Scroll horizontally |
| `Shift`+drag a knob | Fine adjustment |
| Double-click (knob, volume, pan) | Back to the default |

## Vulkan version (`MiniDAWVulkan.exe`)
Press `F1` in the program for the live sheet, or `Ctrl+K` to search. The table below lists the shortcuts of the registry.

### File
| Shortcut | Command |
| --- | --- |
| `Ctrl+N` | New Project |
| `Ctrl+O` | Open... |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save As... |
| `Ctrl+I` | Import Audio... |
| `Ctrl+B` | Bounce / Export Audio... |
| `Ctrl+,` | Preferences... |
| `Ctrl+Q` | Quit |

### Edit
| Shortcut | Command |
| --- | --- |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+X` | Cut Region |
| `Ctrl+C` | Copy Region |
| `Ctrl+V` | Paste Region |
| `Ctrl+D` | Duplicate Region |
| `Del` | Delete Region |
| `Shift+Del` | Ripple Delete Region |
| `S` | Split at Playhead |
| `Ctrl+J` | Join Regions |
| `Ctrl+Shift+M` | Mute Region |
| `F2` | Rename Region... |
| `Alt+Left` | Nudge Region Left |
| `Alt+Right` | Nudge Region Right |

### Track
| Shortcut | Command |
| --- | --- |
| `Ctrl+Alt+A` | New Audio Track |
| `Ctrl+Alt+S` | New Software Instrument Track |
| `Ctrl+Alt+M` | New Drum Machine Track |
| `Ctrl+Alt+D` | New Drummer Track |
| `Ctrl+Shift+D` | Duplicate Track |
| `Ctrl+Backspace` | Delete Track |
| `Shift+F2` | Rename Track... |
| `Ctrl+Alt+Up` | Move Track Up |
| `Ctrl+Alt+Down` | Move Track Down |
| `Shift+R` | Arm Record |
| `Shift+M` | Mute Track |
| `Shift+S` | Solo Track |
| `Up` | Select Previous Track |
| `Down` | Select Next Track |
| `Ctrl+Alt+B` | Bounce in Place |

### Region
| Shortcut | Command |
| --- | --- |
| `Tab` | Tab to Next Transient |
| `Shift+Tab` | Tab to Previous Transient |

Trim Start to Playhead and Trim End to Playhead (audio regions) have no shortcut: use the buttons of the audio editor or `Ctrl+K`. The same goes for the MIDI channel of a track (`Ctrl+K`, "midi channel 5").

### MIDI
| Shortcut | Command |
| --- | --- |
| `Q` | Quantize... |
| `Ctrl+R` | Riff Machine... |
| `Ctrl+Alt+R` | Roll New Riff |
| `Ctrl+L` | Logical Editor... |
| `Ctrl+Shift+C` | Capture MIDI |
| `Ctrl+F` | Fold Notes |
| `Ctrl+Shift+K` | Musical Typing |
| `Z` | Typing Octave Down |
| `X` | Typing Octave Up |

### Audio
| Shortcut | Command |
| --- | --- |
| `Ctrl+T` | Tempo... |
| `T` | Tap Tempo |
| `K` | Metronome |

### Mix
| Shortcut | Command |
| --- | --- |
| `Ctrl+1` | Show Mixer |
| `E` | Show Editor |
| `Ctrl+3` | Show Step Sequencer |
| `Ctrl+4` | Show Devices |
| `Ctrl+5` | Show Snapshots |
| `Ctrl+6` | Show Automation |
| `Ctrl+Shift+Enter` | Save Mixer Snapshot |

### View
| Shortcut | Command |
| --- | --- |
| `1` | Pointer Tool |
| `2` | Pencil Tool |
| `3` | Scissors Tool |
| `4` | Eraser Tool |
| `5` | Mute Tool |
| `B` | Show / Hide Browser |
| `I` | Show / Hide Inspector |
| `F5` | Workspace: Arrange |
| `F6` | Workspace: Mix |
| `F7` | Workspace: Edit |
| `Shift+X` | Maximize Bottom Panel |
| `X` | Show / Hide Bottom Panel |
| `+` | Zoom In |
| `-` | Zoom Out |
| `Z` | Zoom to Fit Project |
| `Shift+Z` | Zoom to Selected Region |
| `H` | Cycle Track Height |
| `F` | Follow Playhead |
| `G` | Snap to Grid |
| `Shift+G` | Next Grid Size |
| `Ctrl+K` | Command Palette |
| `F1` | Keyboard Shortcuts |

### Transport
| Shortcut | Command |
| --- | --- |
| `Space` | Play / Pause |
| `Enter` | Stop and Return to Start |
| `Home` | Go to Start |
| `End` | Go to End |
| `R` | Record |
| `C` | Cycle |
| `M` | Add Marker |
| `]` | Next Marker |
| `[` | Previous Marker |
| `Left` | Playhead Back One Bar |
| `Right` | Playhead Forward One Bar |

### Help
| Shortcut | Command |
| --- | --- |
| `Ctrl+K` | Command Palette... |
| `F1` | Keyboard Shortcuts... |

### Inside the piano roll (mouse over it)
| Key | Action |
| --- | --- |
| `B` | Draw mode on / off |
| `Q` | Quantize the selection (or all notes) with the toolbar value |
| `Left` `Right` | Nudge by the grid |
| `Up` `Down` | Transpose by a semitone (`Shift`: octave) |
| `Ctrl+A` / `Ctrl+C` / `Ctrl+X` / `Ctrl+V` / `Ctrl+D` | Select all / copy / cut / paste / duplicate notes |
| `Delete` | Delete the selected notes |
| `Esc` | Deselect |
| `Alt`+drag | Copy the notes while dragging |
| `Alt`+wheel | Vertical zoom of the keys |
