This is a repo for test documents related to Euphoria, an experimental instrument in development. 

Requires an updated version of PureData with the ELSE library (or replace Plaits~ for another sound source)
Plaits~ written by Émilie Guillet
 
To run, open the TouchOSC template or other MIDI controller. 
Find the appropriate notes and CC numbers in the patch

TODO: add the list for easier reference. 

Chords.pd is functional 
chordsVL.pd is in development to implement voice leading between current and chosen chords.

Experimental support for [Dualo Exquis controller](https://dualo.com/en/exquis/) :
1. Place EuphoriaChords.xqilayout in Exquis/layouts folder 
2. Load the template using the Exquis app. Make sure to add it in Standalone Settings too. 
3. Close the Exquis app and enter standalone mode. Load the template by pressing the Settings (2) button and using encoder 3 to scroll through the templates.
4. Set the exquis in Polyphonic expression mode by pressing the Settings (2) and clicking encoder #1 to set the LED yellow. Make sure PolyEx is set to channel 1.
5. Launch PD patch.

Installation in Zynthian:
1. Copy the contents of the EuphoriumExquis subfolder to /zynthian/zynthian-my-data/presets/puredata/
2. Add Special Chain/PureData - Visual Programming. Set MIDI CHANNEL 1 
3. Load EuphoriumExquis Patch
4. The PD patch can be accessed at (http://zynthian.local:6081/vnc.html)
5. This version uses a simple synth compatible with PD vanilla; this patch can be swapped for others by editing vanillaSynth.pd
