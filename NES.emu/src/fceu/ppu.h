#pragma once

namespace EmuEx
{
class EmuSystemTaskContext;
class EmuVideo;
}

void FCEUPPU_Init(void);
void FCEUPPU_Reset(void);
void FCEUPPU_Power(void);
int FCEUPPU_Loop(EmuEx::EmuSystemTaskContext, EmuEx::EmuVideo *, int skip);
void FCEUPPU_FrameReady(EmuEx::EmuSystemTaskContext, EmuEx::EmuVideo *, uint8 *data);

void FCEUPPU_LineUpdate();
void FCEUPPU_SetVideoSystem(int w);

extern void (*PPU_hook)(uint32 A);
extern void (*GameHBIRQHook)(void), (*GameHBIRQHook2)(void);

int newppu_get_scanline();
int newppu_get_dot();
void newppu_hacky_emergency_reset();

/* For cart.c and banksw.h, mostly */
extern uint8 NTARAM[0x800], *vnapage[4];
extern uint8 PPUNTARAM;
extern uint8 PPUCHRRAM;

void FCEUPPU_SaveState(void);
void FCEUPPU_LoadState(int version);
uint32 FCEUPPU_PeekAddress();
uint8* FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr);
int FCEUPPU_GetAttr(int ntnum, int xt, int yt);
void ppu_getScroll(int &xpos, int &ypos);


#ifdef _MSC_VER
#define FASTCALL __fastcall
#else
#define FASTCALL
#endif

void PPU_ResetHooks();
extern uint8 (FASTCALL *FFCEUX_PPURead)(uint32 A);
extern void (*FFCEUX_PPUWrite)(uint32 A, uint8 V);
extern uint8 FASTCALL FFCEUX_PPURead_Default(uint32 A);
void FFCEUX_PPUWrite_Default(uint32 A, uint8 V);

extern int g_rasterpos;
extern uint8 PPU[4];
extern bool DMC_7bit;
extern bool paldeemphswap;

enum PPUPHASE {
	PPUPHASE_VBL, PPUPHASE_BG, PPUPHASE_OBJ
};

extern PPUPHASE ppuphase;

extern unsigned char *cdloggervdata;
extern unsigned int cdloggerVideoDataSize;
extern volatile int rendercount, vromreadcount, undefinedvromcount;
