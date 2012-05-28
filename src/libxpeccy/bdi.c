// NOTE: FF is BDI-port, but 1F,3F,5F,7F belongs to FDC (VG93)

#include "bdi.h"

#include <stdlib.h>
#include <stdio.h>

#define	BYTEDELAY	224
#define IDXDELAY	600
#define	MSDELAY		7160		// 1ms delay
#define	TRBDELAY	MSDELAY * 3

int pcatch = 0;	// 1 when bdi takes i/o request

// FDC

FDC* fdcCreate(int tp) {
	FDC* fdc = (FDC*)malloc(sizeof(FDC));
	fdc->type = tp;
	fdc->t = 0;
	fdc->tf = 0;
	fdc->wptr = NULL;
	fdc->count = 0;
	fdc->idle = 1;
	fdc->status = FDC_IDLE;
	return fdc;
}

// VG93

// THIS IS BYTECODE FOR VG93 COMMANDS EXECUTION

/*
01,n	jump to work n

10	pause (com & 3)
11,n	pause (n)

20,n	set flag mode n

30	drq = 0
31	drq = 1

80,n	Rtr = n
81	Rtr--
82	Rtr++
83,n,d	if (flp::trk == n) jr d
84,n,d	if (Rtr == n) jr d
85,d	if (Rtr == Atr) jr d
86,d	if (Rtr == Rdat) jr d
87,d	if (Rtr < Rdat) jr d
88	Rsec = Rdat
89,d	if (Rtr != Rdat) jr d
8a,d	if (Rsec != Rdat) jr d
8b,d	if (side != Rdat) jr d
8c,n,d	if (Rdat == n) jr d
8d	Rsec++
8e,n,d	if (Rbus == n) jr d
8f,n	Rbus = n

90,n	read byte from floppy in buf[n]
91	read byte in Rdat
92	read byte in Rbus
93	write from Rdat
94	write from Rbus

A0	step in
A1	step out
A2	step (prev)

A8	next byte (seek)
A9	next byte (rd/wr)
AA	change floppy side

B0,n	icnt = n
B1	icnt--
B2	icnt++
B3,n,d	if (icnt==n) jr d
B4,n,d	if (icnt!=n) jr d
B5	ic = buf[0]:128,256,512,1024

C0,a,b	flag & a | b
C1	fill fields @ curr.flp.trk

D0	init CRC
D1,n	add byte n to CRC
D2	add Rdat to CRC (if field != 0)
D3	add Rbus to CRC (if field != 0)
D4	read flp.CRC (full)
D5,d	if (CRC == flp.CRC) jr d
D7	read fcrc.hi
D8	read fcrc.low

E0,n	n: 0 - stop motor/unload head; 1 - start motor/load head

F0,d	jr d
F8,d	if (flp.wprt) jr d
F9,d	if (flp.ready (insert)) jr d
FA,d	if (sdir = out (1)) jr d
FB,n,d	if (flp.field = n) jr d
FC,d	if (index) jr d
FD,n,d	if (com & n == 0) jr d
FE,n,d	if (com & n != 0) jr d
FF	end
*/

uint8_t vgidle[256] = {
	0xb0,15,
	0xa8,
	0xfc,2,
	0xf0,-5,
	0xb1,
	0xb4,0,-9,
	0xe0,0,
	0xf1
};

uint8_t vgwork[16][256] = {
// 0: restore
	{
		0x20,0,			// flag mode 0
		0xfd,8,2,0xe0,1,	// if h=0 delay 50 mks
		0x80,255,		// Rtr = 255
		0x83,0,8,		// if flp.trk==0 jr +8 (@ 80)
		0x84,0,5,		// if Rtr==0 jr +5 (@ 80)
		0x10,			// pause (com & 3)
		0xa0,			// step in
		0x81,			// Rtr--
		0xf0,-11,		// jr -11 (@ 83)
		0x80,0,			// Rtr = 0
		0x01,12			// WORK 12 (check head pos)
	},
// 1: seek
	{
		0x20,0,			// flag mode 0
		0xfd,8,2,0xe0,1,
		0x86,12,		// if Rtr = Rdat jr +d (@ 01)
		0x87,5,			// if Rtr < Rdat jr +d (@ 82)
		0x10,			// pause (com & 3)
		0x81,			// Rtr--
		0xa0,			// step in
		0xf0,-9,		// jr -9 (@ 86)
		0x10,			// pause (com & 3)
		0x82,			// Rtr++
		0xa1,			// step out
		0xf0,-7,		// jr -7 (@ jr @ 86)
		0x01,12			// WORK 12
	},
// 2: step
	{
		0xfa,2,			// if sdir = out, jr +2 (jp work 4)
		0x01,3,			// WORK 3 (step in)
		0x01,4			// WORK 4 (step out)
	},
// 3: step in
	{
		0x20,0,			// flag mode 0
		0xfd,8,2,0xe0,1,
		0xfd,16,1,0x82,		// if (b4,com) Rtr++
		0xa1,			// step out
		0x10,			// pause (com & 3)
		0x01,12			// WORK 12
	},
// 4: step out
	{
		0x20,0,			// flag mode 0
		0xfd,8,2,0xe0,1,
		0xfd,16,1,0x81,		// if (b4,com) Rtr--
		0x83,0,4,		// if (flp.tr0) jr +4 (@ 80)
		0xa0,			// step in
		0x10,			// pause (com & 3)
		0x01,12,		// WORK 12
		0x80,0,			// Rtr = 0
		0x01,12			// WORK 12
	},
// 5: read sector
	{
		0x20,1,			// flag mode 1
		0xc0,0x9f,0x00,		// reset b 5,6 in flag
		0xf9,1,0xff,		// ifn flp.ready END
		0xfd,4,2,0x11,4,	// if (b2,com) wait (15ms)
		0xe0,1,
		0x01,14			// WORK 14 (ХИТРОСТЬ DESU)
	},
// 6: write sector
	{
		0x20,1,			// flag mode 1
		0xc0,0x83,0x00,		// res b2-6 in flag
		0xf9,1,0xff,		// ifn flp.ready END
		0xfd,4,2,0x11,4,	// if (b2,com) wait 15ms
		0xe0,1,
		0xf8,2,			// if write protect jr +2
		0x01,15,		// WORK 15
		0xc0,0xbf,0x40,		// set b6 (write protect)
		0xff			// end
	},
// 7: read address
	{
		0x20,1,			// flag mode 1
		0xc0,0x83,0x00,		// reset b 2-6 in flag
		0xf9,1,0xff,		// ifn flp.ready END
		0xfd,4,2,0x11,4,	// if (b2,com) wait (15ms)
		0xe0,1,
		0xb0,6,			// ic = 6
		0x02,13,		// CALL WORK 13 (wait for address field)
		0xd0,0xd1,0xfe,		// init CRC, CRC << 0xfe (address marker)
		0x91,0xd2,0x88,0x31,0xa9,	// read byte, add to crc, Rsec = byte, drq=1, next(slow)
		0x91,0xd2,0x31,0xa9,	// read byte, add to crc, drq=1, next(slow)
		0x91,0xd2,0x31,0xa9,
		0x91,0xd2,0x31,0xa9,
		0x91,0xd7,0x31,0xa9,	// read byte, read fcrc.low, drq=1, next(slow)
		0x91,0xd8,0x31,0xa9,	// read byte, read fcrc.hi, drq=1, next(slow)
		0xc0,0xf7,0x00,		// reset CRC ERROR
		0xd5,3,			// if crc == fcrc, jr +3 (end)
		0xc0,0xf7,0x08,		// set CRC ERROR (08)
		0xff			// END
	},
// 8: read track
	{
		0x20,1,			// set flag mode 1
		0xc0,0x83,0x00,		// reset b 2-6 in flag
		0xf9,1,0xff,		// ifn flp.ready END
		0xfd,4,2,0x11,4,	// if (b2,com) wait 15ms
		0xe0,1,
		0x30,			// drq = 0
		0xfc,3,0xa8,0xf0,-5,	// wait for STRB
		0x32,5,			// if (drq=0) jr +3
		0xc0,0xfb,0x04,0xf0,2,	// DATA LOST
		0x91,0x31,		// read byte in Rdat, drq=1
		0xa9,			// next (fast)
		0xfc,2,0xf0,-14,	// ifn STRB jr -14 (@ 32)
		0xff			// END
	},
// 9: write track
	{
		0x20,1,			// set flag mode 1
		0xc0,0x83,0x00,		// res b2-6 in flag
		0xf9,1,0xff,		// ifn flp.ready END
		0xfd,4,2,0x11,4,	// if (b2,com) wait 15ms
		0xe0,1,
		0xf8,2,			// if write protect jr +2
		0xf0,4,			// jr +4 (@ 31)
		0xc0,0xbf,0x40,0xff,	// WRITE PROTECT, END
		0x31,			// drq = 1
//		0xa9,0xa9,0xa9,		// wait 3 bytes (slow)
//		0x32,4,			// if (drq == 0) jr +4
//		0xc0,0xfb,0x04,0xff,	// DATA LOST, END
		0xfc,3,0xa9,0xf0,-5,	// wait for STRB (slow)
		0x32,5,			// if (drq == 0) jr +5
		0xc0,0xfb,0x04,		// DATA LOST
		0xf0,2,			// jr +2
		0x93,			// write Rdat
		0x31,			// drq = 1
		0xa9,			// next (slow)
		0xfc,2,			// if STRB jr +2 (@ C1)
		0xf0,-14,		// jr -14 (@ 32)
		0xc1,			// fill fields (change F5,F6,F7)
		0xff
	},
// 10: interrupt (TODO: conditions)
	{
		0xff
	},
// 11: undef
	{
		0xff
	},
// 12: check head pos (end of restore/seek/step commands)
	{
		0xfe,4,1,0xff,		// if (com & 4 == 0) END
		0x11,4,			// pause 15 ms
		0xfb,0,5,		// WAIT FOR STRB or field 0
		0xfc,3,
		0xa8,0xf0,-8,
		0xb0,0,			// ic = 0
		0xfb,1,16,		// if fptr.field = 1 (header) jr +16
		0xa8,			// next (fast)
		0xfc,2,			// if STRB jr +2
		0xf0,-8,		// jr -8 (@ FB)
		0xaa,			// change floppy head (up/down)
		0xb2,			// ic++
		0xb3,9,2,		// if (icnt==9) jr +2
		0xf0,-9,		// jr -9 (@ jr -8 @ FB)
		0xc0,0xef,0x10,0xff,	// SEEK ERROR, END
		0xd0,0xd1,0xfe,		// init CRC, CRC << FE (address marker)
		0x90,0,0xd3,0xa8,	// read 6 bytes in buffer (Rbus = byte, CRC << Rbus)
		0x90,1,0xd3,0xa8,
		0x90,2,0xd3,0xa8,
		0x90,3,0xd3,0xa8,
		0x90,4,0xd7,0xa8,	// d7: read fptr.hi
		0x90,5,0xd8,		// d8: read fptr.low
		0x85,2,0xf0,-36,	// if (Atr != Rtr) jr -28 (@ jr -9 @ jr -8 @ FB)
		0xc0,0xf7,0x00,		// reset CRC ERROR
		0xd5,3,			// if fcrc == crc, jr +3 (end)
		0xc0,0xf7,0x08,		// set CRC ERROR
		0xff			// END
	},
// 13: [CALL] wait for address field, in: ic=X (X IDX during execution = END with SEEK ERROR, or RETURN if succes)
	{
		0xfb,0,5,		// wait for field 0 or IDX
		0xa8,
		0xfc,2,
		0xf0,-8,
		0xfb,1,13,		// if fptr.field = 1 (header) jr +13 (@ 03)
		0xa8,			// next (fast)
		0xfc,2,			// if STRB jr +2
		0xf0,-8,		// jr -8 (@ FB)
		0xb1,			// ic--
		0xb4,0,-6,		// if (ic!=0) jr -6 (@ jr -8 @ FB)
		0xc0,0xef,0x10,0xff,	// SEEK ERROR, END
		0x03			// RET
	},
// 14: sector read [main]
	{
		0xb0,5,			// ic = 5
		0x02,13,		// CALL WORK 13 (wait address)
		0xd0,0xd1,0xfe,		// init CRC, CRC << fe
		0x91,0xd2,0xa8,		// read [trk] in Rdat, add to CRC, next
		0x89,-10,		// if Rtrk != Rdat jr -(@ 02)
		0x91,0xd2,0xa8,		// read [side] in Rdat, add to CRC, next
		0xfd,2,2,		// ifn (b1,com) skip side check
		0x8b,-18,		// if (com & 8) ? Rdat jr -(@ 02)
		0x91,0xd2,0xa8,		// read [sect] in Rdat, add to CRC, next
		0x8a,-23,		// if Rsec != Rdat jr -17 (@ 02)
		0x90,0,0xd3,0xa8,	// read [len] in buf[0] & Rbus, add to CRC (Rbus), next
		0xd7,0xa8,0xd8,0xa8,	// read fcrc
		0xd5,5,			// if fcrc == crc, jr +(read data)
		0xc0,0xf7,0x08,		// set CRC ERROR
		0xf0,-38,		// seek again
		0xc0,0xf7,0x00,		// reset CRC ERROR
		0xb0,22,
		0xa8,0xb1,0xb4,0,-5,	// skip 22 bytes (space)
		0xb0,30,
		0x91,0xa9,		// read byte in Rdat, next (slow)
		0x8c,0xf8,11,		// if Rdat = F8 jr @(set b5,flag)
		0x8c,0xfb,13,		// if Rdat = FB jr @(res b5,flag)
		0xb1,0xb4,0,-12,	// F8 | FB must be in next 30(?) bytes
		0xc0,0xef,0x10,0xff,	// else: ARRAY NOT FOUND, END
		0xc0,0xbf,0x20,		// F8: set b5,flag
		0xf0,3,			// jr +3
		0xc0,0xbf,0x00,		// FB: res b5,flag
//	0,1,
		0xd0,0xd2,		// init crc, CRC << Rdat (F8 or FB)
		0xb5,			// ic = sector len
		0x30,			// drq=0
		0x32,5,			// if (drq=0) jr +5
		0xc0,0xfb,0x04,		// set DATA LOST
		0xf0,3,
		0x91,0xd2,0x31,		// read byte in Rdat, CRC << Rdat, drq=1
		0xa9,			// next (fast)
		0xb1,0xb4,0,-15,	// ic--; if ic!=0 jr -(@ 32)
		0xd7,0xa8,0xd8,0xa8,	// read fptr.crc
		0xd5,4,
		0xc0,0xf7,0x08,0xff,	// set CRC ERROR, END
//	0,0,
		0xfd,16,0xff,		// ifn (bit 4,com) END [multisector]
		0x8d,			// Rsec++
		0x01,14			// back to start (WORK 14)
	},
// 15: sector write [main]
	{
		0xb0,5,			// ic = 5
		0x02,13,		// CALL WORK 13 (wait address)
		0xd0,0xd1,0xfe,		// init CRC, CRC << fe (address marker)
		0x91,0xd2,0xa8,		// read [trk] in Rdat, add to CRC, next
		0x89,-10,		// if Rtrk != Rdat jr -6 (@ 02)
		0x91,0xd2,0xa8,		// read [side] in Rdat, add yo CRC, next
		0xfd,2,2,		// ifn (b1,com) skip side check
		0x8b,-18,		// if (com & 8) ? Rdat jr -(@ 02)
		0x91,0xd2,0xa8,		// read [sect] in Rdat, add to CRC, next
		0x8a,-23,		// if Rsec != Rdat jr -(@ 02)
		0x90,0,0xd3,0xa8,	// read [len] in buf[0] & Rbus, add Rbus to CRC, next
		0xd7,0xa8,0xd8,0xa8,	// read fcrc
		0xd5,5,			// if fcrc == crc, jr @(write data)
		0xc0,0xf7,0x08,		// set CRC ERROR
		0xf0,-38,		// seek again
		0xa8,0xa8,		// skip 2 bytes
		0x31,			// drq = 1	(in next 8 bytes Rdat must be loaded)
		0xa9,0xa9,0xa9,0xa9,	// skip 8 bytes (slow)
		0xa9,0xa9,0xa9,0xa9,
		0x32,4,			// if (drq == 0) jr +4
		0xc0,0xfb,0x04,0xff,	// DATA LOST, END
		0xb0,45,		// ic = 45 (wait for F8/FB in next 45 bytes)
		0x92,			// read in Rbus
		0x8e,0xf8,12,		// if Rbus = F8
		0x8e,0xfB,9,		// 	or = FB jr +9 (@ 8f)
		0xa9,			// next (slow)
		0xb1,			// ic--
		0xb4,0,-12,		// if ic!=0 jr -11 (@ 92)
		0xc0,0xef,0x10,0xff,	// ARRAY NOT FOUND, END
		0x8f,0xf8,		// Rbus = F8
		0xfe,1,2,0x8f,0xfb,	// ifn (b0,com) Rbus = FB
		0xd0,0xd3,		// init crc, add Rbus (F8 | FB) to crc
		0x94,			// write Rbus (F8 or FB) (add to crc automaticly)
		0xa9,			// next (slow)
		0xb5,			// ic = sec.len
		0x32,5,			// if (drq = 0) jr +5
		0xc0,0xfb,0x04,		// DATA LOST
		0xf0,3,			// jr +3
		0x93,0xd2,0x31,		// write Rdat, add Rdat to CRC, drq = 1
		0xa9,			// next (0)
		0xb1,			// ic--
		0xb4,0,-15,		// if (ic != 0) jr -(@ 32)

		0xd9,0xa8,0xda,		// write (crc.hi, crc.low) - crc

		0xfd,16,0xff,		// ifn (bit 4,com) END [multisector]
		0x8d,			// Rsec++
		0x01,15			// back to start (WORK 15)
	}
};

uint8_t fdcRd(FDC* fdc,int port) {
	uint8_t res = 0xff;
	switch(port) {
		case FDC_COM:
			res = ((fdc->fptr->flag & FLP_INSERT) ? 0 : 128) | (fdc->idle ? 0 : 1);
			switch (fdc->mode) {
				case 0:
					res |= ((fdc->fptr->flag & FLP_PROTECT) ? 0x40 : 0)\
						| 0x20\
						| (fdc->flag & 0x18)\
						| ((fdc->fptr->trk == 0) ? 4 : 0)\
						| (fdc->idx ? 2 : 0);
					break;
				case 1:
				case 2:
					res |= (fdc->flag & 0x7c) | (fdc->drq ? 2 : 0);
					break;
			}
			fdc->irq = 0;
			break;
		case FDC_TRK:
			res = fdc->trk;
			break;
		case FDC_SEC:
			res = fdc->sec;
			break;
		case FDC_DATA:
			res = fdc->data;
			fdc->drq = 0;
			break;
	}
	return res;
}

void fdcExec(FDC* fdc, uint8_t val) {
	if (!fdc->mr) return;			// no commands aviable during master reset
	if (fdc->idle) {
		fdc->com = val;
		fdc->wptr = NULL;
		if ((val & 0xf0) == 0x00) fdc->wptr = vgwork[0];	// restore		00..0f
		if ((val & 0xf0) == 0x10) fdc->wptr = vgwork[1];	// seek			10..1f
		if ((val & 0xe0) == 0x20) fdc->wptr = vgwork[2];	// step			20..3f
		if ((val & 0xe0) == 0x40) fdc->wptr = vgwork[3];	// step in		40..5f
		if ((val & 0xe0) == 0x60) fdc->wptr = vgwork[4];	// step out		60..7f
		if ((val & 0xe1) == 0x80) {
			fdc->wptr = vgwork[5];	// read sector
			fdc->status = FDC_READ;
		}
		if ((val & 0xe0) == 0xa0) {
			fdc->wptr = vgwork[6];	// write sector
			fdc->status = FDC_WRITE;
		}
		if ((val & 0xfb) == 0xc0) {
			fdc->wptr = vgwork[7];	// read address
			fdc->status = FDC_READ;
		}
		if ((val & 0xfb) == 0xe0) {
			fdc->wptr = vgwork[8];	// read track
			fdc->status = FDC_READ;
		}
		if ((val & 0xfb) == 0xf0) {
			fdc->wptr = vgwork[9];	// write track
			fdc->status = FDC_WRITE;
		}
		if (fdc->wptr == NULL) fdc->wptr = vgwork[11];
		fdc->count = -1;
		fdc->idle = 0;
		fdc->irq = 0;
		fdc->drq = 0;
	}
	if ((val & 0xf0) == 0xd0) {
		fdc->wptr = vgwork[10];		// interrupt
		fdc->count = -1;
		fdc->idle = 0;
		fdc->irq = 0;
		fdc->drq = 0;
	}
}

void fdcSetMr(FDC* fdc,int z) {
	if (!fdc->mr && z) {		// 0->1 : execute com 3
		fdcExec(fdc,0x03);	// restore
		fdc->mr = z;
		fdc->sec = 1;
	} else {
		fdc->mr = z;
	}
}

void fdcWr(FDC* fdc,int port,uint8_t val) {
	switch (port) {
		case FDC_COM:
			fdcExec(fdc,val);
			break;
		case FDC_TRK:
			fdc->trk = val;
			break;
		case FDC_SEC:
			fdc->sec = val;
			break;
		case FDC_DATA:
			fdc->data = val;
			fdc->drq = 0;
			break;
	}
}

void fdcAddCrc(FDC* fdc,uint8_t val) {
	uint32_t tkk = fdc->crc;
	tkk ^= val << 8;
	for (int i = 8; i; i--) {
		if ((tkk *= 2) & 0x10000) tkk ^= 0x1021;
	}
	fdc->crc = tkk & 0xffff;
}

typedef void(*VGOp)(FDC*);


uint8_t p1,dlt;
int32_t delays[6]={6 * MSDELAY,12 * MSDELAY,24 * MSDELAY,32 * MSDELAY,15 * MSDELAY, 50 * MSDELAY};	// 6, 12, 20, 32, 15, 50ms (hlt-hld)

void v01(FDC* p) {p1 = *(p->wptr++); p->wptr = vgwork[p1];}
void v02(FDC* p) {p1 = *(p->wptr++); p->sp = p->wptr; p->wptr = vgwork[p1];}
void v03(FDC* p) {p->wptr = p->sp;}

void v10(FDC* p) {p->count += p->turbo ? TRBDELAY : delays[p->com & 3];}
void v11(FDC* p) {p1 = *(p->wptr++); p->count += p->turbo ? TRBDELAY : delays[p1];}

void v20(FDC* p) {p->mode = *(p->wptr++);}

void v30(FDC* p) {p->drq = 0;}
void v31(FDC* p) {p->drq = 1;}
void v32(FDC* p) {dlt = *(p->wptr++); if (!p->drq) p->wptr += (int8_t)dlt;}

void v80(FDC* p) {p->trk = *(p->wptr++);}
void v81(FDC* p) {p->trk--;}
void v82(FDC* p) {p->trk++;}
void v83(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->fptr->trk == p1) p->wptr += (int8_t)dlt;}
void v84(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->trk == p1) p->wptr += (int8_t)dlt;}
void v85(FDC* p) {dlt = *(p->wptr++); if (p->buf[0] == p->trk) p->wptr += (int8_t)dlt;}
void v86(FDC* p) {dlt = *(p->wptr++); if (p->trk == p->data) p->wptr += (int8_t)dlt;}
void v87(FDC* p) {dlt = *(p->wptr++); if (p->trk < p->data) p->wptr += (int8_t)dlt;}
void v88(FDC* p) {p->sec = p->data;}
void v89(FDC* p) {dlt = *(p->wptr++); if (p->trk != p->data) p->wptr += (int8_t)dlt;}
void v8A(FDC* p) {dlt = *(p->wptr++); if (p->sec != p->data) p->wptr += (int8_t)dlt;}
void v8B(FDC* p) {dlt = *(p->wptr++); if (((p->com & 8)?0:1) != p->data) p->wptr += (int8_t)dlt;}
void v8C(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->data == p1) p->wptr += (int8_t)dlt;}
void v8D(FDC* p) {p->sec++;}
void v8E(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->bus == p1) p->wptr += (int8_t)dlt;}
void v8F(FDC* p) {p->bus = *(p->wptr++);}

void v90(FDC* p) {p1 = *(p->wptr++); p->buf[p1] = flpRd(p->fptr); p->bus = p->buf[p1];}
void v91(FDC* p) {p->data = flpRd(p->fptr);}
void v92(FDC* p) {p->bus = flpRd(p->fptr);}
void v93(FDC* p) {flpWr(p->fptr,p->data);}
void v94(FDC* p) {flpWr(p->fptr,p->bus);}

void vA0(FDC* p) {p->sdir = 0; flpStep(p->fptr,0);}
void vA1(FDC* p) {p->sdir = 1; flpStep(p->fptr,1);}
void vA2(FDC* p) {flpStep(p->fptr,p->sdir);}

void vA8(FDC* p) {
	if (p->turbo) {
		p->tf = BYTEDELAY;
		p->count = 0;
		if (flpNext(p->fptr,p->side)) p->t = 0;
	} else {
		p->count += p->tf;
	}
}
void vA9(FDC* p) {
	p->count += p->tf;
}
void vAA(FDC* p) {p->side = !p->side;}

void vB0(FDC* p) {p->ic = *(p->wptr++);}
void vB1(FDC* p) {p->ic--;}
void vB2(FDC* p) {p->ic++;}
void vB3(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->ic == p1) p->wptr += (int8_t)dlt;}
void vB4(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->ic != p1) p->wptr += (int8_t)dlt;}
void vB5(FDC* p) {p->ic = (128 << (p->buf[0] & 3));}				// 128,256,512,1024

void vC0(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); p->flag &= p1; p->flag |= dlt;}
void vC1(FDC* p) {flpFillFields(p->fptr,p->fptr->rtrk,1);}

void vD0(FDC* p) {p->crc = 0xcdb4; p->crchi = 1;}
void vD1(FDC* p) {fdcAddCrc(p,*(p->wptr++));}
void vD2(FDC* p) {fdcAddCrc(p,p->data);}
void vD3(FDC* p) {fdcAddCrc(p,p->bus);}
void vD4(FDC* p) {
	p->fcrc = flpRd(p->fptr);
	if (flpNext(p->fptr,p->side)) p->t = 0;	// if (flpNext(p->fptr,p->side)) p->ti = p->t;
	p->fcrc |= (flpRd(p->fptr) << 8);
}	// read crc from floppy
void vD5(FDC* p) {
	dlt = *(p->wptr++); if (p->crc == p->fcrc) p->wptr += (int8_t)dlt;
}
void vD7(FDC* p) {p->fcrc = (flpRd(p->fptr) << 8);}
void vD8(FDC* p) {p->fcrc |= flpRd(p->fptr);}
void vD9(FDC* p) {flpWr(p->fptr,(p->crc & 0xff00) >> 8);}
void vDA(FDC* p) {flpWr(p->fptr,p->crc & 0xff);}
void vDF(FDC* p) {if (p->crchi) {
			flpWr(p->fptr,(p->crc & 0xff00) >> 8);
		} else {
			flpWr(p->fptr,p->crc & 0xff);
		}
		p->crchi = !p->crchi;
}

void vE0(FDC* p) {
	p1 = *(p->wptr++);
	if (p1 == 0) {
		p->fptr->flag &= ~(FLP_MOTOR | FLP_HEAD);
	} else {
		if (!(p->fptr->flag & FLP_HEAD)) p->count += p->turbo ? TRBDELAY : delays[5];
		p->fptr->flag |= (FLP_MOTOR | FLP_HEAD);
	}
}

void vF0(FDC* p) {dlt = *(p->wptr++); p->wptr += (int8_t)dlt;}
void vF1(FDC* p) {p->wptr = NULL; p->count = 0; p->status = FDC_IDLE;}
void vF8(FDC* p) {dlt = *(p->wptr++); if (p->fptr->flag & FLP_PROTECT) p->wptr += (int8_t)dlt;}
void vF9(FDC* p) {dlt = *(p->wptr++); if (p->fptr->flag & FLP_INSERT) p->wptr += (int8_t)dlt;}	// READY
void vFA(FDC* p) {dlt = *(p->wptr++); if (p->sdir) p->wptr += (int8_t)dlt;}
void vFB(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if (p->fptr->field == p1) p->wptr += (int8_t)dlt;}
void vFC(FDC* p) {dlt = *(p->wptr++); if (p->strb) p->wptr += (int8_t)dlt;}
void vFD(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if ((p->com & p1) == 0) p->wptr += (int8_t)dlt;}
void vFE(FDC* p) {p1 = *(p->wptr++); dlt = *(p->wptr++); if ((p->com & p1) != 0) p->wptr += (int8_t)dlt;}
void vFF(FDC* p) {p->irq = 1; p->idle = 1; p->wptr = &vgidle[0]; p->status = FDC_IDLE;}

VGOp vgfunc[256] = {
	NULL,&v01,&v02,&v03,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&v10,&v11,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&v20,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&v30,&v31,&v32,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&v80,&v81,&v82,&v83,&v84,&v85,&v86,&v87,&v88,&v89,&v8A,&v8B,&v8C,&v8D,&v8E,&v8F,
	&v90,&v91,&v92,&v93,&v94,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&vA0,&vA1,&vA2,NULL,NULL,NULL,NULL,NULL,&vA8,&vA9,&vAA,NULL,NULL,NULL,NULL,NULL,
	&vB0,&vB1,&vB2,&vB3,&vB4,&vB5,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&vC0,&vC1,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&vD0,&vD1,&vD2,&vD3,&vD4,&vD5,NULL,&vD7,&vD8,&vD9,&vDA,NULL,NULL,NULL,NULL,&vDF,
	&vE0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	&vF0,&vF1,NULL,NULL,NULL,NULL,NULL,NULL,&vF8,&vF9,&vFA,&vFB,&vFC,&vFD,&vFE,&vFF
};

// BDI

BDI* bdiCreate() {
	BDI* bdi = (BDI*)malloc(sizeof(BDI));
	for (int i=0; i<4; i++) {
		bdi->flop[i] = flpCreate(i);
	}

	bdi->fdc = fdcCreate(FDC_VG93);
	bdi->fdc->fptr = bdi->flop[0];
	bdi->type = DISK_NONE;
	bdi->flag = 0;

	bdi->fdc765 = fdc_new();
	bdi->drive_a = fd_newdsk();
	bdi->drive_b = fd_newdsk();

	fd_settype(bdi->drive_a,FD_35);
	fd_setheads(bdi->drive_a,2);
	fd_setcyls(bdi->drive_a,80);

	fd_settype(bdi->drive_b,FD_35);
	fd_setheads(bdi->drive_b,2);
	fd_setcyls(bdi->drive_b,80);

	return bdi;
}

void bdiDestroy(BDI* bdi) {
	fdc_destroy(&bdi->fdc765);
	fd_destroy(&bdi->drive_a);
	fd_destroy(&bdi->drive_b);
	free(bdi);
}

void bdiReset(BDI* bdi) {
	bdi->fdc->count = 0;
	fdcSetMr(bdi->fdc,0);
	fdc_reset(bdi->fdc765);
	fdc_setisr(bdi->fdc765,NULL);
	fdc_setdrive(bdi->fdc765,0,bdi->drive_a);
	fdc_setdrive(bdi->fdc765,1,bdi->drive_b);
}

int bdiGetPort(int p) {
	int port = p;
	pcatch = 0;
	if ((p & 0x03)==0x03) {
		if ((p & 0x82) == 0x82) port=BDI_SYS;
		if ((p & 0xe2) == 0x02) port=FDC_COM;
		if ((p & 0xe2) == 0x22) port=FDC_TRK;
		if ((p & 0xe2) == 0x42) port=FDC_SEC;
		if ((p & 0xe2) == 0x62) port=FDC_DATA;
		pcatch = 1;
	}
	return port;
}

int bdiOut(BDI* bdi,int port,uint8_t val) {
	int res = 0;
	switch (bdi->type) {
		case DISK_BDI:
			if (~bdi->flag & BDI_ACTIVE) break;
			port = bdiGetPort(port);
			if (!pcatch) break;
			switch (port) {
				case FDC_COM:
				case FDC_TRK:
				case FDC_SEC:
				case FDC_DATA:
					fdcWr(bdi->fdc,port,val);
					break;
				case BDI_SYS:
					bdi->fdc->fptr = bdi->flop[val & 0x03];	// selet floppy
					fdcSetMr(bdi->fdc,(val & 0x04) ? 1 : 0);		// master reset
					bdi->fdc->block = val & 0x08;
					bdi->fdc->side = (val & 0x10) ? 1 : 0;
					bdi->fdc->mfm = val & 0x40;
					break;
			}
			res = 1;
			break;
		default:
			break;
	}
	return res;
}

int bdiIn(BDI* bdi,int port,uint8_t* val) {
	int res = 0;
	switch (bdi->type) {
		case DISK_BDI:
			if (~bdi->flag & BDI_ACTIVE) break;
			port = bdiGetPort(port);
			if (!pcatch) break;
			*val = 0xff;
			switch (port) {
				case FDC_COM:
				case FDC_TRK:
				case FDC_SEC:
				case FDC_DATA:
					*val = fdcRd(bdi->fdc,port);
					break;
				case BDI_SYS:
					*val = (bdi->fdc->irq ? 0x80 : 0x00) | (bdi->fdc->drq ? 0x40 : 0x00);
					break;
			}
			res = 1;
			break;
		default:
			break;
	}
	return res;
}

void bdiSync(BDI* bdi,int tk) {
	if (bdi->type != DISK_BDI) return;
	uint32_t tz;
	while (tk > 0) {
		if (tk < (int)bdi->fdc->tf) {
			tz = tk;
			bdi->fdc->tf -= tk;
		} else {
			tz = bdi->fdc->tf;
			bdi->fdc->tf = BYTEDELAY;
			if (flpNext(bdi->fdc->fptr,bdi->fdc->side)) bdi->fdc->t = 0;
		}
		bdi->fdc->t += tz;
		bdi->fdc->idxold = bdi->fdc->idx;
		bdi->fdc->idx = (bdi->fdc->t < IDXDELAY);
		bdi->fdc->strb = (!bdi->fdc->idxold) && bdi->fdc->idx;
		if (bdi->fdc->wptr != NULL) {
			bdi->fdc->count -= tz;
			while ((bdi->fdc->wptr != NULL) && (bdi->fdc->count < 0)) {
				bdi->fdc->cop = *(bdi->fdc->wptr++);
				vgfunc[bdi->fdc->cop](bdi->fdc);
			}
		}
		tk -= tz;
	}
}