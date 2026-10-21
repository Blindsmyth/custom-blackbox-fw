@ 3.1.Q = 3.1.X (folders + Warp Stretch/Repitch) + Mix overhaul. Built with -I ../3.1.9-folders-repitch.
.set MIX_EXT, 1                         @ repitch.S: Env Trig hooks (adsr.S)
.ifndef WARP_RUBBER
.set WARP_PLAIN, 1                      @ repitch.S, cave.S: Stretch / Repitch only, no 3.1.Z engines or slices
.endif
.include "cave.S"

.align 2
.ltorg

.include "mixui.S"

.align 2
.ltorg

.include "navknobs.S"

.align 2
.ltorg

.include "padsel.S"

.align 2
.ltorg

.include "seqtools.S"

.align 2
.ltorg

.include "eqtouch.S"

.align 2
.ltorg

.include "fxret.S"

.align 2
.ltorg

.include "seqrec.S"

.align 2
.ltorg

.include "looper.S"

.align 2
.ltorg

.include "mixmute.S"

.align 2
.ltorg

.include "master.S"

.align 2
.ltorg

.include "adsr.S"

.align 2
.ltorg

.include "pset.S"
