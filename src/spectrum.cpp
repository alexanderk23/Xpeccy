#include "spectrum.h"
#include "video.h"
#include "settings.h"

#include "z80/tables.c"
#include "z80/instr.c"

//extern Settings* sets;
// extern BDI* bdi;
//extern HardWare* hw;

//uint8_t zx_in(int);
//void zx_out(int,uint8_t);

/*
ZOp::ZOp(void(*fn)(ZXBase*),int32_t tks,int32_t cnd,int32_t t1,int32_t t0,const char* nm,int32_t fl) {
	name = nm;
	func = fn;
	t = tks;
	cond = cnd;
	tcn0 = t0;
	tcn1 = t1;
	flags = fl;
}

ZOp::ZOp(void(*fn)(ZXBase*),int32_t tks,const char* nm) {
	name = nm;
	func = fn;
	t = tks;
	cond = CND_NONE;
	tcn0 = tcn1 = 0;
	flags = 0;
}
*/
ZXComp::ZXComp() {
	sys = new ZXBase;
	sys->cpu = new Z80(3.5);
	sys->mem = new Memory(MEM_ZX);
	sys->io = new IOSys(IO_ZX);
	vid = new Video(sys->mem);
	keyb = new Keyboard;
	mouse = new Mouse;
	tape = new Tape;
	bdi = new BDI;
	ide = new IDE;
	aym = new AYSys;
	gs = new GS; gs->reset();
	addHardware("ZX48K",HW_ZX48,0x00,0);
	addHardware("Pentagon",HW_PENT,0x05,0);
	addHardware("Pentagon1024SL",HW_P1024,0x08,0);
	addHardware("Scorpion",HW_SCORP,0x0a,IO_WAIT);
	opt.hwName = "ZX48K";
	opt.romsetName = "ZX48";
	reset();
}

void ZXComp::reset() {
	sys->io->block7ffd=false;
	sys->mem->prt1 = 0;
	sys->mem->prt0 = ((sys->mem->res & 1) << 4);
	sys->mem->setrom(sys->mem->res);
	sys->mem->setram(0);
	sys->cpu->reset();
	vid->curscr = false;
	vid->mode = VID_NORMAL;
	bdi->active = (sys->mem->res == 3);
	bdi->vg93.count = 0;
	bdi->vg93.setmr(false);
	if (gs->flags & GS_RESET) gs->reset();
	aym->sc1->reset(vid->t);
	aym->sc2->reset(vid->t);
	aym->scc = aym->sc1;
	ide->reset();
}

void ZXComp::mapMemory() {
	switch (hw->type) {
		case HW_ZX48:
			sys->mem->setrom(bdi->active?3:1);
			sys->mem->setram(0);
			break;
		case HW_PENT:
			sys->mem->setrom((bdi->active) ? 3 : ((sys->mem->prt0 & 0x10)>>4));
			sys->mem->setram((sys->mem->prt0 & 7) | ((sys->mem->prt0 & 0xc0)>>3));
			break;
		case HW_P1024:
			sys->mem->setrom(bdi->active ? 3 : ((sys->mem->prt1 & 8) ? 0xff : ((sys->mem->prt0 & 0x10)>>4)));
			sys->mem->setram((sys->mem->prt0 & 7) | ((sys->mem->prt1 & 4)?0:((sys->mem->prt0 & 0x20) | ((sys->mem->prt0 & 0xc0)>>3))));
			break;
		case HW_SCORP:
			sys->mem->setrom((sys->mem->prt1 & 0x01)?0xff:((sys->mem->prt1 & 0x02)?2:((bdi->active)?3:((sys->mem->prt0 & 0x10)>>4))));
			sys->mem->setram((sys->mem->prt0 & 7) | ((sys->mem->prt1 & 0x10)>>1) | ((sys->mem->prt1 & 0xc0)>>2));
			break;
	}
}

int32_t ZXComp::getPort(int32_t port) {
	switch (hw->type) {
		case HW_ZX48:
			if ((port & 0x01) == 0) {
				port = (port & 0xff00) | 0xfe;
			}
			break;
		case HW_PENT:
			if ((port & 0x8002) == 0x0000) port = 0x7ffd;
			if ((port & 0xc002) == 0x8000) port = 0xbffd;
			if ((port & 0xc002) == 0xc000) port = 0xfffd;
			if ((port & 0x05a1) == 0x0081) port = 0xfadf;
			if ((port & 0x05a1) == 0x0181) port = 0xfbdf;
			if ((port & 0x05a1) == 0x0581) port = 0xffdf;
			if ((port & 0x0003) == 0x0002) port = (port & 0xff00) | 0xfe;	// TODO: уточнить
			break;
		case HW_P1024:
			if ((port & 0x8002) == 0x0000) port = 0x7ffd;
			if ((port & 0xc002) == 0x8000) port = 0xbffd;
			if ((port & 0xc002) == 0xc000) port = 0xfffd;
			if ((port & 0xf008) == 0xe000) port = 0xeff7;
			if ((port & 0x0003) == 0x0002) port = (port & 0xff00) | 0xfe;	// TODO: уточнить
			break;
		case HW_SCORP:
			if ((port & 0x0023) == 0x0001) port = 0x00dd;		// printer
			if ((port & 0x0523) == 0x0003) port = 0xfadf;		// mouse
			if ((port & 0x0523) == 0x0103) port = 0xfbdf;
			if ((port & 0x0523) == 0x0503) port = 0xffdf;
			if ((port & 0xc023) == 0x0021) port = 0x1ffd;		// mem
			if ((port & 0xc023) == 0x4021) port = 0x7ffd;
			if ((port & 0xc023) == 0x8021) port = 0xbffd;		// ay
			if ((port & 0xc023) == 0xc021) port = 0xfffd;
			if ((port & 0x0023) == 0x0022) port = (port & 0xff00) | 0xfe;	// fe
			if ((port & 0x0023) == 0xc023) port = 0x00ff;		// ff
			break;
	}
	return port;
}

uint8_t ZXComp::in(int32_t port) {
	uint8_t res = 0xff;
	gs->sync(vid->t);
#if IDE_ENABLE
	if (ide->in(port,&res,bdi->active)) return res;
#endif
	if (gs->in(port,&res)) return res;
	if (bdi->in(port,&res)) return res;
	port = getPort(port);
	res = sys->io->iostdin(port);
	switch (hw->type) {
		case HW_ZX48:
			break;
		case HW_PENT:
			break;
		case HW_P1024:
			break;
		case HW_SCORP:
			switch (port) {
				case 0x7ffd:
					sys->cpu->frq = 7.0;
					break;
				case 0x1ffd:
					sys->cpu->frq = 3.5;
					break;
				case 0xff:
					if (((vid->curr.h - vid->bord.h) < 256) && ((vid->curr.v - vid->bord.v) < 192)) {
						res = vid->atrbyte;
					}
					break;
			}
			break;
	}
	return res;
}

void ZXComp::out(int32_t port,uint8_t val) {
	gs->sync(vid->t);
#if IDE_ENABLE
	if (ide->out(port,val,bdi->active)) return;
#endif
	if (gs->out(port,val)) return;
	if (bdi->out(port,val)) return;
	port = getPort(port);
	sys->io->iostdout(port,val);
	switch (hw->type) {
		case HW_ZX48:
			break;
		case HW_PENT:
			switch(port) {
				case 0x7ffd:
					sys->io->out7ffd(val);
					mapMemory();
					break;
			}
			break;
		case HW_P1024:
			switch(port) {
				case 0x7ffd:
					if (sys->io->block7ffd) break;
					vid->curscr = val & 0x08;
					sys->mem->prt0 = val;
					sys->io->block7ffd = ((sys->mem->prt1 & 4) && (val & 0x20));
					mapMemory();
					break;
				case 0xeff7:
					sys->mem->prt1 = val;
					vid->mode = (val & 1) ? VID_ALCO : VID_NORMAL;
					sys->cpu->frq = (val & 16) ? 7.0 : 3.5;
					mapMemory();
					break;
			}
			break;
		case HW_SCORP:
			switch(port) {
				case 0x7ffd:
					sys->io->out7ffd(val);
					mapMemory();
					break;
				case 0x1ffd:
					sys->mem->prt1 = val;
					mapMemory();
					break;
			}
			break;
	}
}

void ZXComp::exec() {
	ZOpResult res = sys->fetch();
	vid->sync(res.ticks, sys->cpu->frq);
	sys->istrb = vid->intStrobe;
//	sys->istrb |= vid->intupt;
	res.exec(sys);
	if (bdi->enable) {
		bdi->sync(vid->t);
		if (bdi->active && (sys->cpu->hpc > 0x3f)) {
			bdi->active = false;
			mapMemory();	//hw->setrom();
		}
		if (!bdi->active && (sys->cpu->hpc == 0x3d) && (sys->mem->prt0 & 0x10)) {
			bdi->active = true;
			mapMemory();	//hw->setrom();
		}
	}
	if ((sys->cpu->hpc > 0x3f) && sys->nmi) {
		NMIHandle();
	}
}

void ZXComp::INTHandle() {
	int32_t res = sys->interrupt();
//printf("%i\n",res);
	vid->sync(res,sys->cpu->frq);
}

void ZXComp::NMIHandle() {
	sys->cpu->iff2 = sys->cpu->iff1;
	sys->cpu->iff1 = false;
	sys->mem->wr(--sys->cpu->sp,sys->cpu->hpc);
	sys->mem->wr(--sys->cpu->sp,sys->cpu->lpc);
	sys->cpu->pc = 0x66;
	bdi->active = true;
	mapMemory();
	sys->cpu->t += 11;
	vid->sync(11,sys->cpu->frq);
}

void ZXComp::setHardware(std::string nam) {
	hw = NULL;
	for (uint32_t i = 0; i < hwlist.size(); i++) {
		if (hwlist[i].name == nam) {
			hw = &hwlist[i];
			sys->hwflags = hw->flags;
//			sys->mem->mask = hw->mask;
			break;
		}
	}
}

void ZXComp::addHardware(std::string nam, int typ, int msk, int flg) {
	HardWare nhw;
	nhw.name = nam;
	nhw.type = typ;
	nhw.mask = msk;
	nhw.flags = flg;
	hwlist.push_back(nhw);
}

/*
void ZXComp::addHardware(std::string nam,int(*gpfnc)(int),void(*ofnc)(int,uint8_t),uint8_t(*ifnc)(int),void(*sfnc)(), int msk, int flg) {
	HardWare newmac;
	newmac.name = nam;
	newmac.mask = msk;
	newmac.getport = gpfnc;
	newmac.out = ofnc;
	newmac.in = ifnc;
	newmac.setrom = sfnc;
	newmac.flags = flg;
	hwlist.push_back(newmac);
}
*/

ZXBase::ZXBase () {
	inst[0] = nopref;
	inst[1] = ixpref;
	inst[2] = iypref;
	inst[4] = cbpref;
	inst[5] = cxpref;
	inst[6] = cypref;
	inst[8] = edpref;
	nmi = false;
}

ZOpResult ZXBase::fetch() {
	ZOpResult zres;
//	istrb = false;
	cpu->err = false;
//	uint32_t fcnt = cpu->t;
//	if (cpu->block & !dbg->active) {sync(4); return;}
	if (cpu->nextei) {
		cpu->iff1 = cpu->iff2 = true;
		cpu->nextei = false;
	}
	ZOp *res = NULL;
	cpu->mod = 0;
	cpu->ti = cpu->t;
	do {
		cpu->r = ((cpu->r + 1) & 0x7f) | (cpu->r & 0x80);
		cpu->cod = mem->rd(cpu->pc++);
		res = &inst[cpu->mod][cpu->cod];
		cpu->t += res->t;
		if (res->flags & ZPREF) res->func(this);
	} while (res->flags & ZPREF);
	switch (res->cond) {
		case CND_NONE: break;
		case CND_Z: cpu->t += (cpu->f & FZ) ? res->tcn1 : res->tcn0; break;
		case CND_C: cpu->t += (cpu->f & FC) ? res->tcn1 : res->tcn0; break;
		case CND_P: cpu->t += (cpu->f & FP) ? res->tcn1 : res->tcn0; break;
		case CND_S: cpu->t += (cpu->f & FS) ? res->tcn1 : res->tcn0; break;
		case CND_DJNZ: cpu->t += (cpu->b != 1) ? res->tcn1 : res->tcn0; break;
		case CND_LDIR: cpu->t += (cpu->bc != 1) ? res->tcn1 : res->tcn0; break;
		case CND_CPIR: cpu->t += ((cpu->bc != 1) && (cpu->a != mem->rd(cpu->hl))) ? res->tcn1 : res->tcn0; break;
		default: printf("undefined condition\n"); throw(0);
	}
//	if (vid != NULL) {vid->sync(cpu->t - cpu->ti, cpu->frq); fcnt = cpu->t;}
	zres.exec = res->func;
//	res->func(this);
	if ((hwflags & IO_WAIT) && (hwflags & WAIT_ON) && (cpu->t & 1)) cpu->t++;		// WAIT
//	if ((cpu->hpc > 0x3f) && nmi) nmihandle();					// NMI
//	if ((vid != NULL) && (cpu->t > fcnt)) vid->sync(cpu->t - fcnt, cpu->frq);
//	return (cpu->t - cpu->ti);
	zres.ticks = cpu->t - cpu->ti;
	return zres;
}

int32_t ZXBase::interrupt() {
	if (!cpu->iff1) return 0;
	cpu->iff1 = cpu->iff2 = false;
	int32_t res = 0;
	switch (cpu->imode) {
		case 0: mem->wr(--cpu->sp,cpu->hpc);
			mem->wr(--cpu->sp,cpu->lpc);
			cpu->pc = 0x38;
			cpu->mptr = cpu->pc;
			res = 12;
			break;
		case 1: mem->wr(--cpu->sp,cpu->hpc);
			mem->wr(--cpu->sp,cpu->lpc);
			cpu->pc = 0x38;
			cpu->mptr = cpu->pc;
			res = 13;
			break;
		case 2: mem->wr(--cpu->sp,cpu->hpc);
			mem->wr(--cpu->sp,cpu->lpc);
			cpu->adr = (cpu->i << 8) | 0xff;
			cpu->lpc = mem->rd(cpu->adr);
			cpu->hpc = mem->rd(cpu->adr+1);
			cpu->mptr = cpu->pc;
			res = 19;
			break;
	}
	if ((hwflags & IO_WAIT) && (hwflags & WAIT_ON) && (cpu->t & 1)) res++;		// TODO: is INT handle is WAIT'ing too?
	cpu->t += res;
	return res;
}
