# Blackbox firmware changelog

Compiled from the [1010music Blackbox firmware downloads forum](https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads), official history threads, and per-release posts. Latest **3.1.9** notes are from [1010music.com/downloads](https://1010music.com/downloads).

## Gaps

- **1.9-beta**: ZIP download removed from thread (only upgrade guide PDF remains); forum points users to 2.1.5 thread
- **1.7.0**: Linked ZIP returns 404; 1.7.4 and 1.7.F from same thread are archived
- **1.4.1**: Linked ZIP returns 404; 1.4.0 and 1.4.3 are archived

## Official history threads

### 2019

BLACKBOX FIRMWARE UPDATE HISTORY

1.3.6
12-06-2019When using MIDI sync with a changing tempo, the blackbox was slow to catch up. It would sometimes take 5-10 seconds for sync to resume.
The Beat Synchronized delay would not update following a tempo change.
Program Change messages were only accepted on the PADS MIDI channel.

1.3.5 Beta
11-15-2019Reverse mode in sample mode starts from the wrong position (introduced in version 1.2)
The octave number on the piano roll doesn't match the KEYS screen
The HighQ interpolation setting does not work on empty cells, for use in templates
The MIDI Keys and Pads options don't match the operation in the manual

1.3.3 (no longer available)
11-15-2019Saving presets with sequences heavily loaded with notes across multiple pads would crash the box
Slice mode would add a tiny fade to the start of the slice. This is now removed. Use the attack parameter if you are getting unwanted clicking.
The Reverb Damping parameter didn't work like version 1.2
There is a new HighQ interpolation mode for Sample mode. Look on the ADSR page. The default is Normal. Switch to HighQ to spend more CPU per voice and reduce the amount of aliasing.
Saving a WAV file would not always include the root note
The number of allowed modulation mappings per cell was limited to 8. This number is now 12, which should take care of the most demanding mappings.

1.3
11-15-2019MIDI CCs. Use the built in Learn Mode to connect your MIDI controller to these parameters:
Level
Pitch
Filter Cutoff
Start Position, Length, Loop Start, and Loop End
Slice Selection (Slicer Mode)
Playback Speed (Granular Mode)
Delay Time and Feedback
Reverb Decay and Damping
Multisampling. Load multiple samples per pad. Navigate to a directory and choose File: Load AllBy default, the mapping is chromatic starting from C2
When embedded smpl tags are present, blackbox will map the samples across the keyboard automatically. This tag is written by Sample Robot and blackbox when saving files in sample mode.
We will supply 20 pre built sets shortly.

Support for MIDI clock output to USB devices. Enables the arpeggiator on the Novation LaunchKey Mini MK3 to run in sync with the main sequencer
New setting for the selection of MIDI clock source in TOOLS. The LaunchKey is always sending clock and you will generally want blackbox to ignore it.
Improved knob value stability for some types including dB, percentage, and pitch

1.2 Beta
08-23-2019Simple granular synthesis. We built this enhancement in response to requests for loop crossfading--it takes the idea much further. Look for a new option in the Sample/Clip/Slicer drop down menu. Granular mode works as an extension to sample mode. You can define the pitch, start, stop, length, loop mode, etc. You also get these parameters per voice:Grain size: 1024 to 16384, which is about 10ms to about 200ms
Grain count: 2-8. Running 8 grains sounds cool but eats up the processor
Spread: The amount of randomization in grain position surrounding the
playhead
Speed: How quickly the playhead advances through the waveform.

Copy and paste of PADS. From the PADS screen, turn the lower right knob to the right as you will see a new control panel.
Recording templates: Configure the pad mode, launch mode, envelope, and MIDI setup--before recording the sample
MIDI Keys channel. Hook a keyboard to the blackbox and it will play the active pad directly.
Knob control of the event step as well as pitch and duration in the SEQ editor.
MIDI output channel per pad
Control of the edit position in the text edit window. You can finally change the beginning of a file or preset name.
Pressing the grid button in the upper left corner of most control panels lets you switch PADS or SEQS directly without returning to the main screen
New navigation style for PADS. There are now only two main pages per pad: The Waveform and the Control Panel. The control panel has several sub pages directly accessible by a toolbar at the bottom

Bug fixes:Saving or Trimming WAV files a few minutes in length would crash the box
Sometimes the knobs, particularly the upper right one, would jitter
Sometimes the thruing of MIDI controllers would alter the value slightly
On short envelopes, there was aliasing

1.1 Beta
07-12-2019Destructive editing. You can now trim a sample using the start and length controls in sample mode
WAV file saving. You can embed loops, root note, beat count, and slice markers into WAV files for future use
Control of the root note of a sample for matching the internal keyboard and MIDI
Control of the beat count of a clip for those recordings with odd beat counts and far tempo jumps
MIDI thru. You can now play external MIDI from the touch screen. Inbound MIDI will also route to the active SEQ and its MIDI channel.
Clips will now hold until you press the Play button. This allows you to stage several at once and unleash them in time.
Improved capture of SEQ changes when recording song sections.

Bug fixes:
There was digital bleed through from the Out 3 signal to the headphones and vice versa.

1.0.6
06-07-2019Fixed bug where sometimes noise will appear on the output when attempting to record samples
Updated Reverb algorithm with damping control. Now capable of longer, more sustained reverberations and CPU utilization is better.
Updated FX1 and FX2 send curves to make the algorithms more audible at lower amounts. This will mean presets created on version 1.0.2. will now have a little more FX than before.
Added fix for MIDI controllers taking too much CPU performance while connected. (first appeared in version 1.0.3).

1.0.2
04-19-2019Swing control at the top of the song page
Support for cue points embedded in WAV files for slice markers. (See Reaper and SoundForge as methods to create these points)
Support for loop points embedded in WAV files
FX1 and FX2 are now effects busses and controlled from the FX screen. Each pad can send 0-100% of its signal to FX1 and FX2.
Recording now has an input gain control
Recording now has an output destination for the record monitoring
MIDI output from the SEQ cells will now send note numbers corresponding to Blackbox and Bitbox pads

### 2020

1.6.5
7-10-2020

Version 1.6.5 fixes the following:There is sometimes a faint click at the end of a sample when playing multiple at once

1.6
7-3-2020

Blackbox 1.6 has been a long time in the making. It is the debut of a number of new features, including the following:Automated multisample recording. Use the internal wizard to sample a MIDI or CV+Gate synth. Please note that the feature will allow you to record up to 127 notes by 16 velocity layers. The firmware will only support 64 samples per pad--so choose wisely.
Loop crossfading
Shared voice pool for sample mode provides up to 16 voices per pad
Improved micro SD streaming for up to 16 streamed pads at once
The ability to delete a directory and well as a file
Lots of bug fixes
Thank you for your support. We hope you enjoy the new version.

1.5 beta
3-11-2020

We are pleased to make available a new update to blackbox that reorganizes presets based on your input. Version 1.5 includes these features:Presets are now stored in subfolders under the \Presets path. For example: \Presets\DrumKit\, \Presets\Piano\, etc.
Recordings are now captured in the current preset folder instead of the root directory
There is a new Pack command that will copy all the files used by a preset into the current preset folder
There is a new Clean command that will delete any unused files in the current preset folder
In summary, you can now easily consolidate your presets into folders for archival and copying. We agree that this is a better way to work.
PLEASE MAKE A BACKUP OF YOUR MICRO SD CARD BEFORE TRYING THIS NEW VERSION

We have done several extra rounds of testing with this new version to be extra careful with your precious data. However, the possibility remains that something bad may happen in the migration process. Please make a backup.

Since this is a complex topic, we have also prepared a short user guide to help.

1.4.3
2-10-2020

Version 1.4.3 fixes some newly introduced issues from 1.4:Some WAV files would not play (specifically those with uncommon WAVEFORMATEXTENSIBLE tags)
It was not possible to load files from directories with a period in the name

1.4.1
2-06-2020Incoming program change messages will now only be accepted on the Keys or Pads channels, including Omni modes. As before, the Tools->ProgChange option must be ON.
Pitch bend works again. Keep in mind you need to manually configure the PTCH modulation type and map that to the Pitch parameter or whatever.
32-bit Integer WAV files now work. 32-bit floating-point WAVs worked previously.
There are two small enhancements:Metronome volume control.
The metronome is now added to the mix after the headphone mix. In other words, if you assign it to Out1 it will only appear on Output 1 and not the headphone output as well.

1.4.0
1-14-2020Enhanced slicer mode with the ability to play into adjacent slices, quantize, choke, and sync with the master clock. One way to think of this is as a random access clip mode.
Grid-based keyboard with scale control. Press KEYS a second time to see it.
Alternative grid keyboard for slicer cells that lets you trigger slices more accurately
Swipe to play adjacent keys in the original and new grid KEYS mode.
The load screen now displays directories first followed by files
You can now choose which MIDI note will trigger a pad on the MIDI pads channel

Bug fixes:The system would offer no warning when the micro SD card is running out of space. (You will see red text in the header when there is less than 256MB. Look for the available space on the Tools page.)
When using the MIDI keys feature to route incoming notes to a specific cell, notes would hang if you switched cells while playing
The file system could not handle directories with a period in the name
When using the slice sequencer, the cell would not begin with the first slice after loading a preset.

## Per-release notes (archived builds)

## gamechanger-0.1.2

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/23600-gamechanger-for-blackbox-0-1-2
- ZIP: `gamechanger012.zip` (2699998 bytes)
- BIN: `firmware/gamechanger/0.1.2/BLACKBOX.BIN` sha256=`97824afe6e809da86a6ad7a25b1a965f910e5d50b78e3e7e8a299fb36372bf37` size=389884

On April Fools Day, we announced a bogus technology called Gamechanger. We made up various silly claims in our press release and video. We also demonstrated a real port of Doom for blackbox that we are making available below. Please consider this work bonus freebie and something that will only be available on a limited basis. We believe we have the rights to distribute this work under the open source and shareware licenses on the material. But, that is subject to change based on the large amount of other work included. Here is more detail on what we built:

Gamechanger for blackbox makes use of the following:Doom open source code on GitHub
Chocolate Doom
Chocolate Doom for the STM32F4 Discovery Board
Doom 1.8 Shareware Edition for the publicly available game levels
As a starting point, we had working Doom code for a sister platform to the blackbox. One key omission was the sound. We created a basic audio playback engine to bring the sound effects to life. We also connected the MIDI input to enable you to play each sound effect with MIDI note on messages. The game includes a General MIDI sound track that remains silent in our version.

You may be wondering why we don't make our work available as open source. What we built is an ugly prototype--not something suitable for external review and consumption. We took various shortcuts to make something playable in a few days. For example, we used a bunch of our test code meant for internal use. We did not want to spend the extra time and effort to build something we could publish as open source. This work is just an Easter egg and April Fools' prank.

Installing Gamechanger for blackbox is very similar to installing blackbox firmware: Unpack the ZIP file and copy the contents onto a micro SD card. Unlike blackbox firmware, please make sure to also copy the "doom" folder and its contents onto the micro SD card or it won't boot up.

Here are the key mappings:BACK: Esc
INFO: Enter
PADS: Fire
KEYS: Open
MIX: Left Arrow
PSET: Up Arrow
TOOLS: Right Arrow
REC: Strafe Left
PLAY: Strafe Right
STOP: Down Arrow
SEQS: Maps
One noteworthy omission is the Weapon select keys, normally 1-7. We did not have sufficient buttons available to map them.

Lastly, please know that we are making this runtime available a fun bonus treat. We won't be providing support for those having trouble or installing or using it. You are on your own. Please help each other out. Also, while it may be fun to post ideas about how we could make this project better--please do so with the expectation that we have far more pressing work to do and do not plan to make any updates whatsoever.

We hope you enjoy Gamechanger for blackbox:

Download Gamechanger 0.1.2

## 1.0.2

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/3976-blackbox-version-1-0-2
- ZIP: `Blackbox102.zip` (320226 bytes)
- BIN: `firmware/bins/1.0.2/BLACKBOX.BIN` sha256=`155b5503b5a9af71a4d8d26373eca3bdabb2ef7857987ac94dbc947a6819512d` size=468776

Hello all,

We are finalizing the firmware in preparation for our next round of production. Here is a release candidate of version 1.0. We would love to get your feedback on this new version during the next few days so that we can be confident it is solid.

Here is what's new:Swing control at the top of the song page
Support for cue points embedded in WAV files for slice markers. (See Reaper and SoundForge as methods to create these points)
Support for loop points embedded in WAV files
FX1 and FX2 are now effects busses and controlled from the FX screen. Each pad can send 0-100% of its signal to FX1 and FX2.
Recording now has an input gain control
Recording now has an output destination for the record monitoring
MIDI output from the SEQ cells will now send note numbers corresponding to to Blackbox and Bitbox pads
Of course, there are lots of bug fixes, too numerous to mention.

Download blackbox 1.0.2

Thanks very much for your support. We look forward to hearing what you think.

## 1.0.6

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/4813-blackbox-1-0-6
- ZIP: `Blackbox106.zip` (315341 bytes)
- BIN: `firmware/bins/1.0.6/BLACKBOX.BIN` sha256=`e30166b32c9dda92a329a429dde1c103191ff677dad1eba0c834406ba93db059` size=465796

Version 1.0.6 is a minor update. Here are the differences since 1.0.2:Fixed bug where sometimes noise will appear on the output when attempting to record samples
Updated Reverb algorithm with damping control. Now capable of longer, more sustained reverberations and CPU utilization is better.
Updated FX1 and FX2 send curves to make the algorithms more audible at lower amounts. This will mean presets created on version 1.0.2. will now have a little more FX than before.
Added fix for MIDI controllers taking too much CPU performance while connected. (first appeared in version 1.0.3).

Download Blackbox 1.0.6

## 1.1.1-beta

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/5858-blackbox-1-1-beta
- ZIP: `Blackbox111.zip` (325410 bytes)
- BIN: `firmware/bins/1.1.1-beta/BLACKBOX.BIN` sha256=`cba04ff0a7472140f17def0267475d1677ae41c48c0b38640ebcb71dd7cba574` size=483408

We are pleased to make available our first feature update to blackbox. Version 1.1 includes the following:Destructive editing. You can now trim a sample using the start and length controls in sample mode
WAV file saving. You can embed loops, root note, beat count, and slice markers into WAV files for future use
Control of the root note of a sample for matching the internal keyboard and MIDI
Control of the beat count of a clip for those recordings with odd beat counts and far tempo jumps
MIDI thru. You can now play external MIDI from the touch screen. Inbound MIDI will also route to the active SEQ and its MIDI channel.
Clips will now hold until you press the Play button. This allows you to stage several at once and unleash them in time.
Improved capture of SEQ changes when recording song sections.
Version 1.1 includes the following bug fixes:There was digital bleed through from the Out 3 signal to the headphones and vice versa.

We appreciate your support. Thanks for all the suggestions, comments, bug reports, and love.

(this has been superceded by version 1.1.1, below)

## 1.2.2-beta

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/6812-blackbox-1-2-beta
- ZIP: `blackbox122.zip` (389215 bytes)
- BIN: `firmware/bins/1.2.2-beta/BLACKBOX.BIN` sha256=`4704bde6abdfdff16fd5f64dd426261125e6d951118138e638644a5146200dba` size=581988

Thanks for all the suggestions and feedback. Based on your input we are pleased to make available our next beta release, version 1.2. Here is the list of what's new:Simple granular synthesis. We built this enhancement in response to requests for loop crossfading--it takes the idea much further. Look for a new option in the Sample/Clip/Slicer drop down menu. Granular mode works as an extension to sample mode. You can define the pitch, start, stop, length, loop mode, etc. You also get these parameters per voice:Grain size: 1024 to 16384, which is about 10ms to about 200ms
Grain count: 2-8. Running 8 grains sounds cool but eats up the processor
Spread: The amount of randomization in grain position surrounding the playhead
Speed: How quickly the playhead advances through the waveform.

Copy and paste of PADS. From the PADS screen, turn the lower right knob to the right as you will see a new control panel.
Recording templates: Configure the pad mode, launch mode, envelope, and MIDI setup--before recording the sample
MIDI Keys channel. Hook a keyboard to the blackbox and it will play the active pad directly.
Knob control of the event step as well as pitch and duration in the SEQ editor.
MIDI output channel per pad
Control of the edit position in the text edit window. You can finally change the beginning of a file or preset name.
Pressing the grid button in the upper left corner of most control panels lets you switch PADS or SEQS directly without returning to the main screen
New navigation style for PADS. There are now only two main pages per pad: The Waveform and the Control Panel. The control panel has several sub pages directly accessible by a toolbar at the bottom
Bug fixes:Saving or Trimming WAV files a few minutes in length would crash the box
Sometimes the knobs, particularly the upper right one, would jitter
Sometimes the thruing of MIDI controllers would alter the value slightly
On short envelopes, there was aliasing
Download blackbox 1.2.2

Please check it out and let us know what you think. Thank you for your support

The 1010music Team

## 1.3.5-beta

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/8747-blackbox-1-3-5-beta
- ZIP: `blackbox135.zip` (395845 bytes)
- BIN: `firmware/bins/1.3.5-beta/BLACKBOX.BIN` sha256=`f9cc3c2cf4df9e7b6a080208caca2d2b7318533c7b0ec048bb73f73bf8ede337` size=591792

Version 1.3.5 fixes the following bugs in 1.3:Reverse mode in sample mode starts from the wrong position (introduced in version 1.2)
The octave number on the piano roll doesn't match the KEYS screen
The HighQ interpolation setting does not work on empty cells, for use in templates
The MIDI Keys and Pads options don't match the operation in the manual
Download blackbox 1.3.5

## 1.3.6

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/9120-blackbox-version-1-3-6
- ZIP: `blackbox136.zip` (395768 bytes)
- BIN: `firmware/bins/1.3.6/BLACKBOX.BIN` sha256=`2cefc86b6b4b124ea9b77d83b2560fe21be3050b2a2596a24e77ef86d08e29e9` size=591552

Version 1.3.6 includes the following bug fixes:When using MIDI sync with a changing tempo, the blackbox was slow to catch up. It would sometimes take 5-10 seconds for sync to resume.
The Beat Synchronized delay would not update following a tempo change.
Program Change messages were only accepted on the PADS MIDI channel.
Download Blackbox 1.3.6

## 1.4.0

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/9914-blackbox-1-4-0
- ZIP: `blackbox140.zip` (403206 bytes)
- Note: Forum link pointed at superseded 1.4.1 zip (404); recovered blackbox140.zip from 2020/01 path
- BIN: `firmware/bins/1.4.0/BLACKBOX.BIN` sha256=`1dc46e3170b0904474daaa39d0b0595c62de3899c0501f3df74f064d47fc3fd9` size=602884

In preparation for NAMM, we are pleased to make this new version available to you. Here's whats new:Enhanced slicer mode with the ability to play into adjacent slices, quantize, choke, and sync with the master clock. One way to think of this is as random access clip mode.
Grid based keyboard with scale control. Press KEYS a second time to see it.
Alternative grid keyboard for slicer cells that lets you trigger slices more accurately
Swipe to play adjacent keys in the original and new grid KEYS mode.
The load screen now displays directories first followed by files
You can now choose which MIDI note will trigger a pad on the MIDI pads channel
Bug fixes:The system would offer no warning when the micro SD card is running out of space. (You will see red text in the header when there is less than 256MB. Look for the available space on the Tools page.)
When using the MIDI keys feature to route incoming notes to a specific cell, notes would hang if you switched cells while playing
The file system could not handle directories with a period in the name
When using the slice sequencer, the cell would not begin with the first slice after loading a preset.

[Version 1.4.0 has been superseded by version 1.4.3]
https://forum.1010music.com/forum/ta...blackbox-1-4-3

## 1.4.3

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/10877-blackbox-1-4-3
- ZIP: `blackbox143.zip` (404138 bytes)
- BIN: `firmware/bins/1.4.3/BLACKBOX.BIN` sha256=`fbf0b397f9cfcf41b64094b96a06072b7c4c1e44ea710838227776ea66af8768` size=604340

Version 1.4.3 fixes some newly introduced issues from 1.4:Some WAV files would not play (specifically those with uncommon WAVEFORMATEXTENSIBLE tags)
It was not possible to load files from directories with a period in the name

Download blackbox 1.4.3

## 1.5.1

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/11797-blackbox-1-5-release-version
- ZIP: `blackbox151.zip` (355780 bytes)
- BIN: `firmware/bins/1.5.1/BLACKBOX.BIN` sha256=`49c94fe819a67467b088c9cf3f95f78b0a06991c70d8dbbf0c6219074ccdf636` size=558940

We are pleased to make available a new update to blackbox that reorganizes presets based on your input. Version 1.5 includes these features:Presets are now stored in sub folders under the \Presets path. For example: \Presets\DrumKit\, \Presets\Piano\, etc.
Recordings are now captured in the current preset folder instead of the root directory
There is a new Pack command that will copy all the files used by a preset into the current preset folder
There is a new Clean command that will delete any unused files in the current preset folder
In summary, you can now easily consolidate your presets into folders for archival and copying. We agree that this is a better way to work.
PLEASE MAKE A BACKUP OF YOUR MICRO SD CARD BEFORE TRYING THIS NEW VERSION

We have done several extra rounds of testing with this new version to be extra careful with your precious data. However, the possibility remains that something bad may happen in the migration process. Please make a backup.

Download blackbox 1.5.1

Since this is a complex topic, we have also prepared a short user guide to help.

Thank you for your support.

## 1.6.5

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/15617-blackbox-1-6-release-version
- ZIP: `blackbox165.zip` (369428 bytes)
- BIN: `firmware/bins/1.6.5/BLACKBOX.BIN` sha256=`22f058a76810c42c965f45854bf5a6fe9c6969cdb8821a54aef09479063c19de` size=581780

Blackbox 1.6 has been a long time in the making. It is the debut of a number of new features, including the following:Automated mutlisample recording. Use the internal wizard to sample a MIDI or CV+Gate synth. Please note that the feature will allow you to record up to 127 notes by 16 velocity layers. The firmware will only support 64 samples per pad--so choose wisely.
Loop crossfading
Shared voice pool for sample mode provides up to 16 voices per pad
Improved micro SD streaming for up to 16 streamed pads at once
The ability to delete a directory and well as a file
Lots of bug fixes
Thank you for your support. We hope you enjoy the new version.

(updated link below)

## 1.7.4

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/19305-blackbox-1-7
- ZIP: `blackbox174.zip` (383828 bytes)
- BIN: `firmware/bins/1.7.4/BLACKBOX.BIN` sha256=`d79fd5cba351c4d7d20d4b7d89cd1371e6d1b7cb40fdd67d6f63342a78c8624d` size=611068

We are pleased to make some new features available to you:

New in version 1.7.0:Off grid recording and editing. Look for the Step Mode parameter on the SEQ info page. With Step Mode = OFF notes will no longer be quantized to the grid.
SEQ note probability control
SEQ velocity Editing
Choice of clock divisions (PPQ) on input and output. Look in the TOOLS section
New polyphony modes (Mono, Poly 2, Poly 4, Poly 6, Poly 8 and Poly X). Dial in the polyphony per pad to optimize your use of the box
Faster micro SD interface. In the past when we tried to speed this up, some users had problems booting. Please let us know how it works this time.
Bugs fixed:Sometimes the filter cutoff is not correct on new voices
Retriggering the same note would cut of the previous note at the same pitch. Notes will now retrigger, subject to the polyphony mode
Program change messages will crash the box on the MIXER screen
Some USB devices crash the box (introduced in 1.6.6)
(download a newer version below)

## 1.7.F

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/19305-blackbox-1-7
- ZIP: `blackbox17f.zip` (404023 bytes)
- BIN: `firmware/bins/1.7.F/BLACKBOX.BIN` sha256=`dbe18d8ed7cdbb954ade80070b3e958aa17fd44b07e70368b456eedaa5a8f9ba` size=633148

We are pleased to make some new features available to you:

New in version 1.7.0:Off grid recording and editing. Look for the Step Mode parameter on the SEQ info page. With Step Mode = OFF notes will no longer be quantized to the grid.
SEQ note probability control
SEQ velocity Editing
Choice of clock divisions (PPQ) on input and output. Look in the TOOLS section
New polyphony modes (Mono, Poly 2, Poly 4, Poly 6, Poly 8 and Poly X). Dial in the polyphony per pad to optimize your use of the box
Faster micro SD interface. In the past when we tried to speed this up, some users had problems booting. Please let us know how it works this time.
Bugs fixed:Sometimes the filter cutoff is not correct on new voices
Retriggering the same note would cut of the previous note at the same pitch. Notes will now retrigger, subject to the polyphony mode
Program change messages will crash the box on the MIXER screen
Some USB devices crash the box (introduced in 1.6.6)
(download a newer version below)

## 2.0.E

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/28352-blackbox-2-1-5-and-2-1-5l
- ZIP: `BLACKBOX20E.zip` (434123 bytes)
- Note: Linked from 2.1.5 thread as prior version 2.0.E
- BIN: `firmware/bins/2.0.E/BLACKBOX.bin` sha256=`8598ff628591dc83575372433804f4dde09a254fe579b303e9bd289da7b2ab8d` size=666348

Version 2.0 is now available. It is the same functionality as 1.9 with lots of bug fixes. In case you missed it, the new features include:Tap Tempo. Be sure to look for the small tempo button in the upper left of the SONG page. Press this button at least three times to change the current tempo
LFO per voice with the option for beat sync. This applies to sample, clip, granular, and slicer modes
4-band Parametric EQ on the master output. Press the FX button twice to find it.
Modulation of the envelope Attack, Decay, and Release, including control via MIDI CC
Resonance control on the filter, including the ability to modulate it
Enhanced Delay algorithm with integrated band-pass filter and the ability to play just a single echo
More advanced granular controls, including density, scatter, and pan spread
MIDI-only sequences. Choose MIDI as the cell type in the upper left and the sequence will not play internal pads.
MIDI based recording of cells. Be sure to switch MIDI Rec to ON in the TOOLS/Rec page. Here is the layout for notes received on the MIDI Pads channel(s) :Notes 68-83: Record pads 1-16. The pad needs to be empty in order for recording to happen.
Notes 84-99: Clear pads 1-16
Note 100: Select the previous pad
Note 101: Select the next pad
Note 102: Play the currently selected pad
Note 103: Record the currently selected pad assuming it is empty
Note 104: Clear the currently selected pad

New, more convenient layouts with onscreen knobs and fewer screens
Thank you for your support.

## 2.1.5

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/28352-blackbox-2-1-5-and-2-1-5l
- ZIP: `BLACKBOX215.zip` (427274 bytes)
- BIN: `firmware/bins/2.1.5/BLACKBOX.bin` sha256=`2bd6eb247a2e1074537049c40e39d2add01871ea3bc58500a95268b516d66779` size=672164

Version 2.0 is now available. It is the same functionality as 1.9 with lots of bug fixes. In case you missed it, the new features include:Tap Tempo. Be sure to look for the small tempo button in the upper left of the SONG page. Press this button at least three times to change the current tempo
LFO per voice with the option for beat sync. This applies to sample, clip, granular, and slicer modes
4-band Parametric EQ on the master output. Press the FX button twice to find it.
Modulation of the envelope Attack, Decay, and Release, including control via MIDI CC
Resonance control on the filter, including the ability to modulate it
Enhanced Delay algorithm with integrated band-pass filter and the ability to play just a single echo
More advanced granular controls, including density, scatter, and pan spread
MIDI-only sequences. Choose MIDI as the cell type in the upper left and the sequence will not play internal pads.
MIDI based recording of cells. Be sure to switch MIDI Rec to ON in the TOOLS/Rec page. Here is the layout for notes received on the MIDI Pads channel(s) :Notes 68-83: Record pads 1-16. The pad needs to be empty in order for recording to happen.
Notes 84-99: Clear pads 1-16
Note 100: Select the previous pad
Note 101: Select the next pad
Note 102: Play the currently selected pad
Note 103: Record the currently selected pad assuming it is empty
Note 104: Clear the currently selected pad

New, more convenient layouts with onscreen knobs and fewer screens
Thank you for your support.

## 2.1.5L

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/28352-blackbox-2-1-5-and-2-1-5l
- ZIP: `BLACKBOX215L.zip` (438347 bytes)
- Note: Legacy/compatibility build alongside 2.1.5
- BIN: `firmware/bins/2.1.5L/BLACKBOX.bin` sha256=`488ad3fbdaa83c93c4067f99ea91bc74cfc60f56bc62c6dbe190c0df71c35610` size=671740

Version 2.0 is now available. It is the same functionality as 1.9 with lots of bug fixes. In case you missed it, the new features include:Tap Tempo. Be sure to look for the small tempo button in the upper left of the SONG page. Press this button at least three times to change the current tempo
LFO per voice with the option for beat sync. This applies to sample, clip, granular, and slicer modes
4-band Parametric EQ on the master output. Press the FX button twice to find it.
Modulation of the envelope Attack, Decay, and Release, including control via MIDI CC
Resonance control on the filter, including the ability to modulate it
Enhanced Delay algorithm with integrated band-pass filter and the ability to play just a single echo
More advanced granular controls, including density, scatter, and pan spread
MIDI-only sequences. Choose MIDI as the cell type in the upper left and the sequence will not play internal pads.
MIDI based recording of cells. Be sure to switch MIDI Rec to ON in the TOOLS/Rec page. Here is the layout for notes received on the MIDI Pads channel(s) :Notes 68-83: Record pads 1-16. The pad needs to be empty in order for recording to happen.
Notes 84-99: Clear pads 1-16
Note 100: Select the previous pad
Note 101: Select the next pad
Note 102: Play the currently selected pad
Note 103: Record the currently selected pad assuming it is empty
Note 104: Clear the currently selected pad

New, more convenient layouts with onscreen knobs and fewer screens
Thank you for your support.

## 2.9.1-beta

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/39025-blackbox-3-0-beta
- ZIP: `BLACKBOX291.zip` (445586 bytes)
- Note: Early 3.0 beta build (BLACKBOX291) from 3.0 beta thread
- BIN: `firmware/bins/2.9.1-beta/BLACKBOX.bin` sha256=`75c2acad1af14e277d835df2c6fda61ca0f7d3e9db0299f5cb17588ea74576cd` size=684428

We are pleased to make available a preview version of blackbox 3.0. The focus of this release is on Song mode and sequencer workflow. We hope this update represents a big step forward. Please let us know.

Please note that this new version is only partially compatible with existing presets--please make a back up of your work for safe keeping. Please consider this version a beta for evaluation only. We do not believe this version is ready for a performance. Here are some of the included features:

Reimagined Song Mode:

We started fresh with song mode in a effort to make it much more straightforward and useful. Each song now consists of up to 16 scenes that can launch any available clips and sequences. You get a visual snapshot of which items will launch as well as the ability to edit them. You can create scenes manually or use the snapshot feature to grab what is currently playing. You can play the scenes from start to finish or launch them manually in any order you like. You can also loop scenes or preprogram them to loop a specific number of times. Finally, the keep feature lets you update some clips and sequences in a scene without impacting others that are already playing.

Quadruple the number of sequences:

We have greatly improved the functionality of each sequence cell. Each one now has 4 layers for making separate parts and switching between them in a quantized manner. From the main SEQ screen turn the lower right knob to see the ABCD toolbar. The new song mode can launch sequences along with the specified layer. In this way you can chain patterns and create complex arrangements.

True MIDI sequences:

Each MIDI sequence has four layers and can record notes from the touch screen or external MIDI. All of this can be done independently of the existing pads. Look for the SEQ mode on the KEYS screen. Also check out the MIDI Seq input channel on TOOLS - MIDI In. Both these options give you a direct way to play and record notes using external MIDI devices.

Here are some quick tips on things that have changed and how to work in the new model:MIDI Input. There are three ways in the door:TOOLS - MIDI In - MIDI Pads channel. Use this channel for controlling blackbox overall, including pads and various options, like pad recording, etc.
PAD MIDI In. Use this per PAD connection to play a pad melodically via MIDI
TOOLS - MIDI In - MIDI Seq channel. This new method connects inbound MIDI directly to the currently selected sequence. Use it for playing and recording pitched sequences internally.

MIDI Output. This is now handled entirely from SEQ cells. Each one can have its own individual MIDI output channel. The 4 layers per sequence will all use the same MIDI output channel.
SEQS in KEYS mode. Each sequence cell in KEYS mode now maps to a single pad, played chromatically. This simplifies recording and routing in that you specify where the notes will go for all 4 layers. You can record into a KEYS sequence from the MIDI Seq channel and from the KEYS screen when in SEQ mode.

At this point we really want to know what you think. Have we solved the various sequencing traps that plagued the previous versions? Have we missed anything important? Does this new version help you be more creative?

Please let us know here.​

Read the preliminary documentation here.

## 3.0.1

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/39025-blackbox-3-0-beta
- ZIP: `BLACKBOX301.zip` (448760 bytes)
- Note: Posted in blackbox 3.0 beta thread
- BIN: `firmware/bins/3.0.1/BLACKBOX.bin` sha256=`11f6d80926b7d100e9aa1b7970c0eec219510030fa0f8e2e6ffc8aa4b5209c07` size=689404

We are pleased to make available a preview version of blackbox 3.0. The focus of this release is on Song mode and sequencer workflow. We hope this update represents a big step forward. Please let us know.

Please note that this new version is only partially compatible with existing presets--please make a back up of your work for safe keeping. Please consider this version a beta for evaluation only. We do not believe this version is ready for a performance. Here are some of the included features:

Reimagined Song Mode:

We started fresh with song mode in a effort to make it much more straightforward and useful. Each song now consists of up to 16 scenes that can launch any available clips and sequences. You get a visual snapshot of which items will launch as well as the ability to edit them. You can create scenes manually or use the snapshot feature to grab what is currently playing. You can play the scenes from start to finish or launch them manually in any order you like. You can also loop scenes or preprogram them to loop a specific number of times. Finally, the keep feature lets you update some clips and sequences in a scene without impacting others that are already playing.

Quadruple the number of sequences:

We have greatly improved the functionality of each sequence cell. Each one now has 4 layers for making separate parts and switching between them in a quantized manner. From the main SEQ screen turn the lower right knob to see the ABCD toolbar. The new song mode can launch sequences along with the specified layer. In this way you can chain patterns and create complex arrangements.

True MIDI sequences:

Each MIDI sequence has four layers and can record notes from the touch screen or external MIDI. All of this can be done independently of the existing pads. Look for the SEQ mode on the KEYS screen. Also check out the MIDI Seq input channel on TOOLS - MIDI In. Both these options give you a direct way to play and record notes using external MIDI devices.

Here are some quick tips on things that have changed and how to work in the new model:MIDI Input. There are three ways in the door:TOOLS - MIDI In - MIDI Pads channel. Use this channel for controlling blackbox overall, including pads and various options, like pad recording, etc.
PAD MIDI In. Use this per PAD connection to play a pad melodically via MIDI
TOOLS - MIDI In - MIDI Seq channel. This new method connects inbound MIDI directly to the currently selected sequence. Use it for playing and recording pitched sequences internally.

MIDI Output. This is now handled entirely from SEQ cells. Each one can have its own individual MIDI output channel. The 4 layers per sequence will all use the same MIDI output channel.
SEQS in KEYS mode. Each sequence cell in KEYS mode now maps to a single pad, played chromatically. This simplifies recording and routing in that you specify where the notes will go for all 4 layers. You can record into a KEYS sequence from the MIDI Seq channel and from the KEYS screen when in SEQ mode.

At this point we really want to know what you think. Have we solved the various sequencing traps that plagued the previous versions? Have we missed anything important? Does this new version help you be more creative?

Please let us know here.​

Read the preliminary documentation here.

## 3.0.9

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/42426-blackbox-3-0-9-release-version
- ZIP: `BLACKBOX309.zip` (449436 bytes)
- BIN: `firmware/bins/3.0.9/BLACKBOX.bin` sha256=`e68b5345acf9b752fc36ca4d98d480c499bea15b1bba1a6babe340a01f1627e5` size=691452

Dear all,

Thanks for your feedback. Version 3.0.7 is now considered a stable release.

Have any 3.0.7 feedback? please let us know here.

We prepared a document outlining everything in 3.0 compared to 2.1.5. You can read that here.

There are a number of changes in this build compared to 3.0.2. Here are some quick highlights:Based on popular demand, the global MIDI Seq channel is back. We heard you say this is an easier way to work.
USB MIDI output is also back--with some special options to help with routing and troubleshooting.
USB MIDI input was unreliable and would sometimes drop notes.
Sequence data was lost when switching between KEYS and MIDI modes
The currently active sequence pattern (A-D) is forgotten when reloading a preset.
We appreciate your support and can't wait to hear what you make with the blackbox.

The 1010music team

## 3.0.15-beta

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/45151-blackbox-3-0-15-beta
- ZIP: `BLACKBOX3015.zip` (452908 bytes)
- BIN: `firmware/bins/3.0.15-beta/BLACKBOX.bin` sha256=`0518b3edae53a7b4a35d1746d007059585ba94a0593a0697c277927c8e9c5fc4` size=695968

Dear all,

Based on popular demand, we have changed how sample recording works regarding the auto creation of files. As of 3.0.10, the rules are now as follows:When threshold based recording is active (RecThres: ON), you will create a Sample by default.
When record quantization (Rec Quant) is anything but none, you will always record a clip and the length in beats will be embedded in the WAV file for future use.
You can now create clips that are non-power of two length. Use recording quantization as desired and Length: Custom to capture. The result will default to Clip Mode and automatic playback (RecToPlay) will work as expected.

## 3.1.2

- Source: https://forum.1010music.com/forum/tabletop-instruments/firmware-downloads-aa/blackbox-firmware-downloads/49019-blackbox-3-1-2-release-version
- ZIP: `BLACKBOX312.zip` (452944 bytes)
- BIN: `firmware/bins/3.1.2/BLACKBOX.bin` sha256=`caf5bfd9e76ce84891e2532fc69e34142b70b53a061119f3d9da0bfe4f09ddc8` size=695920

blackbox 3.1 is now available. Here is what's new compared to version 3.0. (These new features already appeared in beta version 3.0.15)Supports import of multisample sets using the filename to set the root note and velocity
Sample pool is now 576 samples instead of 80 samples
Loads loop points embedded into multisample sets. A great source for this kind of material is Samples From Mars.
Sleep mode: Hold the BACK button to put the unit to sleep
When threshold based recording is active (RecThres: ON), you will create a Sample by default.
When record quantization (Rec Quant) is anything but none, you will always record a clip and the length in beats will be embedded in the WAV file for future use.
You can now create clips that are non-power of two length. Use recording quantization as desired and Length: Custom to capture. The result will default to Clip Mode and automatic playback (RecToPlay) will work as expected.
Version 3.1.2 includes a couple of bug fixes over 3.0.15:Importing sets with velocity layers would not play correctly.
Please let us know what you think in this thread.

Download blackbox 3.1.2

## 3.1.9

- Source: https://1010music.com/downloads
- ZIP: `blackbox-3.1.9.zip` (477305 bytes)
- BIN: `firmware/bins/3.1.9/BLACKBOX.bin` sha256=`281ae303d32e5eb52adca7817a648a34e4bd3848017f26673dd9df3905f1341d` size=728696

Here is what's new compared to version 3.0.

- DJ FX with XY control including repeater, gater, echo, bitcrusher, and flanger
- Per pad overdrive algorithm
- Multiselect and editing in the piano roll sequence editor
- Supports import of multisample sets using the filename to set the root note and velocity
- Sample pool is now 576 samples instead of 80 samples
- Loads loop points embedded into multisample sets. A great source for this kind of material is Samples From Mars.
- Sleep mode: Hold the BACK button to put the unit to sleep
- When threshold based recording is active (RecThres: ON), you will create a Sample by default.
- When record quantization (Rec Quant) is anything but none, you will always record a clip and the length in beats will be embedded in the WAV file for future use.
- You can now create clips that are non-power of two length. Use recording quantization as desired and Length: Custom to capture. The result will default to Clip Mode and automatic playback (RecToPlay) will work as expected.
- Importing sets with velocity layers would not play correctly.


