; Places the exported FamiStudio song data into its own PRG-ROM bank.
;
; nes_audio.s is regenerated every build and carries no .segment of its own, so
; without this wrapper its data lands in ca65 default CODE segment.

.segment "_pprg__rom__music"
.include "nes_audio.s"
