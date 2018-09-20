#include "hardware.h"

// debug


unsigned char brkIn(Computer* comp, unsigned short port) {
	printf("IN %.4X (dos:rom:cpm = %i:%i:%i)\n",port,comp->dos,comp->rom,comp->cpm);
	assert(0);
	comp->brk = 1;
	return 0xff;
}

void brkOut(Computer* comp, unsigned short port, unsigned char val) {
	printf("OUT %.4X,%.2X (dos:rom:cpm = %i:%i:%i)\n",port,val,comp->dos,comp->rom,comp->cpm);
	assert(0);
	comp->brk = 1;
}

unsigned char dummyIn(Computer* comp, unsigned short port) {
	return 0xff;
}

void dummyOut(Computer* comp, unsigned short port, unsigned char val) {

}

// INT handle/check

void zx_sync(Computer* comp, int ns) {
	// devices
	comp->beep->accum += ns;
	comp->tapCount += ns;
	difSync(comp->dif, ns);
	gsSync(comp->gs, ns);
	saaSync(comp->saa, ns);
	// nmi
	if ((comp->cpu->pc > 0x3fff) && comp->nmiRequest) {
		comp->cpu->intrq |= Z80_NMI;	// request nmi
		comp->dos = 1;			// set dos page
		comp->rom = 1;
		comp->hw->mapMem(comp);
	}
	// int
	if (comp->vid->intFRAME && !(comp->cpu->intrq & Z80_INT)) {		// so something with int video->cpu
		comp->intVector = 0xff;
		comp->cpu->intrq |= Z80_INT;
		// comp->vid->intFRAME = 0;	// ??
	} else if (!comp->vid->intFRAME && (comp->cpu->intrq & Z80_INT)) {
		comp->cpu->intrq &= ~Z80_INT;
	} else if (comp->vid->intLINE) {
		comp->intVector = 0xfd;
		comp->cpu->intrq |= Z80_INT;
		comp->vid->intLINE = 0;
	} else if (comp->vid->intDMA) {
		comp->intVector = 0xfb;
		comp->cpu->intrq |= Z80_INT;
		comp->vid->intDMA = 0;
	} else if (comp->cpu->intrq & Z80_INT) {
		comp->cpu->intrq &= ~Z80_INT;
	}
}

// zx keypress/release

void zx_keyp(Computer* comp, keyEntry ent) {
	kbdPress(comp->keyb, ent);
}

void zx_keyr(Computer* comp, keyEntry ent) {
	kbdRelease(comp->keyb, ent);
}

// volume

sndPair zx_vol(Computer* comp, sndVolume* sv) {
	sndPair vol;
	sndPair svol;
	int lev = 0;
	vol.left = 0;
	vol.right = 0;
	// 1:tape sound
	if (comp->tape->on) {
		if (comp->tape->rec) {
			lev = comp->tape->levRec ? 0x1000 * sv->tape / 100 : 0;
		} else {
			lev = (comp->tape->volPlay << 8) * sv->tape / 1600;
		}
	}
	// 2:beeper
	bcSync(comp->beep, -1);
	lev += comp->beep->val * sv->beep / 6;
	vol.left = lev;
	vol.right = lev;
	// 3:turbo sound
	svol = tsGetVolume(comp->ts);
	vol.left += svol.left * sv->ay / 100;
	vol.right += svol.right * sv->ay / 100;
	// 4:general sound
	svol = gsVolume(comp->gs);
	vol.left += svol.left * sv->gs / 100;
	vol.right += svol.right * sv->gs / 100;
	// 5:soundrive
	svol = sdrvVolume(comp->sdrv);
	vol.left += svol.left * sv->sdrv / 100;
	vol.right += svol.right * sv->sdrv / 100;
	// 6:saa
	svol = saaVolume(comp->saa);
	vol.left += svol.left * sv->saa / 100;
	vol.right += svol.right * sv->saa / 100;
	// end
	return vol;
}

// in

int zx_dev_wr(Computer* comp, unsigned short adr, unsigned char val, int dos) {
	int res = 0;
	res = gsWrite(comp->gs, adr, val);
	if (!dos) {
		res |= saaWrite(comp->saa, adr, val);
		res |= sdrvWrite(comp->sdrv, adr, val);
	}
	res |= ideOut(comp->ide, adr, val, dos);
	return res;
}

int zx_dev_rd(Computer* comp, unsigned short adr, unsigned char* ptr, int dos) {
	if (gsRead(comp->gs, adr, ptr)) return 1;
	if (ideIn(comp->ide, adr, ptr, dos)) return 1;
	return 0;
}

unsigned char xIn1F(Computer* comp, unsigned short port) {
	return joyInput(comp->joy);
}

unsigned char xInFE(Computer* comp, unsigned short port) {
	unsigned char res = kbdRead(comp->keyb, port);
	if (comp->tape->on)
		res |= ((comp->tape->volPlay & 0x80) ? 0x40 : 0x00);
	else if (comp->beep->lev)
		res |= 0x40;
	return res;
}

unsigned char xInFFFD(Computer* comp, unsigned short port) {
	return tsIn(comp->ts, 0xfffd);
}

unsigned char xInFADF(Computer* comp, unsigned short port) {
	unsigned char res = 0xff;
	comp->mouse->used = 1;
	if (!comp->mouse->enable) return res;
	if (comp->mouse->hasWheel) {
		res &= 0x0f;
		res |= ((comp->mouse->wheel & 0x0f) << 4);
	}
	res ^= comp->mouse->mmb ? 4 : 0;
	if (comp->mouse->swapButtons) {
		res ^= comp->mouse->rmb ? 1 : 0;
		res ^= comp->mouse->lmb ? 2 : 0;
	} else {
		res ^= comp->mouse->lmb ? 1 : 0;
		res ^= comp->mouse->rmb ? 2 : 0;
	}
	return res;
}

unsigned char xInFBDF(Computer* comp, unsigned short port) {
	comp->mouse->used = 1;
	return comp->mouse->enable ? comp->mouse->xpos : 0xff;
}

unsigned char xInFFDF(Computer* comp, unsigned short port) {
	comp->mouse->used = 1;
	return comp->mouse->enable ? comp->mouse->ypos : 0xff;
}

// out

void xOutFE(Computer* comp, unsigned short port, unsigned char val) {
	comp->vid->nextbrd = val & 0x07;
	bcSync(comp->beep, -1);
	comp->beep->lev = (val & 0x10) ? 1 : 0;
	comp->tape->levRec = (val & 0x08) ? 1 : 0;
}

void xOutBFFD(Computer* comp, unsigned short port, unsigned char val) {
	tsOut(comp->ts, 0xbffd, val);
}

void xOutFFFD(Computer* comp, unsigned short port, unsigned char val) {
	tsOut(comp->ts, 0xfffd, val);
}
