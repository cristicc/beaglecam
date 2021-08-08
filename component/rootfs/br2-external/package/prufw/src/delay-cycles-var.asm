;
; Alternative to __delay_cycles() intrinsic allowing for a variable
; expression to be provided as argument.
;
; Note function input arguments are stored in R14..R29. For more details,
; refer to "PRU Optimizing C/C+ Compiler User's Guide" document, section
; "Function Structure and Calling Conventions".
;
; Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
;

;
; void delay_cycles_var(uint32_t n);
;
	.sect	".text:delay_cycles_var"
	.clink
	.global	||delay_cycles_var||

||delay_cycles_var||:
	; arg1 is in R14
$1:
	sub		r14, r14, 1
	qbne	$1, r14, 0

	; Return from function
	jmp		r3.w2
