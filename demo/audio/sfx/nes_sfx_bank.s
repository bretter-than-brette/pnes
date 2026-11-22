; Places the FamiStudio sound-effect export into the audio DATA bank.
;
; sfx.s is FamiStudio own export, regenerated every build, and carries no
; .segment of its own -- so without this wrapper its data lands in ca65 default
; CODE segment and is merged into ordinary .text.

.segment "_pprg__rom__sfx"
.include "sfx.s"
