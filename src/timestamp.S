.global sysreg_read_cntpct
sysreg_read_cntpct:
	isb
	mrrc p15, 0, r0, r1, c14
	bx lr
