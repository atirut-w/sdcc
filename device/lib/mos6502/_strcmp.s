;-------------------------------------------------------------------------
;   _strcmp.s - standard C library function
;
;   Copyright (C) 1998, Ullrich von Bassewitz
;   Copyright (C) 2022, Gabriele Gorla
;
;   This library is free software; you can redistribute it and/or modify it
;   under the terms of the GNU General Public License as published by the
;   Free Software Foundation; either version 2, or (at your option) any
;   later version.
;
;   This library is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;   GNU General Public License for more details.
;
;   You should have received a copy of the GNU General Public License
;   along with this library; see the file COPYING. If not, write to the
;   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;   MA 02110-1301, USA.
;
;   As a special exception, if you link this library with other files,
;   some of which are compiled with SDCC, to produce an executable,
;   this library does not by itself cause the resulting executable to
;   be covered by the GNU General Public License. This exception does
;   not however invalidate any other reasons why the executable file
;   might be covered by the GNU General Public License.
;-------------------------------------------------------------------------

	.module _strcmp

;--------------------------------------------------------
; exported symbols
;--------------------------------------------------------
	.globl _strcmp_PARM_2
	.globl _strcmp

;--------------------------------------------------------
; overlayable items in zero page
;--------------------------------------------------------
	.area	OSEG    (PAG, OVR)
_str1:
	.ds 2
_str2:
	.ds 2


;--------------------------------------------------------
; function parameters
;--------------------------------------------------------
	.area XSEG
_strcmp_PARM_2:
	.ds 2

;--------------------------------------------------------
; code
;--------------------------------------------------------
	.area CSEG

_strcmp:
	sta	*_str1+0
	stx	*_str1+1
	lda	_strcmp_PARM_2+0
	sta	*_str2+0
	ldx	_strcmp_PARM_2+1
	stx	*_str2+1

	ldy	#0
loop:
	lda	[*_str1],y
	cmp	[*_str2],y
	bne	L1
	tax
	beq	end
	iny
	bne	loop
	inc	*_str1+1
	inc	*_str2+1
	bne	loop
L1:
	bcs	L2
	ldx	#0xFF
	txa
	rts
L2:
	ldx	#0x01
	txa
end:
	rts
