/*-------------------------------------------------------------------------
  gen.c - source file for code generation for the 68HC08

  Copyright (C) 1998, Sandeep Dutta . sandeep.dutta@usa.net
  Copyright (C) 1999, Jean-Louis VERN.jlvern@writeme.com
  Bug Fixes - Wojciech Stryjewski  wstryj1@tiger.lsu.edu (1999 v2.1.9a)
  Hacked for the 68HC08:
  Copyright (C) 2003, Erik Petrich

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
-------------------------------------------------------------------------*/

/* Use the D macro for basic (unobtrusive) debugging messages */
#define D(x) do if (options.verboseAsm) { x; } while (0)
/* Use the DD macro for detailed debugging messages */
#define DD(x)
//#define DD(x) x

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

char *aopLiteral (value * val, int offset);
char *aopLiteralLong (value * val, int offset, int size);
extern int allocInfo;
static int pushReg (reg_info * reg, bool freereg);
static void pullReg (reg_info * reg);
static void transferAopAop (asmop * srcaop, int srcofs, asmop * dstaop, int dstofs);

static char *zero = "#0x00";
static char *one = "#0x01";
static char *spname;

char *fReturnhc08[] = { "a", "x", "_ret2", "_ret3" };

unsigned fReturnSizeHC08 = 4;   /* shared with ralloc.c */
char **fReturn2 = fReturnhc08;


static struct
{
  short hxPushed;
  short iyPushed;
  short accInUse;
  short nRegsSaved;
  int stackOfs;
  int stackPushes;
  short regsinuse;
  set *sendSet;
}
_G;

static asmop *hc08_aop_pass[8];

extern int hc08_ptrRegReq;
extern int hc08_nRegs;
extern struct dbuf_s *codeOutBuf;
//static void saveRBank (int, iCode *, bool);
static bool operandsEqu (operand * op1, operand * op2);
static void loadRegFromConst (reg_info * reg, char *c);
static char *aopName (asmop * aop);
static asmop *newAsmop (short type);
static char *aopAdrStr (asmop * aop, int loffset, bool bit16);
#define RESULTONSTACK(x) \
                         (IC_RESULT(x) && IC_RESULT(x)->aop && \
                         IC_RESULT(x)->aop->type == AOP_STK )

#define IS_AOP_HX(x) \
        (((x)->type == AOP_REG) \
         && ((x)->aopu.aop_reg[0] == hc08_reg_x) \
         && ((x)->aopu.aop_reg[1] == hc08_reg_h) )

#define IS_AOP_XA(x) \
        (((x)->type == AOP_REG) \
         && ((x)->aopu.aop_reg[0] == hc08_reg_a) \
         && ((x)->aopu.aop_reg[1] == hc08_reg_x) )

#define IS_AOP_A(x) \
        (((x)->type == AOP_REG) \
         && ((x)->aopu.aop_reg[0] == hc08_reg_a) \
         && ((x)->size == 1) )

#define IS_AOP_X(x) \
        (((x)->type == AOP_REG) \
         && ((x)->aopu.aop_reg[0] == hc08_reg_x) \
         && ((x)->size == 1) )

#define IS_AOP_H(x) \
        (((x)->type == AOP_REG) \
         && ((x)->aopu.aop_reg[0] == hc08_reg_h) \
         && ((x)->size == 1) )

#define CLRC    emitcode("clc","")

#if 0
static unsigned char SLMask[] =
{
  0xFF, 0xFE, 0xFC, 0xF8, 0xF0,
  0xE0, 0xC0, 0x80, 0x00
};

static unsigned char SRMask[] =
{
  0xFF, 0x7F, 0x3F, 0x1F, 0x0F,
  0x07, 0x03, 0x01, 0x00
};
#endif

#define LSB     0
#define MSB16   1
#define MSB24   2
#define MSB32   3

#define AOP(op) op->aop
#define AOP_TYPE(op) AOP(op)->type
#define AOP_SIZE(op) AOP(op)->size
#define AOP_OP(aop) aop->op

static bool regalloc_dry_run;
static unsigned char regalloc_dry_run_cost;

static void
emitBranch (char *branchop, symbol * tlbl)
{
  if (!regalloc_dry_run)
    emitcode (branchop, "%05d$", labelKey2num (tlbl->key));
  regalloc_dry_run_cost += 2; /* Todo: brclr is more expensive */
}

/*-----------------------------------------------------------------*/
/* hc08_emitDebuggerSymbol - associate the current code location   */
/*   with a debugger symbol                                        */
/*-----------------------------------------------------------------*/
void
hc08_emitDebuggerSymbol (const char *debugSym)
{
  genLine.lineElement.isDebug = 1;
  emitcode ("", "%s ==.", debugSym);
  genLine.lineElement.isDebug = 0;
}


/*--------------------------------------------------------------------------*/
/* transferRegReg - Transfer from register(s) sreg to register(s) dreg. If  */
/*                  freesrc is true, sreg is marked free and available for  */
/*                  reuse. sreg and dreg must be of equal size              */
/*--------------------------------------------------------------------------*/
static void
transferRegReg (reg_info *sreg, reg_info *dreg, bool freesrc)
{
  int srcidx;
  int dstidx;
  char error = 0;

  /* Nothing to do if no destination. */
  if (!dreg)
    return;

  /* But it's definately an error if there's no source. */
  if (!sreg)
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "NULL sreg in transferRegReg");
      return;
    }

  DD (emitcode ("", "; transferRegReg(%s,%s)", sreg->name, dreg->name));

  srcidx = sreg->rIdx;
  dstidx = dreg->rIdx;

  if (srcidx == dstidx)
    {
      hc08_useReg (dreg);
      return;
    }

  switch (dstidx)
    {
    case A_IDX:
      switch (srcidx)
        {
        case H_IDX:            /* H to A */
          pushReg (hc08_reg_h, FALSE);
          pullReg (hc08_reg_a);
          break;
        case X_IDX:            /* X to A */
          emitcode ("txa", "");
          regalloc_dry_run_cost++;
          break;
        default:
          error = 1;
        }
      break;
    case H_IDX:
      switch (srcidx)
        {
        case A_IDX:            /* A to H */
          pushReg (hc08_reg_a, FALSE);
          pullReg (hc08_reg_h);
          break;
        case X_IDX:            /* X to H */
          pushReg (hc08_reg_x, FALSE);
          pullReg (hc08_reg_h);
          break;
        default:
          error = 1;
        }
      break;
    case X_IDX:
      switch (srcidx)
        {
        case A_IDX:            /* A to X */
          emitcode ("tax", "");
          regalloc_dry_run_cost++;
          break;
        case H_IDX:            /* H to X */
          pushReg (hc08_reg_h, FALSE);
          pullReg (hc08_reg_x);
          break;
        default:
          error = 1;
        }
      break;
    case HX_IDX:
      switch (srcidx)
        {
        case XA_IDX:           /* XA to HX */
          pushReg (hc08_reg_x, FALSE);
          pullReg (hc08_reg_h);
          emitcode ("tax", "");
          regalloc_dry_run_cost++;
          break;
        default:
          error = 1;
        }
      break;
    case XA_IDX:
      switch (srcidx)
        {
        case HX_IDX:           /* HX to XA */
          emitcode ("txa", "");
          regalloc_dry_run_cost++;
          pushReg (hc08_reg_h, FALSE);
          pullReg (hc08_reg_x);
          break;
        default:
          error = 1;
        }
      break;
    default:
      error = 1;
    }

  wassertl (!error, "bad combo in transferRegReg");

  if (freesrc)
    hc08_freeReg (sreg);

  dreg->aop = sreg->aop;
  dreg->aopofs = sreg->aopofs;
  dreg->isFree = FALSE;
  hc08_useReg (dreg);
}

/*--------------------------------------------------------------------------*/
/* updateCFA - update the debugger information to reflect the current       */
/*             connonical frame address relative to the stack pointer       */
/*--------------------------------------------------------------------------*/
static void
updateCFA (void)
{
  /* there is no frame unless there is a function */
  if (!currFunc)
    return;

  debugFile->writeFrameAddress (NULL, hc08_reg_sp, 1 + _G.stackOfs + _G.stackPushes);
}

/*--------------------------------------------------------------------------*/
/* pushReg - Push register reg onto the stack. If freereg is true, reg is   */
/*           marked free and available for reuse.                           */
/*--------------------------------------------------------------------------*/
static int
pushReg (reg_info * reg, bool freereg)
{
  int regidx = reg->rIdx;

  switch (regidx)
    {
    case A_IDX:
      emitcode ("psha", "");
      regalloc_dry_run_cost++;
      _G.stackPushes++;
      updateCFA ();
      break;
    case X_IDX:
      emitcode ("pshx", "");
      regalloc_dry_run_cost++;
      _G.stackPushes++;
      updateCFA ();
      break;
    case H_IDX:
      emitcode ("pshh", "");
      regalloc_dry_run_cost++;
      _G.stackPushes++;
      updateCFA ();
      break;
    case HX_IDX:
      emitcode ("pshx", "");
      regalloc_dry_run_cost++;
      _G.stackPushes++;
      updateCFA ();
      emitcode ("pshh", "");
      regalloc_dry_run_cost++;
      _G.stackPushes++;
      updateCFA ();
      break;
    case XA_IDX:
      emitcode ("psha", "");
      regalloc_dry_run_cost++;
      updateCFA ();
      _G.stackPushes++;
      emitcode ("pshx", "");
      regalloc_dry_run_cost++;
      updateCFA ();
      _G.stackPushes++;
      break;
    default:
      break;
    }
  if (freereg)
    hc08_freeReg (reg);
  return -_G.stackOfs - _G.stackPushes;
}

/*--------------------------------------------------------------------------*/
/* pullReg - Pull register reg off the stack.                               */
/*--------------------------------------------------------------------------*/
static void
pullReg (reg_info * reg)
{
  int regidx = reg->rIdx;

  switch (regidx)
    {
    case A_IDX:
      emitcode ("pula", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      break;
    case X_IDX:
      emitcode ("pulx", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      break;
    case H_IDX:
      emitcode ("pulh", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      break;
    case HX_IDX:
      emitcode ("pulh", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      emitcode ("pulx", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      break;
    case XA_IDX:
      emitcode ("pulx", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      emitcode ("pula", "");
      regalloc_dry_run_cost++;
      _G.stackPushes--;
      updateCFA ();
      break;
    default:
      break;
    }
  hc08_useReg (reg);
  hc08_dirtyReg (reg, FALSE);
}

/*--------------------------------------------------------------------------*/
/* pullNull - Discard n bytes off the top of the stack                      */
/*--------------------------------------------------------------------------*/
static void
pullNull (int n)
{
  if (n)
    {
      emitcode ("ais", "#%d", n);
      regalloc_dry_run_cost += 2;
      _G.stackPushes -= n;
      updateCFA ();
    }
}

/*--------------------------------------------------------------------------*/
/* pushRegIfUsed - Push register reg if marked in use. Returns true if the  */
/*                 push was performed, false otherwise.                     */
/*--------------------------------------------------------------------------*/
static bool
pushRegIfUsed (reg_info *reg)
{
  if (!reg->isFree)
    {
      pushReg (reg, TRUE);
      return TRUE;
    }
  else
    return FALSE;
}

/*--------------------------------------------------------------------------*/
/* pushRegIfSurv - Push register reg if marked surviving. Returns true if   */
/*                 the push was performed, false otherwise.                 */
/*--------------------------------------------------------------------------*/
static bool
pushRegIfSurv (reg_info *reg)
{
  if (!reg->isDead)
    {
      pushReg (reg, TRUE);
      return TRUE;
    }
  else
    return FALSE;
}

/*--------------------------------------------------------------------------*/
/* pullOrFreeReg - If needpull is true, register reg is pulled from the     */
/*                 stack. Otherwise register reg is marked as free.         */
/*--------------------------------------------------------------------------*/
static void
pullOrFreeReg (reg_info * reg, bool needpull)
{
  if (needpull)
    pullReg (reg);
  else
    hc08_freeReg (reg);
}

/*--------------------------------------------------------------------------*/
/* adjustStack - Adjust the stack pointer by n bytes.                       */
/*--------------------------------------------------------------------------*/
static void
adjustStack (int n)
{
  while (n)
    {
      if (n > 127)
        {
          emitcode ("ais", "#127");
          regalloc_dry_run_cost += 2;
          n -= 127;
          _G.stackPushes -= 127;
          updateCFA ();
        }
      else if (n < -128)
        {
          emitcode ("ais", "#-128");
          regalloc_dry_run_cost += 2;
          n += 128;
          _G.stackPushes += 128;
          updateCFA ();
        }
      else
        {
          emitcode ("ais", "#%d", n);
          regalloc_dry_run_cost += 2;
          _G.stackPushes -= n;
          n = 0;
          updateCFA ();
        }
    }
}


/*--------------------------------------------------------------------------*/
/* aopName - Return a string with debugging information about an asmop.     */
/*--------------------------------------------------------------------------*/
static char *
aopName (asmop * aop)
{
  static char buffer[256];
  char *buf = buffer;

  if (!aop)
    return "(asmop*)NULL";

  switch (aop->type)
    {
    case AOP_IMMD:
      sprintf (buf, "IMMD(%s)", aop->aopu.aop_immd.aop_immd1);
      return buf;
    case AOP_LIT:
      sprintf (buf, "LIT(%s)", aopLiteral (aop->aopu.aop_lit, 0));
      return buf;
    case AOP_DIR:
      sprintf (buf, "DIR(%s)", aop->aopu.aop_dir);
      return buf;
    case AOP_EXT:
      sprintf (buf, "EXT(%s)", aop->aopu.aop_dir);
      return buf;
    case AOP_SOF:
      sprintf (buf, "SOF(%s)", OP_SYMBOL (aop->op)->name);
      return buf;
    case AOP_REG:
      sprintf (buf, "REG(%s,%s,%s,%s)",
               aop->aopu.aop_reg[3] ? aop->aopu.aop_reg[3]->name : "-",
               aop->aopu.aop_reg[2] ? aop->aopu.aop_reg[2]->name : "-",
               aop->aopu.aop_reg[1] ? aop->aopu.aop_reg[1]->name : "-",
               aop->aopu.aop_reg[0] ? aop->aopu.aop_reg[0]->name : "-");
      return buf;
    case AOP_STK:
      return "STK";
    case AOP_STR:
      return "STR";
    default:
      sprintf (buf, "?%d", aop->type);
      return buf;
    }

  return "?";
}


/*--------------------------------------------------------------------------*/
/* loadRegFromAop - Load register reg from logical offset loffset of aop.   */
/*--------------------------------------------------------------------------*/
static void
loadRegFromAop (reg_info * reg, asmop * aop, int loffset)
{
  int regidx = reg->rIdx;

  if (aop->stacked && aop->stk_aop[loffset])
    {
      loadRegFromAop (reg, aop->stk_aop[loffset], 0);
      return;
    }

#if 0
  printf ("loadRegFromAop called\n");
  if (!reg)
    {
      printf (" reg = NULL\n");
      return;
    }
  printf (" reg = %s\n", reg->name);
  if (!aop)
    {
      printf (" aop = NULL\n");
      return;
    }
  printf (" aop->type = %d\n", aop->type);
  printf (" loffset = %d\n", loffset);

  if (aop->op)
    printf (" aop has operand link\n");
  else
    printf (" aop missing operand link\n");
  if (reg->aop)
    printf (" reg has operand link\n");
  else
    printf (" reg missing operand link\n");
#endif

  DD (emitcode ("", ";     loadRegFromAop (%s, %s, %d)", reg->name, aopName (aop), loffset));

  /* If operand is volatile, we cannot optimize. */
  if (!aop->op || isOperandVolatile (aop->op, FALSE))
    goto forceload;


  /* If this register already has this offset of the operand
     then we need only mark it as in use. */
  if (reg->aop && reg->aop->op && aop->op && operandsEqu (reg->aop->op, aop->op) && (reg->aopofs == loffset))
    {
      hc08_useReg (reg);
      DD (emitcode ("", "; already had correct value for %s", reg->name));
      return;
    }

  /* TODO: check to see if we can transfer from another register */

  if (hc08_reg_h->aop && hc08_reg_h->aop->op && aop->op
      && operandsEqu (hc08_reg_h->aop->op, aop->op) && (hc08_reg_h->aopofs == loffset))
    {
      DD (emitcode ("", "; found correct value for %s in h", reg->name));
      transferRegReg (hc08_reg_h, reg, FALSE);
      hc08_useReg (reg);
      return;
    }


  if (hc08_reg_x->aop && hc08_reg_x->aop->op && aop->op
      && operandsEqu (hc08_reg_x->aop->op, aop->op) && (hc08_reg_x->aopofs == loffset))
    {
      DD (emitcode ("", "; found correct value for %s in x", reg->name));
      transferRegReg (hc08_reg_x, reg, FALSE);
      hc08_useReg (reg);
      return;
    }

  if (hc08_reg_a->aop && hc08_reg_a->aop->op && aop->op
      && operandsEqu (hc08_reg_a->aop->op, aop->op) && (hc08_reg_a->aopofs == loffset))
    {
      DD (emitcode ("", "; found correct value for %s in a", reg->name));
      transferRegReg (hc08_reg_a, reg, FALSE);
      hc08_useReg (reg);
      return;
    }

forceload:

  switch (regidx)
    {
    case A_IDX:
      if (aop->type == AOP_REG)
        {
          if (loffset < aop->size)
            transferRegReg (aop->aopu.aop_reg[loffset], reg, FALSE);
          else
            {
              emitcode ("clra", "");      /* TODO: handle sign extension */
              regalloc_dry_run_cost++;
            }
        }
      else
        {
          char *l = aopAdrStr (aop, loffset, FALSE);
          if (!strcmp (l, zero))
            {
              emitcode ("clra", "");
              regalloc_dry_run_cost++;
            }
          else
            {
              emitcode ("lda", "%s", l);
              regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 : 3);
            }
        }
      break;
    case X_IDX:
      if (aop->type == AOP_REG)
        {
          if (loffset < aop->size)
            transferRegReg (aop->aopu.aop_reg[loffset], reg, FALSE);
          else
            {
              emitcode ("clrx", "");      /* TODO: handle sign extension */
              regalloc_dry_run_cost++;
            }
        }
      else
        {
          char *l = aopAdrStr (aop, loffset, FALSE);
          if (!strcmp (l, zero))
            {
              emitcode ("clrx", "");
              regalloc_dry_run_cost++;
            }
          else
            {
              emitcode ("ldx", "%s", l);
              regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 :3);
            }
        }
      break;
    case H_IDX:
      {
        char *l = aopAdrStr (aop, loffset, FALSE);
        if (!strcmp (l, zero))
         {
            emitcode ("clrh", "");
            regalloc_dry_run_cost++;
            break;
          }
      }
      if (aop->type == AOP_REG && loffset < aop->size)
        transferRegReg (aop->aopu.aop_reg[loffset], hc08_reg_h, TRUE);
      else if (hc08_reg_a->isFree)
        {
          loadRegFromAop (hc08_reg_a, aop, loffset);
          transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
        }
      else if (hc08_reg_x->isFree)
        {
          loadRegFromAop (hc08_reg_x, aop, loffset);
          transferRegReg (hc08_reg_x, hc08_reg_h, TRUE);
        }
      else
        {
          pushReg (hc08_reg_a, TRUE);
          loadRegFromAop (hc08_reg_a, aop, loffset);
          transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
          pullReg (hc08_reg_a);
        }
      break;
    case HX_IDX:
      if (IS_AOP_HX (aop))
        break;
      else if (IS_AOP_XA (aop))
        transferRegReg (hc08_reg_xa, hc08_reg_hx, FALSE);
      else if ((aop->type == AOP_DIR))
        {
          if (aop->size > (loffset + 1))
            {
              emitcode ("ldhx", "%s", aopAdrStr (aop, loffset + 1, TRUE));
              regalloc_dry_run_cost += (aop->type == AOP_IMMD ? 3 : 2);
            }
          else
            {
              loadRegFromAop (hc08_reg_x, aop, loffset);
              loadRegFromConst (hc08_reg_h, zero);
            }
        }
      else if ((aop->type == AOP_LIT) || (aop->type == AOP_IMMD))
        {
          emitcode ("ldhx", "%s", aopAdrStr (aop, loffset, TRUE));
          regalloc_dry_run_cost += (aop->type == AOP_IMMD ? 3 : 2);
        }
      else
        {
          bool needpula;
          needpula = pushRegIfUsed (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, aop, loffset + 1);
          loadRegFromAop (hc08_reg_x, aop, loffset);
          transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
          pullOrFreeReg (hc08_reg_a, needpula);
        }
      break;
    case XA_IDX:
      if (IS_AOP_XA (aop))
        break;
      else if (IS_AOP_HX (aop))
        transferRegReg (hc08_reg_hx, hc08_reg_xa, FALSE);
      else
        {
          loadRegFromAop (hc08_reg_a, aop, loffset);
          loadRegFromAop (hc08_reg_x, aop, loffset + 1);
        }
      break;
    }

// ignore caching for now
#if 0
  reg->aop = aop;
  reg->aopofs = loffset;
#endif
}


/*--------------------------------------------------------------------------*/
/* forceStackedAop - Reserve space on the stack for asmop aop; when         */
/*                   freeAsmop is called with aop, the stacked data will    */
/*                   be copied to the original aop location.                */
/*--------------------------------------------------------------------------*/
static asmop *
forceStackedAop (asmop * aop, bool copyOrig)
{
  reg_info *reg;
  int loffset;
  asmop *newaop = newAsmop (aop->type);
  memcpy (newaop, aop, sizeof (*newaop));

  DD (emitcode ("", "; forcedStackAop %s", aopName (aop)));

  if (copyOrig && hc08_reg_a->isFree)
    reg = hc08_reg_a;
  else if (copyOrig && hc08_reg_x->isFree)
    reg = hc08_reg_x;
  else
    reg = NULL;

  for (loffset = 0; loffset < newaop->size; loffset++)
    {
      asmop *aopsof = newAsmop (AOP_SOF);
      aopsof->size = 1;
      if (copyOrig && reg)
        {
          loadRegFromAop (reg, aop, loffset);
          aopsof->aopu.aop_stk = pushReg (reg, FALSE);
        }
      else
        {
          aopsof->aopu.aop_stk = pushReg (hc08_reg_a, FALSE);
        }
      aopsof->op = aop->op;
      newaop->stk_aop[loffset] = aopsof;
    }
  newaop->stacked = 1;

  if (!reg && copyOrig)
    {
      for (loffset = 0; loffset < newaop->size; loffset++)
        {
          transferAopAop (aop, loffset, newaop, loffset);
        }
    }

  return newaop;
}


/*--------------------------------------------------------------------------*/
/* storeRegToAop - Store register reg to logical offset loffset of aop.     */
/*--------------------------------------------------------------------------*/
static void
storeRegToAop (reg_info * reg, asmop * aop, int loffset)
{
  int regidx = reg->rIdx;
#if 0
  regs *otherreg;
  int otheridx;
#endif

  DD (emitcode ("", ";     storeRegToAop (%s, %s, %d), stacked=%d, isaddr=%d",
                reg->name, aopName (aop), loffset, aop->stacked, aop->isaddr));

  if ((reg->rIdx == HX_IDX) && aop->stacked && (aop->stk_aop[loffset] || aop->stk_aop[loffset + 1]))
    {
      storeRegToAop (hc08_reg_h, aop, loffset + 1);
      storeRegToAop (hc08_reg_x, aop, loffset);
      return;
    }

  if ((reg->rIdx == XA_IDX) && aop->stacked && (aop->stk_aop[loffset] || aop->stk_aop[loffset + 1]))
    {
      storeRegToAop (hc08_reg_x, aop, loffset + 1);
      storeRegToAop (hc08_reg_a, aop, loffset);
      return;
    }

  if (aop->stacked && aop->stk_aop[loffset])
    {
      storeRegToAop (reg, aop->stk_aop[loffset], 0);
      return;
    }

  if (aop->type == AOP_STR)
    {
      if (loffset == 0)
        transferRegReg (reg, hc08_reg_x, FALSE);
      else if (loffset == 1)
        transferRegReg (reg, hc08_reg_h, FALSE);
      return;
    }

  if (aop->type == AOP_DUMMY)
    return;

  if (aop->type == AOP_CRY)     /* This can only happen if IFX was optimized */
    return;                     /* away, so just toss the result */

  switch (regidx)
    {
    case A_IDX:
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], FALSE);
      else
        {
          emitcode ("sta", "%s", aopAdrStr (aop, loffset, FALSE));
          regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 :3);
        }
      break;
    case X_IDX:
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], FALSE);
      else
        {
          emitcode ("stx", "%s", aopAdrStr (aop, loffset, FALSE));
          regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 :3);
        }
      break;
    case H_IDX:
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], FALSE);
      else if (hc08_reg_a->isFree)
        {
          transferRegReg (hc08_reg_h, hc08_reg_a, FALSE);
          storeRegToAop (hc08_reg_a, aop, loffset);
          hc08_freeReg (hc08_reg_a);
        }
      else if (hc08_reg_x->isFree)
        {
          transferRegReg (hc08_reg_h, hc08_reg_x, FALSE);
          storeRegToAop (hc08_reg_x, aop, loffset);
          hc08_freeReg (hc08_reg_x);
        }
      else
        {
          pushReg (hc08_reg_a, TRUE);
          transferRegReg (hc08_reg_h, hc08_reg_a, FALSE);
          storeRegToAop (hc08_reg_a, aop, loffset);
          pullReg (hc08_reg_a);
        }
      break;
    case HX_IDX:
      if ((aop->type == AOP_DIR))
        {
          emitcode ("sthx", "%s", aopAdrStr (aop, loffset + 1, TRUE));
          regalloc_dry_run_cost += 2;
        }
      else if (IS_AOP_XA (aop))
        transferRegReg (reg, hc08_reg_xa, FALSE);
      else if (IS_AOP_HX (aop))
        break;
      else
        {
          bool needpula;
          needpula = pushRegIfUsed (hc08_reg_a);
          transferRegReg (hc08_reg_h, hc08_reg_a, FALSE);
          storeRegToAop (hc08_reg_a, aop, loffset + 1);
          storeRegToAop (hc08_reg_x, aop, loffset);
          pullOrFreeReg (hc08_reg_a, needpula);
        }
      break;
    case XA_IDX:
      if (IS_AOP_HX (aop))
        transferRegReg (reg, hc08_reg_hx, FALSE);
      else if (IS_AOP_XA (aop))
        break;
      else
        {
          storeRegToAop (hc08_reg_a, aop, loffset);
          storeRegToAop (hc08_reg_x, aop, loffset + 1);
        }
      break;
    }

  /* Disable the register tracking for now */
#if 0
  //if (!reg->aop || (reg->aop && (reg->aop != aop)))
  {
    //if (reg->aop!=aop)
    for (otheridx = 0; otheridx < hc08_nRegs; otheridx++)
      {
        otherreg = hc08_regWithIdx (otheridx);
        if (otherreg && otherreg->aop
            && otherreg->aop->op && aop->op && operandsEqu (otherreg->aop->op, aop->op) && (otherreg->aopofs == loffset))
          {
            DD (emitcode ("", "; marking %s stale", otherreg->name));
            otherreg->aop = NULL;
          }
      }
    if ((!hc08_reg_x->aop || !hc08_reg_h->aop) && hc08_reg_hx->aop)
      {
        hc08_reg_hx->aop = NULL;
        DD (emitcode ("", "; marking hx stale"));
      }
    if ((!hc08_reg_x->aop || !hc08_reg_a->aop) && hc08_reg_xa->aop)
      {
        hc08_reg_xa->aop = NULL;
        DD (emitcode ("", "; marking xa stale"));
      }

    reg->aop = aop;
    reg->aopofs = loffset;
  }
#endif
}

/*--------------------------------------------------------------------------*/
/* loadRegFromConst - Load register reg from constant c.                    */
/*--------------------------------------------------------------------------*/
static void
loadRegFromConst (reg_info * reg, char *c)
{
  switch (reg->rIdx)
    {
    case A_IDX:
      if (!strcmp (c, zero))
        {
          emitcode ("clra", "");
          regalloc_dry_run_cost++;
        }
      else
        {
          emitcode ("lda", "%s", c);
          regalloc_dry_run_cost += 2;
        }
      break;
    case X_IDX:
      if (!strcmp (c, zero))
        {
          emitcode ("clrx", "");
          regalloc_dry_run_cost++;
        }
      else
        {
          emitcode ("ldx", "%s", c);
          regalloc_dry_run_cost += 2;
        }
      break;
    case H_IDX:
      if (!strcmp (c, zero))
        {
          emitcode ("clrh", "");
          regalloc_dry_run_cost++;
        }
      else if (hc08_reg_a->isFree)
        {
          loadRegFromConst (hc08_reg_a, c);
          transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
        }
      else if (hc08_reg_x->isFree)
        {
          loadRegFromConst (hc08_reg_x, c);
          transferRegReg (hc08_reg_x, hc08_reg_h, TRUE);
        }
      else
        {
          pushReg (hc08_reg_a, TRUE);
          loadRegFromConst (hc08_reg_a, c);
          transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
          pullReg (hc08_reg_a);
        }
      break;
    case HX_IDX:
      emitcode ("ldhx", "%s", c);
      regalloc_dry_run_cost += 3;
      break;
    case XA_IDX:
      emitcode ("lda", "%s", c);
      emitcode ("ldx", "%s >> 8", c);
      regalloc_dry_run_cost += 4;
      break;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in loadRegFromConst");
      return;
    }
  hc08_useReg (reg);
}


/*--------------------------------------------------------------------------*/
/* storeConstToAop- Store constant c to logical offset loffset of asmop aop.*/
/*--------------------------------------------------------------------------*/
static void
storeConstToAop (char *c, asmop * aop, int loffset)
{
  if (aop->stacked && aop->stk_aop[loffset])
    {
      storeConstToAop (c, aop->stk_aop[loffset], 0);
      return;
    }

  switch (aop->type)
    {
    case AOP_DIR:
      if (!strcmp (c, zero))
        {
          emitcode ("clr", "%s", aopAdrStr (aop, loffset, FALSE));
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("mov", "%s,%s", c, aopAdrStr (aop, loffset, FALSE));
          regalloc_dry_run_cost += 3;
        }
      break;
    case AOP_REG:
      if (loffset > (aop->size - 1))
        break;
      loadRegFromConst (aop->aopu.aop_reg[loffset], c);
      break;
    case AOP_DUMMY:
      break;
    default:
      if (hc08_reg_a->isFree)
        {
          loadRegFromConst (hc08_reg_a, c);
          storeRegToAop (hc08_reg_a, aop, loffset);
          hc08_freeReg (hc08_reg_a);
        }
      else if (hc08_reg_x->isFree)
        {
          loadRegFromConst (hc08_reg_x, c);
          storeRegToAop (hc08_reg_x, aop, loffset);
          hc08_freeReg (hc08_reg_x);
        }
      else
        {
          pushReg (hc08_reg_a, TRUE);
          loadRegFromConst (hc08_reg_a, c);
          storeRegToAop (hc08_reg_a, aop, loffset);
          pullReg (hc08_reg_a);
        }
    }
}


/*--------------------------------------------------------------------------*/
/* storeRegSignToUpperAop - If isSigned is true, the sign bit of register   */
/*                          reg is extended to fill logical offsets loffset */
/*                          and above of asmop aop. Otherwise, logical      */
/*                          offsets loffset and above of asmop aop are      */
/*                          zeroed. reg must be an 8-bit register.          */
/*--------------------------------------------------------------------------*/
static void
storeRegSignToUpperAop (reg_info * reg, asmop * aop, int loffset, bool isSigned)
{
//  int regidx = reg->rIdx;
  int size = aop->size;

  if (size <= loffset)
    return;

  if (!isSigned)
    {
      /* Unsigned case */
      while (loffset < size)
        storeConstToAop (zero, aop, loffset++);
    }
  else
    {
      /* Signed case */
      transferRegReg (reg, hc08_reg_a, FALSE);
      emitcode ("rola", "");
      emitcode ("clra", "");
      emitcode ("sbc", "#0");
      regalloc_dry_run_cost += 4;
      hc08_useReg (hc08_reg_a);
      while (loffset < size)
        storeRegToAop (hc08_reg_a, aop, loffset++);
      hc08_freeReg (hc08_reg_a);
    }
}

/*--------------------------------------------------------------------------*/
/* storeRegToFullAop - Store register reg to asmop aop with appropriate     */
/*                     padding and/or truncation as needed. If isSigned is  */
/*                     true, sign extension will take place in the padding. */
/*--------------------------------------------------------------------------*/
static void
storeRegToFullAop (reg_info *reg, asmop *aop, bool isSigned)
{
  int regidx = reg->rIdx;
  int size = aop->size;

  switch (regidx)
    {
    case A_IDX:
    case X_IDX:
    case H_IDX:
      storeRegToAop (reg, aop, 0);
      if (size > 1 && isSigned && aop->type == AOP_REG && aop->aopu.aop_reg[0]->rIdx == A_IDX)
        pushReg (hc08_reg_a, TRUE);
      storeRegSignToUpperAop (reg, aop, 1, isSigned);
      if (size > 1 && isSigned && aop->type == AOP_REG && aop->aopu.aop_reg[0]->rIdx == A_IDX)
        pullReg (hc08_reg_a);
      break;
    case HX_IDX:
      if (size == 1)
        {
          storeRegToAop (hc08_reg_x, aop, 0);
        }
      else
        {
          storeRegToAop (reg, aop, 0);
          storeRegSignToUpperAop (hc08_reg_h, aop, 2, isSigned);
        }
      break;
    case XA_IDX:
      if (size == 1)
        {
          storeRegToAop (hc08_reg_a, aop, 0);
        }
      else
        {
          storeRegToAop (reg, aop, 0);
          storeRegSignToUpperAop (hc08_reg_x, aop, 2, isSigned);
        }
      break;
    default:
      wassert (0);
    }
}

/*--------------------------------------------------------------------------*/
/* transferAopAop - Transfer the value at logical offset srcofs of asmop    */
/*                  srcaop to logical offset dstofs of asmop dstaop.        */
/*--------------------------------------------------------------------------*/
static void
transferAopAop (asmop *srcaop, int srcofs, asmop *dstaop, int dstofs)
{
  bool needpula = FALSE;
  reg_info *reg = NULL;
  int regIdx;
  bool keepreg = FALSE;
  bool afree;

  wassert (srcaop && dstaop);

  /* ignore transfers at the same byte, unless its volatile */
  if (srcaop->op && !isOperandVolatile (srcaop->op, FALSE)
      && dstaop->op && !isOperandVolatile (dstaop->op, FALSE)
      && operandsEqu (srcaop->op, dstaop->op) && srcofs == dstofs && dstaop->type == srcaop->type)
    return;

  if (srcaop->stacked && srcaop->stk_aop[srcofs])
    {
      transferAopAop (srcaop->stk_aop[srcofs], 0, dstaop, dstofs);
      return;
    }

  if (dstaop->stacked && dstaop->stk_aop[srcofs])
    {
      transferAopAop (srcaop, srcofs, dstaop->stk_aop[dstofs], 0);
      return;
    }

//  DD(emitcode ("", "; transferAopAop (%s, %d, %s, %d)",
//            aopName (srcaop), srcofs, aopName (dstaop), dstofs));
//  DD(emitcode ("", "; srcaop->type = %d", srcaop->type));
//  DD(emitcode ("", "; dstaop->type = %d", dstaop->type));

  if (dstofs >= dstaop->size)
    return;

  if ((dstaop->type == AOP_DIR) && ((srcaop->type == AOP_DIR) || (srcaop->type == AOP_LIT)))
    {
      char *src = aopAdrStr (srcaop, srcofs, FALSE);
      if (!strcmp (src, zero))
        {
          emitcode ("clr", "%s", aopAdrStr (dstaop, dstofs, FALSE));
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("mov", "%s,%s", src, aopAdrStr (dstaop, dstofs, FALSE));
          regalloc_dry_run_cost += 3;
        }
      return;
    }

  if (dstaop->type == AOP_REG)
    {
      regIdx = dstaop->aopu.aop_reg[dstofs]->rIdx;
      reg = dstaop->aopu.aop_reg[dstofs];
      keepreg = TRUE;
    }
  else if ((srcaop->type == AOP_REG) && (srcaop->aopu.aop_reg[srcofs]))
    {
      regIdx = srcaop->aopu.aop_reg[srcofs]->rIdx;
      reg = srcaop->aopu.aop_reg[srcofs];
      keepreg = TRUE;
    }

  afree = hc08_reg_a->isFree;

  if (!reg)
    {
      if (hc08_reg_a->isFree)
        reg = hc08_reg_a;
      else if (hc08_reg_x->isFree)
        reg = hc08_reg_x;
      else
        {
          pushReg (hc08_reg_a, TRUE);
          needpula = TRUE;
          reg = hc08_reg_a;
        }
    }

  loadRegFromAop (reg, srcaop, srcofs);
  storeRegToAop (reg, dstaop, dstofs);

  if (!keepreg)
    pullOrFreeReg (hc08_reg_a, needpula);

  hc08_reg_a->isFree = afree;
}


/*--------------------------------------------------------------------------*/
/* accopWithMisc - Emit accumulator modifying instruction accop with the    */
/*                 parameter param.                                         */
/*--------------------------------------------------------------------------*/
static void
accopWithMisc (char *accop, char *param)
{
  emitcode (accop, "%s", param);
  regalloc_dry_run_cost += ((!param[0] || !strcmp(param, ",x")) ? 1 : 2);
  hc08_dirtyReg (hc08_reg_a, FALSE);
}

/*--------------------------------------------------------------------------*/
/* accopWithAop - Emit accumulator modifying instruction accop with the     */
/*                byte at logical offset loffset of asmop aop.              */
/*                Supports: adc, add, and, bit, cmp, eor, ora, sbc, sub     */
/*--------------------------------------------------------------------------*/
static void
accopWithAop (char *accop, asmop *aop, int loffset)
{
  if (aop->stacked && aop->stk_aop[loffset])
    {
      accopWithAop (accop, aop->stk_aop[loffset], 0);
      return;
    }

  if (aop->type == AOP_DUMMY)
    return;

  if (loffset >= aop->size)
    {
      emitcode (accop, "#0");
      regalloc_dry_run_cost += 2;
    }
  else if (aop->type == AOP_REG)
    {
      pushReg (aop->aopu.aop_reg[loffset], FALSE);
      emitcode (accop, "1,s");
      regalloc_dry_run_cost += 3;
      pullNull (1);
    }
  else
    {
      emitcode (accop, "%s", aopAdrStr (aop, loffset, FALSE));
      regalloc_dry_run_cost += 3;
    }

  hc08_dirtyReg (hc08_reg_a, FALSE);
}


/*--------------------------------------------------------------------------*/
/* rmwWithReg - Emit read/modify/write instruction rmwop with register reg. */
/*              byte at logical offset loffset of asmop aop. Register reg   */
/*              must be 8-bit.                                              */
/*              Supports: com, dec, inc, lsl, lsr, neg, rol, ror            */
/*--------------------------------------------------------------------------*/
static void
rmwWithReg (char *rmwop, reg_info * reg)
{
  char rmwbuf[10];
  char *rmwaop = rmwbuf;

  if (reg->rIdx == A_IDX)
    {
      sprintf (rmwaop, "%sa", rmwop);
      emitcode (rmwaop, "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
    }
  else if (reg->rIdx == X_IDX)
    {
      sprintf (rmwaop, "%sx", rmwop);
      emitcode (rmwaop, "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
    }
  else if (hc08_reg_a->isFree)
    {
      transferRegReg (reg, hc08_reg_a, FALSE);
      sprintf (rmwaop, "%sa", rmwop);
      emitcode (rmwaop, "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      transferRegReg (hc08_reg_a, reg, TRUE);
    }
  else
    {
      pushReg (reg, FALSE);
      emitcode (rmwop, "1,s");
      regalloc_dry_run_cost += 3;
      pullReg (reg);
      hc08_dirtyReg (reg, FALSE);
    }
}

/*--------------------------------------------------------------------------*/
/* rmwWithAop - Emit read/modify/write instruction rmwop with the byte at   */
/*                logical offset loffset of asmop aop.                      */
/*                Supports: com, dec, inc, lsl, lsr, neg, rol, ror, tst     */
/*--------------------------------------------------------------------------*/
static void
rmwWithAop (char *rmwop, asmop * aop, int loffset)
{
  bool needpula = FALSE;

  if (aop->stacked && aop->stk_aop[loffset])
    {
      rmwWithAop (rmwop, aop->stk_aop[loffset], 0);
      return;
    }

  switch (aop->type)
    {
    case AOP_REG:
      rmwWithReg (rmwop, aop->aopu.aop_reg[loffset]);
      break;
    case AOP_EXT:
      needpula = pushRegIfUsed (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, aop, loffset);
      rmwWithReg (rmwop, hc08_reg_a);
      if (strcmp ("tst", rmwop))
        storeRegToAop (hc08_reg_a, aop, loffset);
      pullOrFreeReg (hc08_reg_a, needpula);
      break;
    case AOP_DUMMY:
      break;
    default:
      emitcode (rmwop, "%s", aopAdrStr (aop, loffset, FALSE));
      regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 : 3);
    }

}


/*-----------------------------------------------------------------*/
/* newAsmop - creates a new asmOp                                  */
/*-----------------------------------------------------------------*/
static asmop *
newAsmop (short type)
{
  asmop *aop;

  aop = Safe_calloc (1, sizeof (asmop));
  aop->type = type;
  aop->op = NULL;
  return aop;
}

#if 0
/*-----------------------------------------------------------------*/
/* pointerCode - returns the code for a pointer type               */
/*-----------------------------------------------------------------*/
static int
pointerCode (sym_link * etype)
{

  return PTR_TYPE (SPEC_OCLS (etype));

}
#endif

/*-----------------------------------------------------------------*/
/* aopForSym - for a true symbol                                   */
/*-----------------------------------------------------------------*/
static asmop *
aopForSym (iCode * ic, symbol * sym, bool result)
{
  asmop *aop;
  memmap *space;

  wassertl (ic != NULL, "Got a null iCode");
  wassertl (sym != NULL, "Got a null symbol");

//  printf("in aopForSym for symbol %s\n", sym->name);

  space = SPEC_OCLS (sym->etype);

  /* if already has one */
  if (sym->aop)
    {
      return sym->aop;
    }

  /* special case for a function */
  if (IS_FUNC (sym->type))
    {
      sym->aop = aop = newAsmop (AOP_IMMD);
      aop->aopu.aop_immd.aop_immd1 = Safe_calloc (1, strlen (sym->rname) + 1);
      strcpy (aop->aopu.aop_immd.aop_immd1, sym->rname);
      aop->size = FPTRSIZE;
      return aop;
    }

  /* if it is on the stack */
  if (sym->onStack)
    {
      sym->aop = aop = newAsmop (AOP_SOF);
      aop->aopu.aop_dir = sym->rname;
      aop->size = getSize (sym->type);
      aop->aopu.aop_stk = sym->stack;
      return aop;
    }

  /* if it is in direct space */
  if (IN_DIRSPACE (space))
    {
      sym->aop = aop = newAsmop (AOP_DIR);
      aop->aopu.aop_dir = sym->rname;
      aop->size = getSize (sym->type);
      return aop;
    }

  /* if it is in far space */
  if (IN_FARSPACE (space))
    {
      sym->aop = aop = newAsmop (AOP_EXT);
      aop->aopu.aop_dir = sym->rname;
      aop->size = getSize (sym->type);
      return aop;
    }



  werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "aopForSym should never reach here");

  exit (1);

  /* if it is in code space */
  if (IN_CODESPACE (space))
    aop->code = 1;

  return aop;
}

/*-----------------------------------------------------------------*/
/* aopForRemat - rematerializes an object                          */
/*-----------------------------------------------------------------*/
static asmop *
aopForRemat (symbol * sym)
{
  iCode *ic = sym->rematiCode;
  asmop *aop = NULL;
  int ptr_type = 0;
  int val = 0;

  for (;;)
    {
      if (ic->op == '+')
        val += (int) operandLitValue (IC_RIGHT (ic));
      else if (ic->op == '-')
        val -= (int) operandLitValue (IC_RIGHT (ic));
      else if (IS_CAST_ICODE (ic))
        {
          sym_link *from_type = operandType (IC_RIGHT (ic));
          ic = OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
          ptr_type = DCL_TYPE (from_type);
          if (ptr_type == IPOINTER)
            {
              // bug #481053
              ptr_type = POINTER;
            }
          continue;
        }
      else
        break;

      ic = OP_SYMBOL (IC_LEFT (ic))->rematiCode;
    }

  if (ic->op == ADDRESS_OF)
    {
      if (val)
        {
          SNPRINTF (buffer, sizeof (buffer),
                    "(%s %c 0x%04x)", OP_SYMBOL (IC_LEFT (ic))->rname, val >= 0 ? '+' : '-', abs (val) & 0xffff);
        }
      else
        {
          strncpyz (buffer, OP_SYMBOL (IC_LEFT (ic))->rname, sizeof (buffer));
        }

      aop = newAsmop (AOP_IMMD);
      aop->aopu.aop_immd.aop_immd1 = Safe_strdup (buffer);
      /* set immd2 field if required */
    }
  else if (ic->op == '=')
    {
      val += (int) operandLitValue (IC_RIGHT (ic));
      val &= 0xffff;
      SNPRINTF (buffer, sizeof (buffer), "0x%04x", val);
      aop = newAsmop (AOP_LIT);
      aop->aopu.aop_lit = constVal (buffer);
    }
  else
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "unexpected rematerialization");
    }

  return aop;
}

/*-----------------------------------------------------------------*/
/* regsInCommon - two operands have some registers in common       */
/*-----------------------------------------------------------------*/
static bool
regsInCommon (operand * op1, operand * op2)
{
  symbol *sym1, *sym2;
  int i;

  /* if they have registers in common */
  if (!IS_SYMOP (op1) || !IS_SYMOP (op2))
    return FALSE;

  sym1 = OP_SYMBOL (op1);
  sym2 = OP_SYMBOL (op2);

  if (sym1->nRegs == 0 || sym2->nRegs == 0)
    return FALSE;

  for (i = 0; i < sym1->nRegs; i++)
    {
      int j;
      if (!sym1->regs[i])
        continue;

      for (j = 0; j < sym2->nRegs; j++)
        {
          if (!sym2->regs[j])
            continue;

          if (sym2->regs[j] == sym1->regs[i])
            return TRUE;
        }
    }

  return FALSE;
}

/*-----------------------------------------------------------------*/
/* operandsEqu - equivalent                                        */
/*-----------------------------------------------------------------*/
static bool
operandsEqu (operand * op1, operand * op2)
{
  symbol *sym1, *sym2;

  /* if they not symbols */
  if (!IS_SYMOP (op1) || !IS_SYMOP (op2))
    return FALSE;

  sym1 = OP_SYMBOL (op1);
  sym2 = OP_SYMBOL (op2);

  /* if both are itemps & one is spilt
     and the other is not then false */
  if (IS_ITEMP (op1) && IS_ITEMP (op2) && sym1->isspilt != sym2->isspilt)
    return FALSE;

  /* if they are the same */
  if (sym1 == sym2)
    return TRUE;

  /* if they have the same rname */
  if (sym1->rname[0] && sym2->rname[0] && strcmp (sym1->rname, sym2->rname) == 0)
    return TRUE;

  /* if left is a tmp & right is not */
  if (IS_ITEMP (op1) && !IS_ITEMP (op2) && sym1->isspilt && (sym1->usl.spillLoc == sym2))
    return TRUE;

  if (IS_ITEMP (op2) && !IS_ITEMP (op1) && sym2->isspilt && sym1->level > 0 && (sym2->usl.spillLoc == sym1))
    return TRUE;

  return FALSE;
}

/*-----------------------------------------------------------------*/
/* sameRegs - two asmops have the same registers                   */
/*-----------------------------------------------------------------*/
static bool
sameRegs (asmop * aop1, asmop * aop2)
{
  int i;

  if (aop1 == aop2)
    return TRUE;

//  if (aop1->size != aop2->size)
//    return FALSE;

  if (aop1->type == aop2->type)
    {
      switch (aop1->type)
        {
        case AOP_REG:
          for (i = 0; i < aop1->size; i++)
            if (aop1->aopu.aop_reg[i] != aop2->aopu.aop_reg[i])
              return FALSE;
          return TRUE;
        case AOP_SOF:
          return (aop1->aopu.aop_stk == aop2->aopu.aop_stk);
        case AOP_DIR:
        case AOP_EXT:
          return (!strcmp (aop1->aopu.aop_dir, aop2->aopu.aop_dir));
        }
    }

  return FALSE;
}

/*-----------------------------------------------------------------*/
/* aopOp - allocates an asmop for an operand  :                    */
/*-----------------------------------------------------------------*/
static void
aopOp (operand *op, iCode * ic, bool result)
{
  asmop *aop = NULL;
  symbol *sym;
  int i;

  if (!op)
    return;

  // Is this a pointer set result?
  //
  if ((op == IC_RESULT (ic)) && POINTER_SET (ic))
    {
    }

//  printf("checking literal\n");
  /* if this a literal */
  if (IS_OP_LITERAL (op))
    {
      op->aop = aop = newAsmop (AOP_LIT);
      aop->aopu.aop_lit = OP_VALUE (op);
      aop->size = getSize (operandType (op));
      aop->op = op;
      aop->isaddr = op->isaddr;
      return;
    }

//  printf("checking pre-existing\n");
  /* if already has a asmop then continue */
  if (op->aop)
    {
      op->aop->op = op;
      op->aop->isaddr = op->isaddr;
      return;
    }

//  printf("checking underlying sym\n");
  /* if the underlying symbol has a aop */
  if (IS_SYMOP (op) && OP_SYMBOL (op)->aop)
    {
      op->aop = aop = Safe_calloc (1, sizeof (*aop));
      memcpy (aop, OP_SYMBOL (op)->aop, sizeof (*aop));
      //op->aop = aop = OP_SYMBOL (op)->aop;
      aop->size = getSize (operandType (op));
      //printf ("reusing underlying symbol %s\n",OP_SYMBOL (op)->name);
      //printf (" with size = %d\n", aop->size);

      aop->op = op;
      aop->isaddr = op->isaddr;
      /* if (aop->isaddr & IS_ITEMP (op))
         {
         aop->psize=aop->size;
         aop->size = getSize( operandType (op)->next);
         } */
      return;
    }

//  printf("checking true sym\n");
  /* if this is a true symbol */
  if (IS_TRUE_SYMOP (op))
    {
      op->aop = aop = aopForSym (ic, OP_SYMBOL (op), result);
      aop->op = op;
      aop->isaddr = op->isaddr;
      //printf ("new symbol %s\n", OP_SYMBOL (op)->name);
      //printf (" with size = %d\n", aop->size);
      return;
    }

  /* this is a temporary : this has
     only five choices :
     a) register
     b) spillocation
     c) rematerialize
     d) conditional
     e) can be a return use only */

  sym = OP_SYMBOL (op);

//  printf("checking conditional\n");
  /* if the type is a conditional */
  if (sym->regType == REG_CND)
    {
      sym->aop = op->aop = aop = newAsmop (AOP_CRY);
      aop->size = 0;
      aop->op = op;
      aop->isaddr = op->isaddr;
      return;
    }

//  printf("checking spilt\n");
  /* if it is spilt then two situations
     a) is rematerialize
     b) has a spill location */
  if (sym->isspilt || sym->nRegs == 0)
    {
//      printf("checking remat\n");
      /* rematerialize it NOW */
      if (sym->remat)
        {
          sym->aop = op->aop = aop = aopForRemat (sym);
          aop->size = getSize (sym->type);
          aop->op = op;
          aop->isaddr = op->isaddr;
          /* if (aop->isaddr & IS_ITEMP (op))
             {
             aop->psize=aop->size;
             aop->size = getSize( operandType (op)->next);
             } */
          return;
        }

//      printf("checking accuse\n");
      if (sym->accuse)
        {
          sym->aop = op->aop = aop = newAsmop (AOP_REG);
          aop->size = getSize (sym->type);
          switch (sym->accuse)
            {
            case ACCUSE_XA:
              aop->aopu.aop_reg[0] = hc08_reg_a;
              aop->aopu.aop_reg[1] = hc08_reg_x;
              break;
            case ACCUSE_HX:
              aop->aopu.aop_reg[0] = hc08_reg_x;
              aop->aopu.aop_reg[1] = hc08_reg_h;
              break;
            default:
              wassert (0);
            }
          aop->op = op;
          aop->isaddr = op->isaddr;
          return;
        }

//      printf("checking ruonly\n");
#if 1
      if (sym->ruonly)
        {
          unsigned i;

          sym->aop = op->aop = aop = newAsmop (AOP_STR);
          aop->size = getSize (sym->type);
          for (i = 0; i < fReturnSizeHC08; i++)
            aop->aopu.aop_str[i] = fReturn2[i];
          aop->op = op;
          aop->isaddr = op->isaddr;
          return;
        }
#endif
      /* else spill location  */
      if (sym->usl.spillLoc)
        {
          asmop *oldAsmOp = NULL;

          if (sym->usl.spillLoc->aop && sym->usl.spillLoc->aop->size != getSize (sym->type))
            {
              /* force a new aop if sizes differ */
              oldAsmOp = sym->usl.spillLoc->aop;
              sym->usl.spillLoc->aop = NULL;
              //printf ("forcing new aop\n");
            }
          sym->aop = op->aop = aop = aopForSym (ic, sym->usl.spillLoc, result);
          if (sym->usl.spillLoc->aop->size != getSize (sym->type))
            {
              /* Don't reuse the new aop, go with the last one */
              sym->usl.spillLoc->aop = oldAsmOp;
            }
          aop->size = getSize (sym->type);
          aop->op = op;
          aop->isaddr = op->isaddr;
          //printf ("spill symbol %s\n", OP_SYMBOL (op)->name);
          //printf (" with size = %d\n", aop->size);
          return;
        }

      /* else must be a dummy iTemp */
      sym->aop = op->aop = aop = newAsmop (AOP_DUMMY);
      aop->size = getSize (sym->type);
      aop->op = op;
      return;
    }

//  printf("assuming register\n");
  /* must be in a register */
  wassert (sym->nRegs);
  sym->aop = op->aop = aop = newAsmop (AOP_REG);
  aop->size = sym->nRegs;
  for (i = 0; i < sym->nRegs; i++)
    {
       wassert (sym->regs[i] < regshc08 + 3);
       aop->aopu.aop_reg[i] = sym->regs[i];
    }
  aop->op = op;
  aop->isaddr = op->isaddr;
}

/*-----------------------------------------------------------------*/
/* freeAsmop - free up the asmop given to an operand               */
/*----------------------------------------------------------------*/
static void
freeAsmop (operand * op, asmop * aaop, iCode * ic, bool pop)
{
  asmop *aop;

  if (!op)
    aop = aaop;
  else
    aop = op->aop;

  if (!aop)
    return;

  if (aop->freed)
    goto dealloc;

  aop->freed = 1;

  if (aop->stacked)
    {
      int stackAdjust;
      int loffset;

      DD (emitcode ("", "; freeAsmop restoring stacked %s", aopName (aop)));
      aop->stacked = 0;
      stackAdjust = 0;
      for (loffset = 0; loffset < aop->size; loffset++)
        if (aop->stk_aop[loffset])
          {
            transferAopAop (aop->stk_aop[loffset], 0, aop, loffset);
            stackAdjust++;
          }
      pullNull (stackAdjust);
    }

dealloc:
  /* all other cases just dealloc */
  if (op)
    {
      op->aop = NULL;
      if (IS_SYMOP (op))
        {
          OP_SYMBOL (op)->aop = NULL;
          /* if the symbol has a spill */
          if (SPIL_LOC (op))
            SPIL_LOC (op)->aop = NULL;
        }
    }
}


/*-----------------------------------------------------------------*/
/* aopDerefAop - treating the aop parameter as a pointer, return   */
/*               an asmop for the object it references             */
/*-----------------------------------------------------------------*/
asmop *
aopDerefAop (asmop * aop)
{
  int adr;
  char *s = buffer;
  char *rs;
  asmop *newaop = NULL;
  sym_link *type, *etype;
  int p_type;

  DD (emitcode ("", ";     aopDerefAop(%s)", aopName (aop)));
  if (aop->op)
    {

      type = operandType (aop->op);
      etype = getSpec (type);
      /* if op is of type of pointer then it is simple */
      if (IS_PTR (type) && !IS_FUNC (type->next))
        p_type = DCL_TYPE (type);
      else
        {
          /* we have to go by the storage class */
          p_type = PTR_TYPE (SPEC_OCLS (etype));
        }
    }
  else
    p_type = UPOINTER;

  switch (aop->type)
    {
    case AOP_IMMD:
      if (p_type == POINTER)
        newaop = newAsmop (AOP_DIR);
      else
        newaop = newAsmop (AOP_EXT);
      newaop->aopu.aop_dir = aop->aopu.aop_immd.aop_immd1;
      break;
    case AOP_LIT:
      adr = (int) ulFromVal (aop->aopu.aop_lit);
      if (p_type == POINTER)
        adr &= 0xff;

      if (adr < 0x100)
        {
          newaop = newAsmop (AOP_DIR);
          sprintf (s, "0x%02x", adr);
        }
      else
        {
          newaop = newAsmop (AOP_EXT);
          sprintf (s, "0x%04x", adr);
        }
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      newaop->aopu.aop_dir = rs;
      break;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "unsupported asmop");
      return NULL;
    }


  return newaop;
}



/*-----------------------------------------------------------------*/
/* aopAdrStr - for referencing the address of the aop              */
/*-----------------------------------------------------------------*/
static char *
aopAdrStr (asmop * aop, int loffset, bool bit16)
{
  char *s = buffer;
  char *rs;
  int offset = aop->size - 1 - loffset;

  /* offset is greater than
     size then zero */
  if (loffset > (aop->size - 1) && aop->type != AOP_LIT)
    return zero;

  /* depending on type */
  switch (aop->type)
    {
    case AOP_DUMMY:
      return zero;

    case AOP_IMMD:
      if (aop->aopu.aop_immd.from_cast_remat && (loffset == (aop->size - 1)))
        {
          sprintf (s, "%s", aop->aopu.aop_immd.aop_immd2);
        }
      else if (bit16)
        {
          sprintf (s, "#%s", aop->aopu.aop_immd.aop_immd1);
        }
      else if (loffset)
        {
          if (loffset != 1)
            sprintf (s, "#(%s >> %d)", aop->aopu.aop_immd.aop_immd1, loffset * 8);
          else
            sprintf (s, "#>%s", aop->aopu.aop_immd.aop_immd1);
        }
      else
        sprintf (s, "#%s", aop->aopu.aop_immd.aop_immd1);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_DIR:
      if (offset)
        sprintf (s, "*(%s + %d)", aop->aopu.aop_dir, offset);
      else
        sprintf (s, "*%s", aop->aopu.aop_dir);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_EXT:
      if (offset)
        sprintf (s, "(%s + %d)", aop->aopu.aop_dir, offset);
      else
        sprintf (s, "%s", aop->aopu.aop_dir);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_REG:
      return aop->aopu.aop_reg[loffset]->name;

    case AOP_LIT:
      if (bit16)
        return aopLiteralLong (aop->aopu.aop_lit, /*loffset */ 0, 2);
      else
        return aopLiteral (aop->aopu.aop_lit, loffset);

    case AOP_STR:
      aop->coff = offset;
      return aop->aopu.aop_str[loffset];

    case AOP_SOF:
      sprintf (s, "%d,s", _G.stackOfs + _G.stackPushes + aop->aopu.aop_stk + offset + 1);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;
    }

  werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "aopAdrStr got unsupported aop->type");
  exit (1);
}





#if 0
/*-----------------------------------------------------------------*/
/* opIsGptr: returns non-zero if the passed operand is       */
/* a generic pointer type.             */
/*-----------------------------------------------------------------*/
static int
opIsGptr (operand * op)
{
  sym_link *type = operandType (op);

  if ((AOP_SIZE (op) == GPTRSIZE) && IS_GENPTR (type))
    {
      return 1;
    }
  return 0;
}
#endif

/*-----------------------------------------------------------------*/
/* getDataSize - get the operand data size                         */
/*-----------------------------------------------------------------*/
static int
getDataSize (operand *op)
{
  int size;
  size = AOP_SIZE (op);
  return size;
}


/*-----------------------------------------------------------------*/
/* asmopToBool - Emit code to convert an asmop to a boolean.       */
/*               Result left in A (0=FALSE, 1=TRUE) if ResultInA,  */
/*               otherwise result left in Z flag (1=FALSE, 0=TRUE) */
/*-----------------------------------------------------------------*/
static void
asmopToBool (asmop *aop, bool resultInA)
{
  bool isFloat; 
  symbol *tlbl, *tlbl1;
  int size = aop->size;
  bool needpula = FALSE;
  bool flagsonly = TRUE;
  int offset = size - 1;

  wassert (aop);
  isFloat = IS_FLOAT (operandType (AOP_OP (aop)));

  if (resultInA)
    hc08_freeReg (hc08_reg_a);

  switch (aop->type)
    {
    case AOP_REG:
      if (IS_AOP_A (aop))
        {
          emitcode ("tsta", "");
          regalloc_dry_run_cost++;
          flagsonly = FALSE;
        }
      else if (IS_AOP_X (aop))
        {
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
        }
      else if (IS_AOP_H (aop))
        {
          if (hc08_reg_a->isFree)
            {
              transferRegReg (hc08_reg_h, hc08_reg_a, FALSE);
              emitcode ("tsta", "");
              regalloc_dry_run_cost++;
              flagsonly = FALSE;
              hc08_freeReg (hc08_reg_a);
            }
          else if (hc08_reg_x->isFree)
            {
              transferRegReg (hc08_reg_h, hc08_reg_x, FALSE);
              emitcode ("tstx", "");
              hc08_freeReg (hc08_reg_x);
            }
          else
            {
              emitcode ("pshh", "");
              emitcode ("tst", "1,s");
              emitcode ("ais", "#1");
              regalloc_dry_run_cost += 6;
            }
        }
      else if (IS_AOP_HX (aop))
        {
          emitcode ("cphx", zero);
        }
      else if (IS_AOP_XA (aop))
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tsta", "");
          if (!regalloc_dry_run)
            emitcode ("bne", "%05d$", labelKey2num (tlbl->key));
          emitcode ("tstx", "");
          regalloc_dry_run_cost += 4;
          if (!regalloc_dry_run)
            emitLabel (tlbl);
        }
      else
        {
          werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in asmopToBool");
          return;
        }
      break;
    case AOP_EXT:
      if (resultInA)
        needpula = FALSE;
      else
        needpula = pushRegIfUsed (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, aop, offset--);
      if (isFloat)
        {
          emitcode ("and", "#0x7F");      //clear sign bit
          regalloc_dry_run_cost += 2;
        }
      while (--size)
        accopWithAop ("ora", aop, offset--);
      if (needpula)
        pullReg (hc08_reg_a);
      else
        {
          hc08_freeReg (hc08_reg_a);
          flagsonly = FALSE;
        }
      break;
    case AOP_LIT:
      /* Higher levels should optimize this case away but let's be safe */
      if (ulFromVal (aop->aopu.aop_lit))
        loadRegFromConst (hc08_reg_a, one);
      else
        loadRegFromConst (hc08_reg_a, zero);
      hc08_freeReg (hc08_reg_a);
      break;
    default:
      if (size == 1)
        {
          if (resultInA)
            {
              loadRegFromAop (hc08_reg_a, aop, 0);
              hc08_freeReg (hc08_reg_a);
              flagsonly = FALSE;
            }
          else
            {
              emitcode ("tst", "%s", aopAdrStr (aop, 0, FALSE));
              regalloc_dry_run_cost += ((aop->type == AOP_DIR || aop->type == AOP_IMMD) ? 2 : 3);
            }
          break;
        }
      else if (size == 2)
        {
          if (hc08_reg_a->isFree)
            {
              loadRegFromAop (hc08_reg_a, aop, 0);
              accopWithAop ("ora", aop, 1);
              hc08_freeReg (hc08_reg_a);
              flagsonly = FALSE;
            }
          else
            {
              tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
              emitcode ("tst", "%s", aopAdrStr (aop, 0, FALSE));
              if (!regalloc_dry_run)
                emitcode ("bne", "%05d$", labelKey2num (tlbl->key));
              emitcode ("tst", "%s", aopAdrStr (aop, 1, FALSE));
              regalloc_dry_run_cost += 4;
              if (!regalloc_dry_run)
                emitLabel (tlbl);
              break;
            }
        }
      else
        {
          needpula = pushRegIfUsed (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, aop, offset--);
          if (isFloat)
            {
              emitcode ("and", "#0x7F");
              regalloc_dry_run_cost += 2;
            }
          while (--size)
            accopWithAop ("ora", aop, offset--);
          if (needpula)
            pullReg (hc08_reg_a);
          else
            {
              hc08_freeReg (hc08_reg_a);
              flagsonly = FALSE;
            }
        }
    }

  if (resultInA)
    {
      tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (flagsonly)
        {
          tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitBranch ("bne", tlbl1);
          loadRegFromConst (hc08_reg_a, zero);
          emitBranch ("bra", tlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl1);
          loadRegFromConst (hc08_reg_a, one);
        }
      else
        {
          emitBranch ("beq", tlbl);
          loadRegFromConst (hc08_reg_a, one);
        }
      if (!regalloc_dry_run)
        emitLabel (tlbl);
      hc08_useReg (hc08_reg_a);
    }
}



/*-----------------------------------------------------------------*/
/* genNot - generate code for ! operation                          */
/*-----------------------------------------------------------------*/
static void
genNot (iCode * ic)
{
  bool needpulla;

  D (emitcode (";     genNot", ""));

  /* assign asmOps to operand & result */
  aopOp (IC_LEFT (ic), ic, FALSE);
  aopOp (IC_RESULT (ic), ic, TRUE);

  needpulla = pushRegIfSurv (hc08_reg_a);
  asmopToBool (AOP (IC_LEFT (ic)), TRUE);
  emitcode ("eor", one);
  regalloc_dry_run_cost += 2;
  storeRegToFullAop (hc08_reg_a, AOP (IC_RESULT (ic)), FALSE);
  pullOrFreeReg (hc08_reg_a, needpulla);

  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genCpl - generate code for complement                           */
/*-----------------------------------------------------------------*/
static void
genCpl (iCode * ic)
{
  int offset = 0;
  int size;
  reg_info *reg;
  bool needpullreg;

  D (emitcode (";     genCpl", ""));

  /* assign asmOps to operand & result */
  aopOp (IC_LEFT (ic), ic, FALSE);
  aopOp (IC_RESULT (ic), ic, TRUE);
  size = AOP_SIZE (IC_RESULT (ic));

  if(AOP_TYPE (IC_LEFT (ic)) == AOP_REG && AOP_TYPE (IC_RESULT (ic)) == AOP_REG &&
    AOP (IC_RESULT (ic))->aopu.aop_reg[0] == AOP (IC_LEFT (ic))->aopu.aop_reg[0] &&
    (size < 2 || AOP (IC_RESULT (ic))->aopu.aop_reg[1] == AOP (IC_LEFT (ic))->aopu.aop_reg[1]))
    {
      while (size--)
        rmwWithReg ("com", AOP (IC_RESULT (ic))->aopu.aop_reg[offset++]);
      goto release;
    }

  reg = (hc08_reg_a->isDead && !(AOP_TYPE (IC_RESULT (ic)) == AOP_REG && AOP (IC_RESULT (ic))->aopu.aop_reg[0] == hc08_reg_a) ? hc08_reg_a : hc08_reg_x);

  needpullreg = pushRegIfSurv (reg);
  while (size--)
    {
      bool needpullreg2 = (!size && AOP_TYPE (IC_RESULT (ic)) == AOP_REG && AOP (IC_RESULT (ic))->aopu.aop_reg[0] == reg || size && AOP_TYPE (IC_RESULT (ic)) == AOP_REG && AOP (IC_RESULT (ic))->aopu.aop_reg[1] == reg);
      if (needpullreg2)
        pushReg (reg, TRUE);
      loadRegFromAop (reg, AOP (IC_LEFT (ic)), offset);
      rmwWithReg ("com", reg);
      hc08_useReg (reg);
      storeRegToAop (reg, AOP (IC_RESULT (ic)), offset);
      hc08_freeReg (reg);
      if (needpullreg2)
        pullReg (reg);
      offset++;
    }
  pullOrFreeReg (reg, needpullreg);

  /* release the aops */
release:
  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genUminusFloat - unary minus for floating points                */
/*-----------------------------------------------------------------*/
static void
genUminusFloat (operand * op, operand * result)
{
  int size, offset = 0;
  bool needpula;

  D (emitcode (";     genUminusFloat", ""));

  /* for this we just copy and then flip the bit */

  size = AOP_SIZE (op) - 1;

  while (size--)
    {
      transferAopAop (AOP (op), offset, AOP (result), offset);
      offset++;
    }

  needpula = pushRegIfSurv (hc08_reg_a);
  loadRegFromAop (hc08_reg_a, AOP (op), offset);
  emitcode ("eor", "#0x80");
  regalloc_dry_run_cost += 2;
  hc08_useReg (hc08_reg_a);
  storeRegToAop (hc08_reg_a, AOP (result), offset);
  pullOrFreeReg (hc08_reg_a, needpula);
}

/*-----------------------------------------------------------------*/
/* genUminus - unary minus code generation                         */
/*-----------------------------------------------------------------*/
static void
genUminus (iCode * ic)
{
  int offset, size;
  sym_link *optype;
  char *sub;
  bool needpula;
  asmop *result;

  D (emitcode (";     genUminus", ""));

  /* assign asmops */
  aopOp (IC_LEFT (ic), ic, FALSE);
  aopOp (IC_RESULT (ic), ic, TRUE);

  optype = operandType (IC_LEFT (ic));

  /* if float then do float stuff */
  if (IS_FLOAT (optype))
    {
      genUminusFloat (IC_LEFT (ic), IC_RESULT (ic));
      goto release;
    }

  /* otherwise subtract from zero */
  size = AOP_SIZE (IC_LEFT (ic));
  offset = 0;

  if (size == 1)
    {
      if (!IS_AOP_A (AOP (IC_LEFT (ic))))
        needpula = pushRegIfUsed (hc08_reg_a);
      else
        needpula = FALSE;
      loadRegFromAop (hc08_reg_a, AOP (IC_LEFT (ic)), 0);
      emitcode ("nega", "");
      regalloc_dry_run_cost++;
      hc08_freeReg (hc08_reg_a);
      storeRegToFullAop (hc08_reg_a, AOP (IC_RESULT (ic)), SPEC_USIGN (operandType (IC_LEFT (ic))));
      pullOrFreeReg (hc08_reg_a, needpula);
    }
  else
    {
      if (IS_AOP_XA (AOP (IC_RESULT (ic))))
        result = forceStackedAop (AOP (IC_RESULT (ic)), FALSE);
      else
        result = AOP (IC_RESULT (ic));

      needpula = pushRegIfUsed (hc08_reg_a);
      sub = "sub";
      while (size--)
        {
          loadRegFromConst (hc08_reg_a, zero);
          accopWithAop (sub, AOP (IC_LEFT (ic)), offset);
          storeRegToAop (hc08_reg_a, result, offset++);
          sub = "sbc";
        }
      storeRegSignToUpperAop (hc08_reg_a, result, offset, SPEC_USIGN (operandType (IC_LEFT (ic))));
      pullOrFreeReg (hc08_reg_a, needpula);

      if (IS_AOP_XA (AOP (IC_RESULT (ic))))
        freeAsmop (NULL, result, ic, TRUE);
    }



release:
  /* release the aops */
  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
  freeAsmop (IC_LEFT (ic), NULL, ic, FALSE);
}

/*-----------------------------------------------------------------*/
/* saveRegisters - will look for a call and save the registers     */
/*-----------------------------------------------------------------*/
static void
saveRegisters (iCode *lic)
{
  int i;
  iCode *ic;

  /* look for call */
  for (ic = lic; ic; ic = ic->next)
    if (ic->op == CALL || ic->op == PCALL)
      break;

  if (!ic)
    {
      fprintf (stderr, "found parameter push with no function call\n");
      return;
    }

  /* if the registers have been saved already or don't need to be then
     do nothing */
  if (ic->regsSaved)
    return;
  if (IS_SYMOP (IC_LEFT (ic)) &&
      (IFFUNC_CALLEESAVES (OP_SYMBOL (IC_LEFT (ic))->type) || IFFUNC_ISNAKED (OP_SYM_TYPE (IC_LEFT (ic)))))
    return;

  if (!regalloc_dry_run)
    ic->regsSaved = 1;
  for (i = A_IDX; i <= H_IDX; i++)
    {
      if (bitVectBitValue (ic->rSurv, i))
        pushReg (hc08_regWithIdx (i), FALSE);
    }
}

/*-----------------------------------------------------------------*/
/* unsaveRegisters - pop the pushed registers                      */
/*-----------------------------------------------------------------*/
static void
unsaveRegisters (iCode *ic)
{
  int i;
  bitVect *rsave;

  /* restore the registers in use at this time but skip the
     ones for the result */
  rsave = bitVectCplAnd (bitVectCopy (ic->rMask), hc08_rUmaskForOp (IC_RESULT (ic)));

  for (i = H_IDX; i >= A_IDX; i--)
    {
      if (bitVectBitValue (ic->rSurv, i))
        pullReg (hc08_regWithIdx (i));
    }

}


/*-----------------------------------------------------------------*/
/* pushSide -                                                      */
/*-----------------------------------------------------------------*/
static void
pushSide (operand * oper, int size, iCode * ic)
{
  int offset = 0;

  aopOp (oper, ic, FALSE);

  while (size--)
    {
      loadRegFromAop (hc08_reg_a, AOP (oper), offset++);
      pushReg (hc08_reg_a, TRUE);
    }

  freeAsmop (oper, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* assignResultValue -               */
/*-----------------------------------------------------------------*/
static void
assignResultValue (operand * oper)
{
  int size = AOP_SIZE (oper);
  int offset = 0;
  bool delayed_x = FALSE;
  while (size--)
    {
      if (!offset && AOP_TYPE (oper) == AOP_REG && AOP_SIZE (oper) > 1 && AOP (oper)->aopu.aop_reg[0]->rIdx == X_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          delayed_x = true;
        }
      else
        transferAopAop (hc08_aop_pass[offset], 0, AOP (oper), offset);
      if (hc08_aop_pass[offset]->type == AOP_REG)
        hc08_freeReg (hc08_aop_pass[offset]->aopu.aop_reg[0]);
      offset++;
    }
  if (delayed_x)
    pullReg (hc08_reg_x);
}



/*-----------------------------------------------------------------*/
/* genIpush - genrate code for pushing this gets a little complex  */
/*-----------------------------------------------------------------*/
static void
genIpush (iCode * ic)
{
  int size, offset = 0;

  D (emitcode (";     genIpush", ""));

  /* if this is not a parm push : ie. it is spill push
     and spill push is always done on the local stack */
  if (!ic->parmPush)
    {

      /* and the item is spilt then do nothing */
      if (OP_SYMBOL (IC_LEFT (ic))->isspilt)
        return;

      aopOp (IC_LEFT (ic), ic, FALSE);
      size = AOP_SIZE (IC_LEFT (ic));
      offset = 0;
      /* push it on the stack */
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (IC_LEFT (ic)), offset++);
          pushReg (hc08_reg_a, TRUE);
        }

      return;
    }

  /* this is a paramter push: in this case we call
     the routine to find the call and save those
     registers that need to be saved */
  if (!regalloc_dry_run) /* Cost for saving registers is counted at CALL or PCALL */
    saveRegisters (ic);

  /* then do the push */
  aopOp (IC_LEFT (ic), ic, FALSE);


  // pushSide(IC_LEFT(ic), AOP_SIZE(IC_LEFT(ic)));
  size = AOP_SIZE (IC_LEFT (ic));
  offset = 0;

//  l = aopGet (AOP (IC_LEFT (ic)), 0, FALSE, TRUE);
  if (AOP_TYPE (IC_LEFT (ic)) == AOP_IMMD || IS_AOP_HX (AOP (IC_LEFT (ic))))
    {
      if ((size == 2) && hc08_reg_hx->isDead || IS_AOP_HX (AOP (IC_LEFT (ic))))
        {
          loadRegFromAop (hc08_reg_hx, AOP (IC_LEFT (ic)), 0);
          pushReg (hc08_reg_hx, TRUE);
          goto release;
        }
    }

  if (AOP_TYPE (IC_LEFT (ic)) == AOP_REG)
    {
      while (size--)
        pushReg (AOP (IC_LEFT (ic))->aopu.aop_reg[offset++], TRUE);
      goto release;
    }

  while (size--)
    {
//      printf("loading %d\n", offset);
      loadRegFromAop (hc08_reg_a, AOP (IC_LEFT (ic)), offset++);
//      printf("pushing \n");
      pushReg (hc08_reg_a, TRUE);
    }

release:
  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genIpop - recover the registers: can happen only for spilling   */
/*-----------------------------------------------------------------*/
static void
genIpop (iCode * ic)
{
  int size, offset;

  D (emitcode (";     genIpop", ""));

  /* if the temp was not pushed then */
  if (OP_SYMBOL (IC_LEFT (ic))->isspilt)
    return;

  aopOp (IC_LEFT (ic), ic, FALSE);
  size = AOP_SIZE (IC_LEFT (ic));
  offset = size - 1;
  while (size--)
    {
      pullReg (hc08_reg_a);
      storeRegToAop (hc08_reg_a, AOP (IC_LEFT (ic)), offset--);
    }
  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genSend - gen code for SEND                                     */
/*-----------------------------------------------------------------*/
static void
genSend (set *sendSet)
{
  iCode *sic;
  bool delayed_x = FALSE;

  D (emitcode (";", "genSend"));

  for (sic = setFirstItem (sendSet); sic; sic = setNextItem (sendSet))
    {
      int size, offset = 0;
      aopOp (IC_LEFT (sic), sic, FALSE);
      size = AOP_SIZE (IC_LEFT (sic));

      if (sic->argreg)
        {
          offset = size - 1;
          while (size--)
            {
              if (offset + (sic->argreg - 1) == 0 && delayed_x)
                continue;
              if (offset + (sic->argreg - 1) == 1 && (!setNextItem (sendSet) && AOP_TYPE (IC_LEFT (sic)) == AOP_REG && AOP (IC_LEFT (sic))->aopu.aop_reg[0]->rIdx == X_IDX || 0))
                {
                  pushReg (hc08_reg_x, TRUE);
                  delayed_x = TRUE;
                }
              transferAopAop (AOP (IC_LEFT (sic)), offset, hc08_aop_pass[offset + (sic->argreg - 1)], 0);
              offset--;
            }
        }
      freeAsmop (IC_LEFT (sic), NULL, sic, TRUE);
    }
  if (delayed_x)
    pullReg (hc08_reg_a);
}

/*-----------------------------------------------------------------*/
/* genCall - generates a call statement                            */
/*-----------------------------------------------------------------*/
static void
genCall (iCode * ic)
{
  sym_link *dtype;
  sym_link *etype;
//  bool restoreBank = FALSE;
//  bool swapBanks = FALSE;

  D (emitcode (";", "genCall"));

  /* if caller saves & we have not saved then */
  if (!ic->regsSaved)
    saveRegisters (ic);

  dtype = operandType (IC_LEFT (ic));
  etype = getSpec (dtype);
  /* if send set is not empty then assign */
  if (_G.sendSet && !regalloc_dry_run)
    {
      if (IFFUNC_ISREENT (dtype))       /* need to reverse the send set */
        {
          genSend (reverseSet (_G.sendSet));
        }
      else
        {
          genSend (_G.sendSet);
        }
      _G.sendSet = NULL;
    }

  /* make the call */
  if (IS_LITERAL (etype))
    {
      emitcode ("jsr", "0x%04X", ulFromVal (OP_VALUE (IC_LEFT (ic))));
      regalloc_dry_run_cost += 3;
    }
  else
    {
      emitcode ("jsr", "%s", (OP_SYMBOL (IC_LEFT (ic))->rname[0] ?
                              OP_SYMBOL (IC_LEFT (ic))->rname : OP_SYMBOL (IC_LEFT (ic))->name));
      regalloc_dry_run_cost += 3;
    }

  /* if we need assign a result value */
  if ((IS_ITEMP (IC_RESULT (ic)) &&
       (OP_SYMBOL (IC_RESULT (ic))->nRegs ||
        OP_SYMBOL (IC_RESULT (ic))->accuse || OP_SYMBOL (IC_RESULT (ic))->spildir)) || IS_TRUE_SYMOP (IC_RESULT (ic)))
    {
      _G.accInUse++;
      aopOp (IC_RESULT (ic), ic, FALSE);
      _G.accInUse--;

      assignResultValue (IC_RESULT (ic));

      freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
    }

  /* adjust the stack for parameters if required */
  if (ic->parmBytes)
    {
      pullNull (ic->parmBytes);
    }

  /* if we had saved some registers then unsave them */
  if (ic->regsSaved && !IFFUNC_CALLEESAVES (dtype))
    unsaveRegisters (ic);
}

/*-----------------------------------------------------------------*/
/* genPcall - generates a call by pointer statement                */
/*-----------------------------------------------------------------*/
static void
genPcall (iCode * ic)
{
  sym_link *dtype;
  symbol *rlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
//  bool restoreBank=FALSE;
//  bool swapBanks = FALSE;

  D (emitcode (";", "genPcall"));

  dtype = operandType (IC_LEFT (ic))->next;
  /* if caller saves & we have not saved then */
  if (!ic->regsSaved)
    saveRegisters (ic);

  /* if we are calling a not _naked function that is not using
     the same register bank then we need to save the
     destination registers on the stack */

  /* push the return address on to the stack */
  emitBranch ("bsr", tlbl);
  emitBranch ("bra", rlbl);
  if (!regalloc_dry_run)
    emitLabel (tlbl);
  _G.stackPushes += 2;          /* account for the bsr return address now on stack */
  updateCFA ();

  /* now push the function address */
  pushSide (IC_LEFT (ic), FPTRSIZE, ic);

  /* if send set is not empty then assign */
  if (_G.sendSet && !regalloc_dry_run)
    {
      genSend (reverseSet (_G.sendSet));
      _G.sendSet = NULL;
    }

  /* make the call */
  emitcode ("rts", "");
  regalloc_dry_run_cost++;

  if (!regalloc_dry_run)
    emitLabel (rlbl);
  _G.stackPushes -= 4;          /* account for rts here & in called function */
  updateCFA ();

  /* if we need assign a result value */
  if ((IS_ITEMP (IC_RESULT (ic)) &&
       (OP_SYMBOL (IC_RESULT (ic))->nRegs || OP_SYMBOL (IC_RESULT (ic))->spildir)) || IS_TRUE_SYMOP (IC_RESULT (ic)))
    {

      _G.accInUse++;
      aopOp (IC_RESULT (ic), ic, FALSE);
      _G.accInUse--;

      assignResultValue (IC_RESULT (ic));

      freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
    }

  /* adjust the stack for parameters if required */
  if (ic->parmBytes)
    {
      pullNull (ic->parmBytes);
    }

  /* if we had saved some registers then unsave them */
  if (ic->regsSaved && !IFFUNC_CALLEESAVES (dtype))
    unsaveRegisters (ic);
}

/*-----------------------------------------------------------------*/
/* resultRemat - result  is rematerializable                       */
/*-----------------------------------------------------------------*/
static int
resultRemat (iCode * ic)
{
  if (SKIP_IC (ic) || ic->op == IFX)
    return 0;

  if (IC_RESULT (ic) && IS_ITEMP (IC_RESULT (ic)))
    {
      symbol *sym = OP_SYMBOL (IC_RESULT (ic));
      if (sym->remat && !POINTER_SET (ic))
        return 1;
    }

  return 0;
}

/*-----------------------------------------------------------------*/
/* inExcludeList - return 1 if the string is in exclude Reg list   */
/*-----------------------------------------------------------------*/
static int
regsCmp (void *p1, void *p2)
{
  return (STRCASECMP ((char *) p1, (char *) (p2)) == 0);
}

static bool
inExcludeList (char *s)
{
  const char *p = setFirstItem (options.excludeRegsSet);

  if (p == NULL || STRCASECMP (p, "none") == 0)
    return FALSE;


  return isinSetWith (options.excludeRegsSet, s, regsCmp);
}

/*-----------------------------------------------------------------*/
/* genFunction - generated code for function entry                 */
/*-----------------------------------------------------------------*/
static void
genFunction (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  sym_link *ftype;
  iCode *ric = (ic->next && ic->next->op == RECEIVE) ? ic->next : NULL;
  int stackAdjust = sym->stack;
  int accIsFree = sym->recvSize == 0;

  _G.nRegsSaved = 0;
  _G.stackPushes = 0;
  /* create the function header */
  emitcode (";", "-----------------------------------------");
  emitcode (";", " function %s", sym->name);
  emitcode (";", "-----------------------------------------");
  emitcode (";", " Stack space usage: %d bytes.", sym->stack);

  emitcode ("", "%s:", sym->rname);
  genLine.lineCurr->isLabel = 1;
  ftype = operandType (IC_LEFT (ic));

  _G.stackOfs = 0;
  _G.stackPushes = 0;
  debugFile->writeFrameAddress (NULL, hc08_reg_sp, 0);

  if (IFFUNC_ISNAKED (ftype))
    {
      emitcode (";", "naked function: no prologue.");
      return;
    }

  /* if this is an interrupt service routine then
     save h  */
  if (IFFUNC_ISISR (sym->type))
    {

      if (!inExcludeList ("h"))
        pushReg (hc08_reg_h, FALSE);
    }

  /* For some cases it is worthwhile to perform a RECEIVE iCode */
  /* before setting up the stack frame completely. */
  while (ric && ric->next && ric->next->op == RECEIVE)
    ric = ric->next;
  while (ric && IC_RESULT (ric))
    {
      symbol *rsym = OP_SYMBOL (IC_RESULT (ric));
      int rsymSize = rsym ? getSize (rsym->type) : 0;

      if (rsym->isitmp)
        {
          if (rsym && rsym->regType == REG_CND)
            rsym = NULL;
          if (rsym && (rsym->accuse || rsym->ruonly))
            rsym = NULL;
          if (rsym && (rsym->isspilt || rsym->nRegs == 0) && rsym->usl.spillLoc)
            rsym = rsym->usl.spillLoc;
        }

      /* If the RECEIVE operand immediately spills to the first entry on the  */
      /* stack, we can push it directly rather than use an sp relative store. */
      if (rsym && rsym->onStack && rsym->stack == -_G.stackPushes - rsymSize)
        {
          int ofs;

          genLine.lineElement.ic = ric;
          D (emitcode (";     genReceive", ""));
          for (ofs = 0; ofs < rsymSize; ofs++)
            {
              reg_info *reg = hc08_aop_pass[ofs + (ric->argreg - 1)]->aopu.aop_reg[0];
              pushReg (reg, TRUE);
              if (reg->rIdx == A_IDX)
                accIsFree = 1;
              stackAdjust--;
            }
          genLine.lineElement.ic = ic;
          ric->generated = 1;
        }
      ric = (ric->prev && ric->prev->op == RECEIVE) ? ric->prev : NULL;
    }

  /* adjust the stack for the function */
  if (stackAdjust)
    {
      adjustStack (-stackAdjust);
    }
  _G.stackOfs = sym->stack;
  _G.stackPushes = 0;

  /* if critical function then turn interrupts off */
  if (IFFUNC_ISCRITICAL (ftype))
    {
      if (!accIsFree)
        {
          /* Function was passed parameters, so make sure A is preserved */
          pushReg (hc08_reg_a, FALSE);
          pushReg (hc08_reg_a, FALSE);
          emitcode ("tpa", "");
          emitcode ("sta", "2,s");
          emitcode ("sei", "");
          regalloc_dry_run_cost += 5;
          pullReg (hc08_reg_a);
        }
      else
        {
          /* No passed parameters, so A can be freely modified */
          emitcode ("tpa", "");
          regalloc_dry_run_cost++;
          pushReg (hc08_reg_a, TRUE);
          emitcode ("sei", "");
          regalloc_dry_run_cost++;
        }
    }

}

/*-----------------------------------------------------------------*/
/* genEndFunction - generates epilogue for functions               */
/*-----------------------------------------------------------------*/
static void
genEndFunction (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));

  if (IFFUNC_ISNAKED (sym->type))
    {
      emitcode (";", "naked function: no epilogue.");
      if (options.debug && currFunc)
        debugFile->writeEndFunction (currFunc, ic, 0);
      return;
    }

  if (IFFUNC_ISCRITICAL (sym->type))
    {
      if (!IS_VOID (sym->type->next))
        {
          /* Function has return value, so make sure A is preserved */
          pushReg (hc08_reg_a, FALSE);
          emitcode ("lda", "2,s");
          emitcode ("tap", "");
          regalloc_dry_run_cost += 4;
          pullReg (hc08_reg_a);
          pullNull (1);
        }
      else
        {
          /* Function returns void, so A can be freely modified */
          pullReg (hc08_reg_a);
          emitcode ("tap", "");
          regalloc_dry_run_cost++;
        }
    }

  if (IFFUNC_ISREENT (sym->type) || options.stackAuto)
    {
    }

  if (sym->stack)
    {
      _G.stackPushes += sym->stack;
      adjustStack (sym->stack);
    }


  if ((IFFUNC_ISREENT (sym->type) || options.stackAuto))
    {
    }

  if (IFFUNC_ISISR (sym->type))
    {

      if (!inExcludeList ("h"))
        pullReg (hc08_reg_h);


      /* if debug then send end of function */
      if (options.debug && currFunc)
        {
          debugFile->writeEndFunction (currFunc, ic, 1);
        }

      emitcode ("rti", "");
      regalloc_dry_run_cost++;
    }
  else
    {
      if (IFFUNC_CALLEESAVES (sym->type))
        {
          int i;

          /* if any registers used */
          if (sym->regsUsed)
            {
              /* save the registers used */
              for (i = sym->regsUsed->size; i >= 0; i--)
                {
                  if (bitVectBitValue (sym->regsUsed, i) || (hc08_ptrRegReq && (i == HX_IDX || i == HX_IDX)))
                    emitcode ("pop", "%s", hc08_regWithIdx (i)->name); /* Todo: Cost. Can't find this instruction in manual! */
                }
            }

        }

      /* if debug then send end of function */
      if (options.debug && currFunc)
        {
          debugFile->writeEndFunction (currFunc, ic, 1);
        }

      emitcode ("rts", "");
      regalloc_dry_run_cost++;
    }

}

/*-----------------------------------------------------------------*/
/* genRet - generate code for return statement                     */
/*-----------------------------------------------------------------*/
static void
genRet (iCode * ic)
{
  int size, offset = 0;
//  int pushed = 0;
  bool delayed_x = FALSE;

  D (emitcode (";     genRet", ""));

  /* if we have no return value then
     just generate the "ret" */
  if (!IC_LEFT (ic))
    goto jumpret;

  /* we have something to return then
     move the return value into place */
  aopOp (IC_LEFT (ic), ic, FALSE);
  size = AOP_SIZE (IC_LEFT (ic));

  /* Take care when swapping a and x */
  if (AOP_TYPE (IC_LEFT (ic)) == AOP_REG && size > 1 && AOP (IC_LEFT (ic))->aopu.aop_reg[0]->rIdx == X_IDX)
    {
      delayed_x = TRUE;
      pushReg (hc08_reg_x, TRUE);
    }

  offset = size - 1;
  while (size--)
    {
      if (!(delayed_x && !offset))
      transferAopAop (AOP (IC_LEFT (ic)), offset, hc08_aop_pass[offset], 0);
      offset--;
    }

  if (delayed_x)
    pullReg (hc08_reg_a);

  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);

jumpret:
  /* generate a jump to the return label
     if the next is not the return statement */
  if (!(ic->next && ic->next->op == LABEL && IC_LABEL (ic->next) == returnLabel))
    {
      emitcode ("jmp", "%05d$", labelKey2num (returnLabel->key));
      regalloc_dry_run_cost += 3;
    }
}

/*-----------------------------------------------------------------*/
/* genLabel - generates a label                                    */
/*-----------------------------------------------------------------*/
static void
genLabel (iCode * ic)
{
  int i;
  reg_info *reg;

  /* For the high level labels we cannot depend on any */
  /* register's contents. Amnesia time.                */
  for (i = A_IDX; i <= XA_IDX; i++)
    {
      reg = hc08_regWithIdx (i);
      if (reg)
        reg->aop = NULL;
    }

  /* special case never generate */
  if (IC_LABEL (ic) == entryLabel)
    return;

  debugFile->writeLabel (IC_LABEL (ic), ic);

  emitLabel (IC_LABEL (ic));

}

/*-----------------------------------------------------------------*/
/* genGoto - generates a jmp                                      */
/*-----------------------------------------------------------------*/
static void
genGoto (iCode * ic)
{
  emitcode ("jmp", "%05d$", labelKey2num (IC_LABEL (ic)->key));
  regalloc_dry_run_cost += 3;
}

#if 0
/*-----------------------------------------------------------------*/
/* findLabelBackwards: walks back through the iCode chain looking  */
/* for the given label. Returns number of iCode instructions     */
/* between that label and given ic.          */
/* Returns zero if label not found.          */
/*-----------------------------------------------------------------*/
static int
findLabelBackwards (iCode * ic, int key)
{
  int count = 0;

  while (ic->prev)
    {
      ic = ic->prev;
      count++;

      /* If we have any pushes or pops, we cannot predict the distance.
         I don't like this at all, this should be dealt with in the
         back-end */
      if (ic->op == IPUSH || ic->op == IPOP)
        {
          return 0;
        }

      if (ic->op == LABEL && IC_LABEL (ic)->key == key)
        {
          return count;
        }
    }

  return 0;
}
#endif

/*-----------------------------------------------------------------*/
/* genPlusIncr :- does addition with increment if possible         */
/*-----------------------------------------------------------------*/
static bool
genPlusIncr (iCode * ic)
{
  int icount;
  operand *left;
  operand *result;
  bool needpulx;
  bool needpulh;
  bool needpula;
  unsigned int size = getDataSize (IC_RESULT (ic));
  unsigned int offset;
  symbol *tlbl = NULL;

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  /* will try to generate an increment */
  /* if the right side is not a literal
     we cannot */
  if (AOP_TYPE (IC_RIGHT (ic)) != AOP_LIT)
    return FALSE;

  icount = (unsigned int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  DD (emitcode ("", "; IS_AOP_HX = %d", IS_AOP_HX (AOP (left))));

  // TODO: Use aix for 8-bit operands in x.
  if ((IS_AOP_HX (AOP (left)) || ((AOP_TYPE (left) == AOP_DIR) &&
    (IS_AOP_HX (AOP (result)) || AOP_TYPE (result) == AOP_DIR))) &&
    (icount >= -128) && (icount <= 127) && (size == 2))
    {
      needpulx = pushRegIfSurv (hc08_reg_x);
      needpulh = pushRegIfSurv (hc08_reg_h);
      loadRegFromAop (hc08_reg_hx, AOP (left), 0);
      emitcode ("aix", "#%d", icount);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_hx, FALSE);
      storeRegToAop (hc08_reg_hx, AOP (result), 0);
      pullOrFreeReg (hc08_reg_h, needpulh);
      pullOrFreeReg (hc08_reg_x, needpulx);
      return TRUE;
    }

  DD (emitcode ("", "; icount = %d, sameRegs=%d", icount, sameRegs (AOP (left), AOP (result))));

  if ((icount > 255) || (icount < 0))
    return FALSE;

  if (!sameRegs (AOP (left), AOP (result)))
    return FALSE;

  D (emitcode (";     genPlusIncr", ""));

  if (size > 1)
    tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);

  if (icount == 1)
    {
      needpula = FALSE;
      rmwWithAop ("inc", AOP (result), 0);
      if (1 < size)
        emitBranch ("bne", tlbl);
    }
  else
    {
      if (!IS_AOP_A (AOP (result)) && !IS_AOP_XA (AOP (result)))
        needpula = pushRegIfUsed (hc08_reg_a);
      else
        needpula = FALSE;
      loadRegFromAop (hc08_reg_a, AOP (result), 0);
      accopWithAop ("add", AOP (IC_RIGHT (ic)), 0);
      hc08_useReg (hc08_reg_a);
      storeRegToAop (hc08_reg_a, AOP (result), 0);
      hc08_freeReg (hc08_reg_a);
      if (1 < size)
        emitBranch ("bcc", tlbl);
    }
  for (offset = 1; offset < size; offset++)
    {
      rmwWithAop ("inc", AOP (result), offset);
      if ((offset + 1) < size)
        emitBranch ("bne", tlbl);
    }

  if (size > 1 && !regalloc_dry_run)
    emitLabel (tlbl);

  pullOrFreeReg (hc08_reg_a, needpula);

  return TRUE;
}



/*-----------------------------------------------------------------*/
/* genPlus - generates code for addition                           */
/*-----------------------------------------------------------------*/
static void
genPlus (iCode * ic)
{
  int size, offset = 0;
  char *add;
  asmop *leftOp, *rightOp;
  bool needpulla;
  bool delayedstore = FALSE;
  bool mayskip = TRUE;
  bool skip = FALSE;

  /* special cases :- */

  D (emitcode (";     genPlus", ""));

  aopOp (IC_LEFT (ic), ic, FALSE);
  aopOp (IC_RIGHT (ic), ic, FALSE);
  aopOp (IC_RESULT (ic), ic, TRUE);

  /* we want registers on the left and literals on the right */
  if ((AOP_TYPE (IC_LEFT (ic)) == AOP_LIT) || (AOP_TYPE (IC_RIGHT (ic)) == AOP_REG))
    {
      operand *t = IC_RIGHT (ic);
      IC_RIGHT (ic) = IC_LEFT (ic);
      IC_LEFT (ic) = t;
    }


  /* if I can do an increment instead
     of add then GOOD for ME */
  if (genPlusIncr (ic) == TRUE)
    goto release;

  DD (emitcode ("", ";  left size = %d", getDataSize (IC_LEFT (ic))));
  DD (emitcode ("", ";  right size = %d", getDataSize (IC_RIGHT (ic))));
  DD (emitcode ("", ";  result size = %d", getDataSize (IC_RESULT (ic))));

  size = getDataSize (IC_RESULT (ic));

  leftOp = AOP (IC_LEFT (ic));
  rightOp = AOP (IC_RIGHT (ic));
  add = "add";

  offset = 0;
  needpulla = pushRegIfSurv (hc08_reg_a);
  while (size--)
    {
      loadRegFromAop (hc08_reg_a, leftOp, offset);
      if (!mayskip || AOP_TYPE (IC_RIGHT (ic)) != AOP_LIT || (((ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit) >> (offset * 8)) & 0xff) != 0x00))
        {
          accopWithAop (add, rightOp, offset);
          mayskip = FALSE;
          skip = FALSE;
        }
      else
        skip = TRUE;
      if (size && AOP_TYPE (IC_RESULT (ic)) == AOP_REG && AOP (IC_RESULT (ic))->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          delayedstore = TRUE;
        }
      else
        storeRegToAop (hc08_reg_a, AOP (IC_RESULT (ic)), offset);
      offset++;
      hc08_freeReg (hc08_reg_a);
      if (!skip)
        add = "adc";              /* further adds must propagate carry */
    }
 if (delayedstore)
   pullReg (hc08_reg_a);
 pullOrFreeReg (hc08_reg_a, needpulla);


//  adjustArithmeticResult (ic);

release:
  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
  freeAsmop (IC_RIGHT (ic), NULL, ic, TRUE);
  freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genMinusDec :- does subtraction with decrement if possible      */
/*-----------------------------------------------------------------*/
static bool
genMinusDec (iCode * ic)
{
  unsigned int icount;
  operand *left;
  operand *result;
  bool needpulx;
  bool needpulh;
  unsigned int size = getDataSize (IC_RESULT (ic));
//  int offset;
//  symbol *tlbl;

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  /* will try to generate an increment */
  /* if the right side is not a literal
     we cannot */
  if (AOP_TYPE (IC_RIGHT (ic)) != AOP_LIT)
    return FALSE;

  icount = (unsigned int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  if (((AOP_TYPE (left) == AOP_DIR) && (AOP_TYPE (result) == AOP_DIR) && (size == 2) ||
    (IS_AOP_HX (AOP (left)) || IS_AOP_X (AOP (left))) && (IS_AOP_HX (AOP (result)) || IS_AOP_X (AOP (result)))) &&
    (icount >= -127) && (icount <= 128))
    {
      needpulx = pushRegIfSurv (hc08_reg_x);
      needpulh = pushRegIfSurv (hc08_reg_h);

      loadRegFromAop (hc08_reg_hx, AOP (left), 0);
      emitcode ("aix", "#%d", -(int) icount);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_hx, FALSE);
      storeRegToAop (hc08_reg_hx, AOP (result), 0);
      pullOrFreeReg (hc08_reg_h, needpulh);
      pullOrFreeReg (hc08_reg_x, needpulx);
      return TRUE;
    }

  if ((icount > 1) || (icount < 0))
    return FALSE;

  if (!sameRegs (AOP (left), AOP (result)))
    return FALSE;

  if (size != 1)
    return FALSE;

  D (emitcode (";     genMinusDec", ""));

  rmwWithAop ("dec", AOP (result), 0);

  return TRUE;
}

/*-----------------------------------------------------------------*/
/* addSign - complete with sign                                    */
/*-----------------------------------------------------------------*/
static void
addSign (operand * result, int offset, int sign)
{
  int size = (getDataSize (result) - offset);
  if (size > 0)
    {
      if (sign)
        {
          emitcode ("rola", "");
          emitcode ("clra", "");
          emitcode ("sbc", zero);
          regalloc_dry_run_cost += 4;
          while (size--)
            storeRegToAop (hc08_reg_a, AOP (result), offset++);
        }
      else
        while (size--)
          storeConstToAop (zero, AOP (result), offset++);
    }
}


/*-----------------------------------------------------------------*/
/* genMinus - generates code for subtraction                       */
/*-----------------------------------------------------------------*/
static void
genMinus (iCode * ic)
{
  char *sub;
  int size, offset = 0;
  bool needpulla;
  bool delayedstore = FALSE;

  asmop *leftOp, *rightOp;

  D (emitcode (";     genMinus", ""));

  aopOp (IC_LEFT (ic), ic, FALSE);
  aopOp (IC_RIGHT (ic), ic, FALSE);
  aopOp (IC_RESULT (ic), ic, TRUE);

  /* special cases :- */
  /* if I can do an decrement instead
     of subtract then GOOD for ME */
  if (genMinusDec (ic) == TRUE)
    goto release;

  size = getDataSize (IC_RESULT (ic));


  leftOp = AOP (IC_LEFT (ic));
  rightOp = AOP (IC_RIGHT (ic));
  sub = "sub";
  offset = 0;

  needpulla = pushRegIfSurv (hc08_reg_a);

  if (IS_AOP_A (rightOp))
    {
      loadRegFromAop (hc08_reg_a, rightOp, offset);
      accopWithAop (sub, leftOp, offset);
      accopWithMisc ("nega", "");
      storeRegToAop (hc08_reg_a, AOP (IC_RESULT (ic)), offset++);
      pullOrFreeReg (hc08_reg_a, needpulla);
      goto release;
    }

  while (size--)
    {
      if (AOP_TYPE (IC_RIGHT (ic)) == AOP_REG && AOP (IC_RIGHT (ic))->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          loadRegFromAop (hc08_reg_a, leftOp, offset);
          emitcode (sub, "1, s");
          regalloc_dry_run_cost += 3;
          emitcode ("ais", "#1");
          regalloc_dry_run_cost += 2;
        }
      else
        {
          loadRegFromAop (hc08_reg_a, leftOp, offset);
          accopWithAop (sub, rightOp, offset);
        }
      if (size && AOP_TYPE (IC_RESULT (ic)) == AOP_REG && AOP (IC_RESULT (ic))->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          delayedstore = TRUE;
        }
      else
        storeRegToAop (hc08_reg_a, AOP (IC_RESULT (ic)), offset);
      offset++;
      sub = "sbc";
    }
  if(delayedstore)
    pullReg (hc08_reg_a);
  pullOrFreeReg (hc08_reg_a, needpulla);

//  adjustArithmeticResult (ic);

release:
  freeAsmop (IC_LEFT (ic), NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (IC_RIGHT (ic), NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
}



/*-----------------------------------------------------------------*/
/* genMultOneByte : 8*8=8/16 bit multiplication                    */
/*-----------------------------------------------------------------*/
static void
genMultOneByte (operand * left, operand * right, operand * result)
{
  /* sym_link *opetype = operandType (result); */
  symbol *tlbl1, *tlbl2, *tlbl3, *tlbl4;
  int size = AOP_SIZE (result);
  bool negLiteral = FALSE;
  bool lUnsigned, rUnsigned;
  bool needpulla, needpullx;

  D (emitcode (";     genMultOneByte", ""));

  if (size < 1 || size > 2)
    {
      // this should never happen
      fprintf (stderr, "size!=1||2 (%d) in %s at line:%d \n", AOP_SIZE (result), __FILE__, lineno);
      exit (1);
    }

  /* (if two literals: the value is computed before) */
  /* if one literal, literal on the right */
  if (AOP_TYPE (left) == AOP_LIT)
    {
      operand *t = right;
      right = left;
      left = t;
    }
  /* if an operand is in A, make sure it is on the left */
  if (IS_AOP_A (AOP (right)))
    {
      operand *t = right;
      right = left;
      left = t;
    }

  needpulla = pushRegIfSurv (hc08_reg_a);
  needpullx = pushRegIfSurv (hc08_reg_x);

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  /* lUnsigned  rUnsigned  negLiteral  negate     case */
  /* false      false      false       odd        3    */
  /* false      false      true        even       3    */
  /* false      true       false       odd        3    */
  /* false      true       true        impossible      */
  /* true       false      false       odd        3    */
  /* true       false      true        always     2    */
  /* true       true       false       never      1    */
  /* true       true       true        impossible      */

  /* case 1 */
  if (size == 1 || (lUnsigned && rUnsigned))
    {
      // just an unsigned 8*8=8/16 multiply
      //DD(emitcode (";","unsigned"));

      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      emitcode ("mul", "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_xa, FALSE);
      storeRegToFullAop (hc08_reg_xa, AOP (result), TRUE);
      hc08_freeReg (hc08_reg_xa);

      return;
    }

  // we have to do a signed multiply

  /* case 2 */
  /* left unsigned, right signed literal -- literal determines sign handling */
  if (AOP_TYPE (right) == AOP_LIT && lUnsigned && !rUnsigned)
    {
      signed char val = (signed char) ulFromVal (AOP (right)->aopu.aop_lit);

      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      if (val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", val);
          regalloc_dry_run_cost += 2;
        }

      emitcode ("mul", "");
      regalloc_dry_run_cost++;

      if (val < 0)
        {
          rmwWithReg ("neg", hc08_reg_a);
          tlbl4 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitBranch ("bcc", tlbl4);
          rmwWithReg ("inc", hc08_reg_x);
          if (!regalloc_dry_run)
            emitLabel (tlbl4);
          rmwWithReg ("neg", hc08_reg_x);
        }

      hc08_dirtyReg (hc08_reg_xa, FALSE);
      storeRegToFullAop (hc08_reg_xa, AOP (result), TRUE);
      hc08_freeReg (hc08_reg_xa);
      return;
    }


  /* case 3 */
  adjustStack (-1);
  emitcode ("clr", "1,s");
  regalloc_dry_run_cost += 3;

  loadRegFromAop (hc08_reg_a, AOP (left), 0);
  if (!lUnsigned)
    {
      tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      emitcode ("tsta", "");
      regalloc_dry_run_cost++;
      emitBranch ("bpl", tlbl1);
      emitcode ("inc", "1,s");
      regalloc_dry_run_cost += 3;
      rmwWithReg ("neg", hc08_reg_a);
      regalloc_dry_run_cost++;
      if (!regalloc_dry_run)
        emitLabel (tlbl1);
    }

  if (AOP_TYPE (right) == AOP_LIT && !rUnsigned)
    {
      signed char val = (signed char) ulFromVal (AOP (right)->aopu.aop_lit);
      /* AND literal negative */
      if (val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
          negLiteral = TRUE;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", val);
          regalloc_dry_run_cost += 2;
        }
      hc08_useReg (hc08_reg_x);
    }
  else
    {
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", hc08_reg_x);
          regalloc_dry_run_cost++;
          if (!regalloc_dry_run)
            emitLabel (tlbl2);
        }
    }

  emitcode ("mul", "");
  regalloc_dry_run_cost++;
  hc08_dirtyReg (hc08_reg_xa, FALSE);

  tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  emitcode ("dec", "1,s");
  regalloc_dry_run_cost += 3;
  if (!lUnsigned && !rUnsigned && negLiteral)
    emitBranch ("beq", tlbl3);
  else
    emitBranch ("bne", tlbl3);

  rmwWithReg ("neg", hc08_reg_a);
  tlbl4 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  emitBranch ("bcc", tlbl4);
  rmwWithReg ("inc", hc08_reg_x);
  if (!regalloc_dry_run)
    emitLabel (tlbl4);
  rmwWithReg ("neg", hc08_reg_x);

  if (!regalloc_dry_run)
    emitLabel (tlbl3);
  adjustStack (1);
  storeRegToFullAop (hc08_reg_xa, AOP (result), TRUE);
  
  pullOrFreeReg (hc08_reg_x, needpullx);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genMult - generates code for multiplication                     */
/*-----------------------------------------------------------------*/
static void
genMult (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genMult", ""));

  /* assign the amsops */
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* special cases first */
  /* if both are of size == 1 */
//  if (getSize(operandType(left)) == 1 &&
//      getSize(operandType(right)) == 1)
  if (AOP_SIZE (left) == 1 && AOP_SIZE (right) == 1)
    {
      genMultOneByte (left, right, result);
      goto release;
    }

  /* should have been converted to function call */
  fprintf (stderr, "left: %d right: %d\n", getSize (OP_SYMBOL (left)->type), getSize (OP_SYMBOL (right)->type));
  fprintf (stderr, "left: %d right: %d\n", AOP_SIZE (left), AOP_SIZE (right));
  assert (0);

release:
  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genDivOneByte : 8 bit division                                  */
/*-----------------------------------------------------------------*/
static void
genDivOneByte (operand * left, operand * right, operand * result)
{
  symbol *tlbl1, *tlbl2, *tlbl3;
  int size;
  int offset = 0;
  bool lUnsigned, rUnsigned;
  bool runtimeSign, compiletimeSign;
  bool needpulla, needpullh;
  bool needpullx = FALSE;
  bool delayed_a = FALSE;

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  D (emitcode (";     genDivOneByte", ""));

  needpulla = pushRegIfSurv (hc08_reg_a);
  needpullh = pushRegIfSurv (hc08_reg_h);

  size = AOP_SIZE (result);
  /* signed or unsigned */
  if (lUnsigned && rUnsigned)
    {
      /* unsigned is easy */
      loadRegFromAop (hc08_reg_h, AOP (left), 1);
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      emitcode ("div", "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      hc08_dirtyReg (hc08_reg_h, FALSE);
      storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
      hc08_freeReg (hc08_reg_x);
      pullOrFreeReg (hc08_reg_h, needpullh);
      pullOrFreeReg (hc08_reg_a, needpulla);
      return;
    }

  needpullx = pushRegIfSurv (hc08_reg_x);

  /* signed is a little bit more difficult */

  /* now sign adjust for both left & right */

  /* let's see what's needed: */
  /* apply negative sign during runtime */
  runtimeSign = FALSE;
  /* negative sign from literals */
  compiletimeSign = FALSE;

  if (!lUnsigned)
    {
      if (AOP_TYPE (left) == AOP_LIT)
        {
          /* signed literal */
          signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);
          if (val < 0)
            compiletimeSign = TRUE;
        }
      else
        /* signed but not literal */
        runtimeSign = TRUE;
    }

  if (!rUnsigned)
    {
      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* signed literal */
          signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);
          if (val < 0)
            compiletimeSign ^= TRUE;
        }
      else
        /* signed but not literal */
        runtimeSign = TRUE;
    }

  /* initialize the runtime sign */
  if (runtimeSign)
    {
      if (compiletimeSign)
        loadRegFromConst (hc08_reg_x, "#1");    /* set sign flag */
      else
        loadRegFromConst (hc08_reg_x, zero);    /* reset sign flag */
      pushReg (hc08_reg_x, TRUE);
    }

  /* save the signs of the operands */
  if (AOP_TYPE (right) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);

      if (!rUnsigned && val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
    }
  else                          /* ! literal */
    {
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl1);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", hc08_reg_x);
          if (!regalloc_dry_run)
            emitLabel (tlbl1);
        }
    }

  if (AOP_TYPE (left) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);

      if (!lUnsigned && val < 0)
        {
          emitcode ("lda", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("lda", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
    }
  else                          /* ! literal */
    {
      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      if (!lUnsigned)
        {
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tsta", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", hc08_reg_a);
          if (!regalloc_dry_run)
            emitLabel (tlbl2);
        }
    }

  loadRegFromConst (hc08_reg_h, zero);
  emitcode ("div", "");
  regalloc_dry_run_cost++;
  hc08_dirtyReg (hc08_reg_x, FALSE);
  hc08_dirtyReg (hc08_reg_a, FALSE);
  hc08_dirtyReg (hc08_reg_h, FALSE);

  if (runtimeSign || compiletimeSign)
    {
      tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      if (runtimeSign)
        {
          pullReg (hc08_reg_x);
          rmwWithReg ("lsr", hc08_reg_x);
          rmwWithReg ("ror", hc08_reg_x);
          emitBranch ("bpl", tlbl3);
        }

      rmwWithReg ("neg", hc08_reg_a);
      if (runtimeSign && !regalloc_dry_run)
        emitLabel (tlbl3);

      if (size > 1 && AOP_TYPE (result) == AOP_REG && AOP (result)->aopu.aop_reg[0]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          delayed_a = TRUE;
        }
      else
        storeRegToAop (hc08_reg_a, AOP (result), 0);

      if (size > 1)
        {
          /* msb is 0x00 or 0xff depending on the sign */
          if (runtimeSign)
            {
              rmwWithReg ("lsl", hc08_reg_x);
              emitcode ("clra", "");
              emitcode ("sbc", "#0");
              regalloc_dry_run_cost += 3;
              while (--size)
                {
                  offset++;
                  if (size && AOP_TYPE (result) == AOP_REG && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
                    {
                      pushReg (hc08_reg_a, TRUE);
                      delayed_a = TRUE;
                    }
                  else
                    storeRegToAop (hc08_reg_a, AOP (result), offset);
                }
            }
          else                  /* compiletimeSign */
            while (--size)
                storeConstToAop ("#0xff", AOP (result), ++offset);
        }
    }
  else
    {
      storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
    }

  if (delayed_a)
    pullReg (hc08_reg_a);

  pullOrFreeReg (hc08_reg_x, needpullx);
  pullOrFreeReg (hc08_reg_h, needpullh);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genDiv - generates code for division                            */
/*-----------------------------------------------------------------*/
static void
genDiv (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genDiv", ""));

  /* assign the amsops */
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* special cases first */
  /* if both are of size == 1 */
  if (AOP_SIZE (left) <= 2 && AOP_SIZE (right) == 1)
    {
      genDivOneByte (left, right, result);
      goto release;
    }

  /* should have been converted to function call */
  assert (0);
release:
  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genModOneByte : 8 bit modulus                                   */
/*-----------------------------------------------------------------*/
static void
genModOneByte (operand * left, operand * right, operand * result)
{
  symbol *tlbl1, *tlbl2, *tlbl3;
  int size;
  int offset = 0;
  bool lUnsigned, rUnsigned;
  bool runtimeSign, compiletimeSign;
  bool needpulla, needpullh;
  bool needpullx = FALSE;

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  D (emitcode (";     genModOneByte", ""));

  size = AOP_SIZE (result);

  needpulla = pushRegIfSurv (hc08_reg_a);
  needpullh = pushRegIfSurv (hc08_reg_h);

  if (lUnsigned && rUnsigned)
    {
      /* unsigned is easy */
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      loadRegFromAop (hc08_reg_h, AOP (left), 1);
      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      emitcode ("div", "");
      regalloc_dry_run_cost++;
      hc08_freeReg (hc08_reg_a);
      hc08_freeReg (hc08_reg_x);
      hc08_dirtyReg (hc08_reg_h, FALSE);
      storeRegToFullAop (hc08_reg_h, AOP (result), FALSE);
      pullOrFreeReg (hc08_reg_h, needpullh);
      pullOrFreeReg (hc08_reg_a, needpulla);
      return;
    }

  /* signed is a little bit more difficult */
  needpullx = pushRegIfSurv (hc08_reg_x);

  if (AOP_TYPE (right) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);

      if (!rUnsigned && val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
    }
  else                          /* ! literal */
    {
      loadRegFromAop (hc08_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl1);
          rmwWithReg ("neg", hc08_reg_x);
          if (!regalloc_dry_run)
            emitLabel (tlbl1);
        }
    }

  /* let's see what's needed: */
  /* apply negative sign during runtime */
  runtimeSign = FALSE;
  /* negative sign from literals */
  compiletimeSign = FALSE;

  /* sign adjust left side */
  if (AOP_TYPE (left) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);

      if (!lUnsigned && val < 0)
        {
          compiletimeSign = TRUE;       /* set sign flag */
          emitcode ("lda", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("lda", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
    }
  else                          /* ! literal */
    {
      if (lUnsigned)
        loadRegFromAop (hc08_reg_a, AOP (left), 0);
      else
        {
          runtimeSign = TRUE;
          adjustStack (-1);
          emitcode ("clr", "1,s");
          regalloc_dry_run_cost += 3;

          loadRegFromAop (hc08_reg_a, AOP (left), 0);
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tsta", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", hc08_reg_a);
          if (!regalloc_dry_run)
            emitLabel (tlbl2);
        }
    }

  loadRegFromConst (hc08_reg_h, zero);
  emitcode ("div", "");
  regalloc_dry_run_cost++;
  hc08_freeReg (hc08_reg_a);
  hc08_freeReg (hc08_reg_x);
  hc08_dirtyReg (hc08_reg_h, FALSE);

  if (runtimeSign || compiletimeSign)
    {
      transferRegReg (hc08_reg_h, hc08_reg_a, TRUE);
      tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      if (runtimeSign)
        {
          pullReg (hc08_reg_x);
          rmwWithReg ("lsr", hc08_reg_x);
          rmwWithReg ("ror", hc08_reg_x);
          emitBranch ("bpl", tlbl3);
        }

      rmwWithReg ("neg", hc08_reg_a);
      if (runtimeSign && !regalloc_dry_run)
        emitLabel (tlbl3);

      storeRegToAop (hc08_reg_a, AOP (result), 0);

      if (size > 1)
        {
          /* msb is 0x00 or 0xff depending on the sign */
          if (runtimeSign)
            {
              rmwWithReg ("lsl", hc08_reg_x);
              emitcode ("clra", "");
              emitcode ("sbc", "#0");
              regalloc_dry_run_cost += 3;
              while (--size)
                storeRegToAop (hc08_reg_a, AOP (result), ++offset);
            }
          else                  /* compiletimeSign */
            while (--size)
              storeConstToAop ("#0xff", AOP (result), ++offset);
        }
    }
  else
    {
      storeRegToFullAop (hc08_reg_h, AOP (result), FALSE);
    }

  pullOrFreeReg (hc08_reg_x, needpullx);
  pullOrFreeReg (hc08_reg_h, needpullh);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genMod - generates code for division                            */
/*-----------------------------------------------------------------*/
static void
genMod (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genMod", ""));

  /* assign the amsops */
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* special cases first */
  /* if both are of size == 1 */
  if (AOP_SIZE (left) <= 2 && AOP_SIZE (right) == 1)
    {
      genModOneByte (left, right, result);
      goto release;
    }

  /* should have been converted to function call */
  assert (0);

release:
  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genIfxJump :- will create a jump depending on the ifx           */
/*-----------------------------------------------------------------*/
static void
genIfxJump (iCode * ic, char *jval)
{
  symbol *jlbl;
  symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  char *inst;

  D (emitcode (";     genIfxJump", ""));

  /* if true label then we jump if condition
     supplied is true */
  if (IC_TRUE (ic))
    {
      jlbl = IC_TRUE (ic);
      if (!strcmp (jval, "a"))
        inst = "beq";
      else if (!strcmp (jval, "c"))
        inst = "bcc";
      else
        inst = "bge";
    }
  else
    {
      /* false label is present */
      jlbl = IC_FALSE (ic);
      if (!strcmp (jval, "a"))
        inst = "bne";
      else if (!strcmp (jval, "c"))
        inst = "bcs";
      else
        inst = "blt";
    }
  emitBranch (inst, tlbl);
  emitBranch ("jmp", jlbl);
  if (!regalloc_dry_run)
    emitLabel (tlbl);

  /* mark the icode as generated */
  ic->generated = 1;
}


/*-----------------------------------------------------------------*/
/* exchangedCmp : returns the opcode need if the two operands are  */
/*                exchanged in a comparison                        */
/*-----------------------------------------------------------------*/
static int
exchangedCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return '>';
    case '>':
      return '<';
    case LE_OP:
      return GE_OP;
    case GE_OP:
      return LE_OP;
    case NE_OP:
      return NE_OP;
    case EQ_OP:
      return EQ_OP;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return EQ_OP;                 /* shouldn't happen, but need to return something */
}

/*------------------------------------------------------------------*/
/* negatedCmp : returns the equivalent opcode for when a comparison */
/*              if not true                                         */
/*------------------------------------------------------------------*/
static int
negatedCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return GE_OP;
    case '>':
      return LE_OP;
    case LE_OP:
      return '>';
    case GE_OP:
      return '<';
    case NE_OP:
      return EQ_OP;
    case EQ_OP:
      return NE_OP;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return EQ_OP;                 /* shouldn't happen, but need to return something */
}

/*------------------------------------------------------------------*/
/* nameCmp : helper function for human readable debug output        */
/*------------------------------------------------------------------*/
static char *
nameCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return "<";
    case '>':
      return ">";
    case LE_OP:
      return "<=";
    case GE_OP:
      return ">=";
    case NE_OP:
      return "!=";
    case EQ_OP:
      return "==";
    default:
      return "invalid";
    }
}

/*------------------------------------------------------------------*/
/* branchInstCmp : returns the conditional branch instruction that  */
/*                 will branch if the comparison is true            */
/*------------------------------------------------------------------*/
static char *
branchInstCmp (int opcode, int sign)
{
  switch (opcode)
    {
    case '<':
      if (sign)
        return "blt";
      else
        return "bcs";           /* same as blo */
    case '>':
      if (sign)
        return "bgt";
      else
        return "bhi";
    case LE_OP:
      if (sign)
        return "ble";
      else
        return "bls";
    case GE_OP:
      if (sign)
        return "bge";
      else
        return "bcc";           /* same as bhs */
    case NE_OP:
      return "bne";
    case EQ_OP:
      return "beq";
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return "brn";
}


/*------------------------------------------------------------------*/
/* genCmp :- greater or less than (and maybe with equal) comparison */
/*------------------------------------------------------------------*/
static void
genCmp (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  sym_link *letype, *retype;
  int sign, opcode;
  int size, offset = 0;
  unsigned long lit = 0L;
  char *sub;
  symbol *jlbl = NULL;
  bool needpulla = FALSE;

  opcode = ic->op;

  D (emitcode (";     genCmp", "(%s)", nameCmp (opcode)));

  result = IC_RESULT (ic);
  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);

  sign = 0;
  if (IS_SPEC (operandType (left)) && IS_SPEC (operandType (right)))
    {
      letype = getSpec (operandType (left));
      retype = getSpec (operandType (right));
      sign = !(SPEC_USIGN (letype) | SPEC_USIGN (retype));
    }

  /* assign the amsops */
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* need register operand on left, prefer literal operand on right */
  if ((AOP_TYPE (right) == AOP_REG) || AOP_TYPE (left) == AOP_LIT)
    {
      operand *temp = left;
      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }

  if (ifx)
    {
      if (IC_TRUE (ifx))
        {
          jlbl = IC_TRUE (ifx);
          opcode = negatedCmp (opcode);
        }
      else
        {
          /* false label is present */
          jlbl = IC_FALSE (ifx);
        }
    }

  size = max (AOP_SIZE (left), AOP_SIZE (right));

  if ((size == 2)
      && ((AOP_TYPE (left) == AOP_DIR || IS_AOP_HX (AOP (left))) && (AOP_SIZE (left) == 2))
      && ((AOP_TYPE (right) == AOP_LIT) || ((AOP_TYPE (right) == AOP_DIR) && (AOP_SIZE (right) == 2))) && (hc08_reg_h->isDead && hc08_reg_x->isDead || IS_AOP_HX (AOP (left))))
    {
      loadRegFromAop (hc08_reg_hx, AOP (left), 0);
      emitcode ("cphx", "%s", aopAdrStr (AOP (right), 1, TRUE));
      regalloc_dry_run_cost += (AOP_TYPE (right) == AOP_DIR ? 2 : 3);
      hc08_freeReg (hc08_reg_hx);
    }
  else
    {
      offset = 0;
      if (size == 1)
        sub = "cmp";
      else
        {
          sub = "sub";

          /* These conditions depend on the Z flag bit, but Z is */
          /* only valid for the last byte of the comparison, not */
          /* the whole value. So exchange the operands to get a  */
          /* comparison that doesn't depend on Z. (This is safe  */
          /* to do here since ralloc won't assign multi-byte     */
          /* operands to registers for comparisons)              */
          if ((opcode == '>') || (opcode == LE_OP))
            {
              operand *temp = left;
              left = right;
              right = temp;
              opcode = exchangedCmp (opcode);
            }

          if ((AOP_TYPE (right) == AOP_LIT) && !isOperandVolatile (left, FALSE))
            {
              lit = ulFromVal (AOP (right)->aopu.aop_lit);
              while ((size > 1) && (((lit >> (8 * offset)) & 0xff) == 0))
                {
                  offset++;
                  size--;
                }
            }
        }
      needpulla = pushRegIfSurv (hc08_reg_a);
      while (size--)
        {
          if (AOP_TYPE (right) == AOP_REG && AOP(right)->aopu.aop_reg[offset]->rIdx == A_IDX)
            {
              pushReg (hc08_reg_a, TRUE);
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              emitcode (sub, "1, s");
              regalloc_dry_run_cost += 3;
              pullReg (hc08_reg_a);
            }
          else
            {
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              accopWithAop (sub, AOP (right), offset);
            }
          hc08_freeReg (hc08_reg_a);
          offset++;
          sub = "sbc";
        }
    }
  freeAsmop (right, NULL, ic, FALSE);
  freeAsmop (left, NULL, ic, FALSE);

  if (ifx)
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      char *inst;

      pullOrFreeReg (hc08_reg_a, needpulla);

      freeAsmop (result, NULL, ic, TRUE);

      inst = branchInstCmp (opcode, sign);
      emitBranch (inst, tlbl);
      emitBranch ("jmp", jlbl);
      if (!regalloc_dry_run)
        emitLabel (tlbl);

      /* mark the icode as generated */
      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (!needpulla)
        needpulla = pushRegIfSurv (hc08_reg_a);

      emitBranch (branchInstCmp (opcode, sign), tlbl1);
      loadRegFromConst (hc08_reg_a, zero);
      emitBranch ("bra", tlbl2);
      if (!regalloc_dry_run)
        emitLabel (tlbl1);
      loadRegFromConst (hc08_reg_a, one);
      if (!regalloc_dry_run)
        emitLabel (tlbl2);
      storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
      pullOrFreeReg (hc08_reg_a, needpulla);
      freeAsmop (result, NULL, ic, TRUE);
    }
}

/*-----------------------------------------------------------------*/
/* genCmpEQorNE - equal or not equal comparison                    */
/*-----------------------------------------------------------------*/
static void
genCmpEQorNE (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int opcode;
  int size, offset = 0;
  symbol *jlbl = NULL;
  symbol *tlbl_NE = NULL;
  symbol *tlbl_EQ = NULL;
  bool needpulla = FALSE;

  opcode = ic->op;

  D (emitcode (";     genCmpEQorNE", "(%s)", nameCmp (opcode)));

  result = IC_RESULT (ic);
  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);

  /* assign the amsops */
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* need register operand on left, prefer literal operand on right */
  if ((AOP_TYPE (right) == AOP_REG) || AOP_TYPE (left) == AOP_LIT)
    {
      operand *temp = left;
      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }

  if (ifx)
    {
      if (IC_TRUE (ifx))
        {
          jlbl = IC_TRUE (ifx);
          opcode = negatedCmp (opcode);
        }
      else
        {
          /* false label is present */
          jlbl = IC_FALSE (ifx);
        }
    }

  size = max (AOP_SIZE (left), AOP_SIZE (right));

  if ((size == 2)
      && ((AOP_TYPE (left) == AOP_DIR || IS_AOP_HX (AOP (left))) && (AOP_SIZE (left) == 2))
      && ((AOP_TYPE (right) == AOP_LIT) || ((AOP_TYPE (right) == AOP_DIR) && (AOP_SIZE (right) == 2))) && hc08_reg_h->isDead && hc08_reg_x->isDead)
    {
      loadRegFromAop (hc08_reg_hx, AOP (left), 0);
      emitcode ("cphx", "%s", aopAdrStr (AOP (right), 1, TRUE));
      regalloc_dry_run_cost += (AOP_TYPE (right) == AOP_DIR ? 2 : 3);
      hc08_freeReg (hc08_reg_hx);
    }
  else
    {
      offset = 0;
      while (size--)
        {
          if (AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == X_IDX)
            accopWithAop ("cpx", AOP (right), offset);
          else
            {
              if (!(AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == A_IDX))
                {
                  needpulla = pushRegIfSurv (hc08_reg_a);
                  loadRegFromAop (hc08_reg_a, AOP (left), offset);
                }
              accopWithAop ("cmp", AOP (right), offset);
              if (!(AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == A_IDX))
                pullOrFreeReg (hc08_reg_a, needpulla);
              needpulla = FALSE;
            }
          if (size)
            {
              if (!needpulla)
                needpulla = pushRegIfSurv (hc08_reg_a);
              if (!tlbl_NE && !regalloc_dry_run)
                tlbl_NE = newiTempLabel (NULL);
              emitBranch ("bne", tlbl_NE);
              pullOrFreeReg (hc08_reg_a, needpulla);
              needpulla = FALSE;
            }
          offset++;
          }
    }
  freeAsmop (right, NULL, ic, FALSE);
  freeAsmop (left, NULL, ic, FALSE);

  if (ifx)
    {
      freeAsmop (result, NULL, ic, TRUE);

      if (opcode == EQ_OP)
        {
          if (!tlbl_EQ && !regalloc_dry_run)
            tlbl_EQ = newiTempLabel (NULL);
          emitBranch ("beq", tlbl_EQ);
          if (tlbl_NE)
            emitLabel (tlbl_NE);
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl_EQ);
        }
      else
        {
          if (!tlbl_NE && !regalloc_dry_run)
            tlbl_NE = newiTempLabel (NULL);
          emitBranch ("bne", tlbl_NE);
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl_NE);
        }

      /* mark the icode as generated */
      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (!needpulla)
        needpulla = pushRegIfSurv (hc08_reg_a);
      if (opcode == EQ_OP)
        {
          if (!tlbl_EQ && !regalloc_dry_run)
            tlbl_EQ = newiTempLabel (NULL);
          emitBranch ("beq", tlbl_EQ);
          if (tlbl_NE)
            emitLabel (tlbl_NE);
          loadRegFromConst (hc08_reg_a, zero);
          emitBranch ("bra", tlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl_EQ);
          loadRegFromConst (hc08_reg_a, one);
        }
      else
        {
          if (!tlbl_NE && !regalloc_dry_run)
            tlbl_NE = newiTempLabel (NULL);
          emitBranch ("bne", tlbl_NE);
          loadRegFromConst (hc08_reg_a, zero);
          emitBranch ("bra", tlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl_NE);
          loadRegFromConst (hc08_reg_a, one);
        }

      if (!regalloc_dry_run)
        emitLabel (tlbl);
      storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
      pullOrFreeReg (hc08_reg_a, needpulla);
      freeAsmop (result, NULL, ic, TRUE);
    }
}

static bool
genPointerGetSetOfs (iCode * ic)
{
  iCode *lic = ic->next;
  bool pset, pget;
  int size;
  symbol *sym;
  asmop *derefaop;

  /* TODO: Make this work with new allcoator! */
  return FALSE;

  /* Make sure we have a next iCode */
  DD (emitcode ("", "; checking lic"));
  if (!lic)
    return FALSE;

  /* Make sure the result of the addition is an iTemp */
  DD (emitcode ("", "; checking IS_ITEMP"));
  if (!IS_ITEMP (IC_RESULT (ic)))
    return FALSE;

  /* Make sure the next iCode is a pointer set or get */
  pset = POINTER_SET (lic);
  pget = POINTER_GET (lic);
  DD (emitcode ("", "; pset=%d, pget=%d", pset, pget));
  if (!pset && !pget)
    return FALSE;

  /* Make sure this is the only use of the pointer */
  if (bitVectnBitsOn (OP_USES (IC_RESULT (ic))) > 1)
    return FALSE;

  DD (emitcode ("", "; checking pset operandsEqu"));
  if (pset & !operandsEqu (IC_RESULT (ic), IC_RESULT (lic)))
    return FALSE;

  DD (emitcode ("", "; checking pget operandsEqu"));
  if (pget & !operandsEqu (IC_RESULT (ic), IC_LEFT (lic)))
    return FALSE;

  DD (emitcode ("", "; checking IS_SYMOP"));
  if (!IS_SYMOP (IC_LEFT (ic)))
    return FALSE;

  DD (emitcode ("", "; checking !IS_TRUE_SYMOP"));
  if (IS_TRUE_SYMOP (IC_LEFT (ic)))
    return FALSE;

  sym = OP_SYMBOL (IC_LEFT (ic));

  DD (emitcode ("", "; checking remat"));
  if (!sym->remat)
    return FALSE;


  if (pget)
    {
      D (emitcode (";     genPointerGetOfs", ""));
      aopOp (IC_LEFT (ic), ic, FALSE);
      derefaop = aopDerefAop (AOP (IC_LEFT (ic)));
      freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);

      aopOp (IC_RIGHT (ic), ic, FALSE);
      aopOp (IC_RESULT (lic), lic, FALSE);

      if (AOP_SIZE (IC_RIGHT (ic)) == 1)
        {
          if (SPEC_USIGN (getSpec (operandType (IC_RIGHT (ic)))))
            {
              loadRegFromAop (hc08_reg_x, AOP (IC_RIGHT (ic)), 0);
              loadRegFromConst (hc08_reg_h, zero);
            }
          else
            {
              loadRegFromAop (hc08_reg_a, AOP (IC_RIGHT (ic)), 0);
              transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
              emitcode ("rola", "");
              emitcode ("clra", "");
              emitcode ("sbc", "#0");
              regalloc_dry_run_cost += 4;
              hc08_useReg (hc08_reg_a);
              transferRegReg (hc08_reg_a, hc08_reg_h, FALSE);
            }
        }
      else
        loadRegFromAop (hc08_reg_hx, AOP (IC_RIGHT (ic)), 0);
      size = AOP_SIZE (IC_RESULT (lic));
      derefaop->size = size;

      while (size--)
        {
          emitcode ("lda", "%s,x", aopAdrStr (derefaop, size, TRUE));
          regalloc_dry_run_cost += 3;
          hc08_useReg (hc08_reg_a);
          storeRegToAop (hc08_reg_a, AOP (IC_RESULT (lic)), size);
          hc08_freeReg (hc08_reg_a);
        }

      lic->generated = 1;
      hc08_freeReg (hc08_reg_hx);

      freeAsmop (NULL, derefaop, ic, TRUE);
      freeAsmop (IC_RIGHT (ic), NULL, ic, TRUE);
      freeAsmop (IC_RESULT (lic), NULL, lic, TRUE);

      return TRUE;
    }

  if (pset)
    {
      D (emitcode (";     genPointerSetOfs", ""));
      aopOp (IC_LEFT (ic), ic, FALSE);
      derefaop = aopDerefAop (AOP (IC_LEFT (ic)));
      freeAsmop (IC_LEFT (ic), NULL, ic, TRUE);

      aopOp (IC_RIGHT (ic), ic, FALSE);
      aopOp (IC_RIGHT (lic), lic, FALSE);

      if (AOP_SIZE (IC_RIGHT (ic)) == 1)
        {
          if (SPEC_USIGN (getSpec (operandType (IC_RIGHT (ic)))))
            {
              loadRegFromAop (hc08_reg_x, AOP (IC_RIGHT (ic)), 0);
              loadRegFromConst (hc08_reg_h, zero);
            }
          else
            {
              loadRegFromAop (hc08_reg_a, AOP (IC_RIGHT (ic)), 0);
              transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
              emitcode ("rola", "");
              emitcode ("clra", "");
              emitcode ("sbc", "#0");
              regalloc_dry_run_cost += 4;
              hc08_useReg (hc08_reg_a);
              transferRegReg (hc08_reg_a, hc08_reg_h, FALSE);
            }
        }
      else
        loadRegFromAop (hc08_reg_hx, AOP (IC_RIGHT (ic)), 0);
      size = AOP_SIZE (IC_RIGHT (lic));
      derefaop->size = size;

      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (IC_RIGHT (lic)), size);
          emitcode ("sta", "%s,x", aopAdrStr (derefaop, size, TRUE));
          regalloc_dry_run_cost += 3;
          hc08_freeReg (hc08_reg_a);
        }

      lic->generated = 1;
      hc08_freeReg (hc08_reg_hx);

      freeAsmop (NULL, derefaop, ic, TRUE);
      freeAsmop (IC_RIGHT (ic), NULL, ic, TRUE);
      freeAsmop (IC_RIGHT (lic), NULL, lic, TRUE);

      return TRUE;
    }

  return FALSE;
}


/*-----------------------------------------------------------------*/
/* hasInchc08 - operand is incremented before any other use        */
/*-----------------------------------------------------------------*/
iCode *
hasInchc08 (const operand *op, const iCode *ic, int osize)
{
  sym_link *type = operandType (op);
  sym_link *retype = getSpec (type);
  iCode *lic = ic->next;
  int isize;

  /* this could from a cast, e.g.: "(char xdata *) 0x7654;" */
  if (!IS_SYMOP (op))
    return NULL;

  if (IS_BITVAR (retype) || !IS_PTR (type))
    return NULL;
  if (IS_AGGREGATE (type->next))
    return NULL;
  if (osize != (isize = getSize (type->next)))
    return NULL;

  while (lic)
    {
      /* if operand of the form op = op + <sizeof *op> */
      if (lic->op == '+' && isOperandEqual (IC_LEFT (lic), op) &&
          isOperandEqual (IC_RESULT (lic), op) &&
          isOperandLiteral (IC_RIGHT (lic)) && operandLitValue (IC_RIGHT (lic)) == isize)
        {
          return lic;
        }
      /* if the operand used or deffed */
      if (bitVectBitValue (OP_USES (op), lic->key) || lic->defKey == op->key)
        {
          return NULL;
        }
      /* if GOTO or IFX */
      if (lic->op == IFX || lic->op == GOTO || lic->op == LABEL)
        break;
      lic = lic->next;
    }
  return NULL;
}

/*-----------------------------------------------------------------*/
/* genAndOp - for && operation                                     */
/*-----------------------------------------------------------------*/
static void
genAndOp (iCode * ic)
{
  operand *left, *right, *result;
  symbol *tlbl, *tlbl0;
  bool needpulla;

  D (emitcode (";     genAndOp", ""));

  /* note here that && operations that are in an
     if statement are taken away by backPatchLabels
     only those used in arthmetic operations remain */
  aopOp ((left = IC_LEFT (ic)), ic, FALSE);
  aopOp ((right = IC_RIGHT (ic)), ic, FALSE);
  aopOp ((result = IC_RESULT (ic)), ic, FALSE);

  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  tlbl0 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  needpulla = pushRegIfSurv (hc08_reg_a);
  asmopToBool (AOP (left), FALSE);
  emitBranch ("beq", tlbl0);
  asmopToBool (AOP (right), FALSE);
  emitBranch ("beq", tlbl0);
  loadRegFromConst (hc08_reg_a, one);
  emitBranch ("bra", tlbl);
  if (!regalloc_dry_run)
    emitLabel (tlbl0);
  loadRegFromConst (hc08_reg_a, zero);
  if (!regalloc_dry_run)
    emitLabel (tlbl);

  hc08_useReg (hc08_reg_a);
  hc08_freeReg (hc08_reg_a);

  storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
  pullOrFreeReg(hc08_reg_a, needpulla);

  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genOrOp - for || operation                                      */
/*-----------------------------------------------------------------*/
static void
genOrOp (iCode * ic)
{
  operand *left, *right, *result;
  symbol *tlbl, *tlbl0;
  bool needpulla;

  D (emitcode (";     genOrOp", ""));

  /* note here that || operations that are in an
     if statement are taken away by backPatchLabels
     only those used in arthmetic operations remain */
  aopOp ((left = IC_LEFT (ic)), ic, FALSE);
  aopOp ((right = IC_RIGHT (ic)), ic, FALSE);
  aopOp ((result = IC_RESULT (ic)), ic, FALSE);

  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  tlbl0 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  needpulla = pushRegIfSurv (hc08_reg_a);
  asmopToBool (AOP (left), FALSE);
  emitBranch ("bne", tlbl0);
  asmopToBool (AOP (right), FALSE);
  emitBranch ("bne", tlbl0);
  loadRegFromConst (hc08_reg_a, zero);
  emitBranch ("bra", tlbl);
  if (!regalloc_dry_run)
    emitLabel (tlbl0);
  loadRegFromConst (hc08_reg_a, one);
  if (!regalloc_dry_run)
    emitLabel (tlbl);

  hc08_useReg (hc08_reg_a);
  hc08_freeReg (hc08_reg_a);

  storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
  pullOrFreeReg(hc08_reg_a, needpulla);

  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* isLiteralBit - test if lit == 2^n                               */
/*-----------------------------------------------------------------*/
static int
isLiteralBit (unsigned long lit)
{
  unsigned long pw[32] =
  {
    1L, 2L, 4L, 8L, 16L, 32L, 64L, 128L,
    0x100L, 0x200L, 0x400L, 0x800L,
    0x1000L, 0x2000L, 0x4000L, 0x8000L,
    0x10000L, 0x20000L, 0x40000L, 0x80000L,
    0x100000L, 0x200000L, 0x400000L, 0x800000L,
    0x1000000L, 0x2000000L, 0x4000000L, 0x8000000L,
    0x10000000L, 0x20000000L, 0x40000000L, 0x80000000L
  };
  int idx;

  for (idx = 0; idx < 32; idx++)
    if (lit == pw[idx])
      return idx + 1;
  return 0;
}

#if 0
/*-----------------------------------------------------------------*/
/* continueIfTrue -                                                */
/*-----------------------------------------------------------------*/
static void
continueIfTrue (iCode * ic)
{
  if (IC_TRUE (ic))
    emitBranch ("jmp", IC_TRUE (ic));
  ic->generated = 1;
}

/*-----------------------------------------------------------------*/
/* jmpIfTrue -                                                     */
/*-----------------------------------------------------------------*/
static void
jumpIfTrue (iCode * ic)
{
  if (!IC_TRUE (ic))
    emitBranch ("jmp", IC_FALSE (ic));
  ic->generated = 1;
}

/*-----------------------------------------------------------------*/
/* jmpTrueOrFalse -                                                */
/*-----------------------------------------------------------------*/
static void
jmpTrueOrFalse (iCode * ic, symbol * tlbl)
{
  // ugly but optimized by peephole
  if (IC_TRUE (ic))
    {
      symbol *nlbl = newiTempLabel (NULL);
      emitBranch ("bra", nlbl);
      emitLabel (tlbl);
      emitBranch ("jmp", IC_TRUE (ic));
      emitLabel (nlbl);
    }
  else
    {
      emitBranch ("jmp", IC_FALSE (ic));
      emitLabel (tlbl);
    }
  ic->generated = 1;
}
#endif

/*-----------------------------------------------------------------*/
/* genAnd  - code for and                                          */
/*-----------------------------------------------------------------*/
static void
genAnd (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  unsigned long lit = 0L;
  unsigned long litinv;
  unsigned char bytemask;
  bool needpulla = FALSE;

//  int bytelit = 0;
//  char buffer[10];

  D (emitcode (";     genAnd", ""));

  aopOp ((left = IC_LEFT (ic)), ic, FALSE);
  aopOp ((right = IC_RIGHT (ic)), ic, FALSE);
  aopOp ((result = IC_RESULT (ic)), ic, TRUE);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]&r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]&r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not then exchange them */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if right is accumulator & left is not then exchange them */
  if (AOP_TYPE (right) == AOP_REG && AOP_TYPE (left) != AOP_REG)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    lit = ulFromVal (AOP (right)->aopu.aop_lit);

  size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);

  if (AOP_TYPE (result) == AOP_CRY && size > 1 && (isOperandVolatile (left, FALSE) || isOperandVolatile (right, FALSE)))
    {
      needpulla = pushRegIfSurv (hc08_reg_a);

      /* this generates ugly code, but meets volatility requirements */
      loadRegFromConst (hc08_reg_a, zero);
      pushReg (hc08_reg_a, TRUE);

      offset = 0;
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          accopWithAop ("and", AOP (right), offset);
          emitcode ("ora", "1,s");
          emitcode ("sta", "1,s");
          regalloc_dry_run_cost += 6;
          offset++;
        }

      pullReg (hc08_reg_a);
      emitcode ("tsta", "");
      regalloc_dry_run_cost++;

      pullOrFreeReg (hc08_reg_a, needpulla);

      genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl = NULL;

      needpulla = pushRegIfSurv (hc08_reg_a);

      offset = 0;
      while (size--)
        {
          bytemask = (lit >> (offset * 8)) & 0xff;

          if (AOP_TYPE (right) == AOP_LIT && bytemask == 0)
            {
              /* do nothing */
            }
          else if (AOP_TYPE (right) == AOP_LIT && bytemask == 0xff)
            {
              rmwWithAop ("tst", AOP (left), offset);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else
            {
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              accopWithAop ("and", AOP (right), offset);
              hc08_freeReg (hc08_reg_a);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          offset++;
        }
      if (tlbl)
        emitLabel (tlbl);

      pullOrFreeReg (hc08_reg_a, needpulla);

      if (ifx)
        genIfxJump (ifx, "a");
      goto release;
    }

  size = AOP_SIZE (result);

  if (AOP_TYPE (right) == AOP_LIT)
    {
      litinv = (~lit) & (((unsigned int) 0xffffffff) >> (8 * (4 - size)));
      if (sameRegs (AOP (IC_LEFT (ic)), AOP (IC_RESULT (ic))) && (AOP_TYPE (left) == AOP_DIR) && isLiteralBit (litinv))
        {
          int bitpos = isLiteralBit (litinv) - 1;
          emitcode ("bclr", "#%d,%s", bitpos & 7, aopAdrStr (AOP (left), bitpos >> 3, FALSE));
          regalloc_dry_run_cost += 2;
          goto release;
        }
    }

  needpulla = pushRegIfSurv (hc08_reg_a);

  offset = 0;
  while (size--)
    {
      bytemask = (lit >> (offset * 8)) & 0xff;

      if (AOP_TYPE (right) == AOP_LIT && bytemask == 0)
        {
          if (isOperandVolatile (left, FALSE))
            {
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              hc08_freeReg (hc08_reg_a);
            }
          storeConstToAop (zero, AOP (result), offset);
        }
      else if (AOP_TYPE (right) == AOP_LIT && bytemask == 0xff)
        {
          transferAopAop (AOP (left), offset, AOP (result), offset);
        }
      else
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          accopWithAop ("and", AOP (right), offset);
          storeRegToAop (hc08_reg_a, AOP (result), offset);
          hc08_freeReg (hc08_reg_a);
        }
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          needpulla = TRUE;
        }
      offset++;
    }

  pullOrFreeReg (hc08_reg_a, needpulla);

release:
  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genOr  - code for or                                            */
/*-----------------------------------------------------------------*/
static void
genOr (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  unsigned long lit = 0L;
  unsigned char bytemask;
  bool needpulla = FALSE;

  D (emitcode (";     genOr", ""));

  aopOp ((left = IC_LEFT (ic)), ic, FALSE);
  aopOp ((right = IC_RIGHT (ic)), ic, FALSE);
  aopOp ((result = IC_RESULT (ic)), ic, TRUE);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]&r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]&r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not then exchange them */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if left is accumulator & right is not then exchange them */
  if (AOP_TYPE (right) == AOP_REG && AOP_TYPE (left) != AOP_REG)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    lit = ulFromVal (AOP (right)->aopu.aop_lit);

  size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);

  if (AOP_TYPE (result) == AOP_CRY && size > 1 && (isOperandVolatile (left, FALSE) || isOperandVolatile (right, FALSE)))
    {
      needpulla = pushRegIfSurv (hc08_reg_a);

      /* this generates ugly code, but meets volatility requirements */
      loadRegFromConst (hc08_reg_a, zero);
      pushReg (hc08_reg_a, TRUE);

      offset = 0;
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          accopWithAop ("ora", AOP (right), offset);
          emitcode ("ora", "1,s");
          emitcode ("sta", "1,s");
          regalloc_dry_run_cost += 6;
          offset++;
        }

      pullReg (hc08_reg_a);
      emitcode ("tsta", "");
      regalloc_dry_run_cost++;

      pullOrFreeReg (hc08_reg_a, needpulla);

      genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl = NULL;

      needpulla = pushRegIfSurv (hc08_reg_a);

      offset = 0;
      while (size--)
        {
          bytemask = (lit >> (offset * 8)) & 0xff;

          if (AOP_TYPE (right) == AOP_LIT && bytemask == 0x00)
            {
              rmwWithAop ("tst", AOP (left), offset);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else
            {
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              accopWithAop ("ora", AOP (right), offset);
              hc08_freeReg (hc08_reg_a);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          offset++;
        }
      if (tlbl)
        emitLabel (tlbl);

      pullOrFreeReg (hc08_reg_a, needpulla);

      if (ifx)
        genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    lit = ulFromVal (AOP (right)->aopu.aop_lit);

  size = AOP_SIZE (result);

  if (sameRegs (AOP (IC_LEFT (ic)), AOP (IC_RESULT (ic))) &&
      (AOP_TYPE (right) == AOP_LIT) && isLiteralBit (lit) && (AOP_TYPE (left) == AOP_DIR))
    {
      int bitpos = isLiteralBit (lit) - 1;
      emitcode ("bset", "#%d,%s", bitpos & 7, aopAdrStr (AOP (left), bitpos >> 3, FALSE));
      regalloc_dry_run_cost += 2;
      goto release;
    }

  needpulla = pushRegIfSurv (hc08_reg_a);

  offset = 0;
  while (size--)
    {
      bytemask = (lit >> (offset * 8)) & 0xff;

      if (AOP_TYPE (right) == AOP_LIT && bytemask == 0xff)
        {
          if (isOperandVolatile (left, FALSE))
            {
              loadRegFromAop (hc08_reg_a, AOP (left), offset);
              hc08_freeReg (hc08_reg_a);
            }
          transferAopAop (AOP (right), offset, AOP (result), offset);
        }
      else if (AOP_TYPE (right) == AOP_LIT && bytemask == 0)
        {
          transferAopAop (AOP (left), offset, AOP (result), offset);
        }
      else
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          accopWithAop ("ora", AOP (right), offset);
          storeRegToAop (hc08_reg_a, AOP (result), offset);
          hc08_freeReg (hc08_reg_a);
        }
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          needpulla = TRUE;
        }
      offset++;
    }

  pullOrFreeReg (hc08_reg_a, needpulla);

release:
  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genXor - code for xclusive or                                   */
/*-----------------------------------------------------------------*/
static void
genXor (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  bool needpulla = FALSE;

  D (emitcode (";     genXor", ""));

  aopOp ((left = IC_LEFT (ic)), ic, FALSE);
  aopOp ((right = IC_RIGHT (ic)), ic, FALSE);
  aopOp ((result = IC_RESULT (ic)), ic, TRUE);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]&r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]&r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not ||
     if left needs acc & right does not */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if left is accumulator & right is not then exchange them */
  if (AOP_TYPE (right) == AOP_REG && AOP_TYPE (left) != AOP_REG)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  needpulla = pushRegIfSurv (hc08_reg_a);

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl;
      // this can just happen, if ifx has been optimized away
      // wassertl (ifx, "AOP_CPY result without ifx");

      tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);
      offset = 0;
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          if (AOP_TYPE (right) == AOP_LIT && ((ulFromVal (AOP (right)->aopu.aop_lit) >> (offset * 8)) & 0xff) == 0)
            {
              emitcode ("tsta", "");
              regalloc_dry_run_cost++;
            }
          else
            accopWithAop ("eor", AOP (right), offset);

          hc08_freeReg (hc08_reg_a);
          if (size)
            emitBranch ("bne", tlbl);
          else
            {
              /*
               * I think this is all broken here, (see simulation mismatch in bug1875933.c)
               *   multiple calls to emitLabel() ?!
               * and we can't genIfxJump, if there is none
               */
              if (!regalloc_dry_run)
                emitLabel (tlbl);
              if (ifx)
                genIfxJump (ifx, "a");
            }
          offset++;
        }
    }

  size = AOP_SIZE (result);
  offset = 0;
  while (size--)
    {
      loadRegFromAop (hc08_reg_a, AOP (left), offset);
      accopWithAop ("eor", AOP (right), offset);
      storeRegToAop (hc08_reg_a, AOP (result), offset);
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (hc08_reg_a, TRUE);
          needpulla = TRUE;
        }
      hc08_freeReg (hc08_reg_a);
      offset++;
    }

  pullOrFreeReg (hc08_reg_a, needpulla);

//release:

  freeAsmop (left, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (right, NULL, ic, (RESULTONSTACK (ic) ? FALSE : TRUE));
  freeAsmop (result, NULL, ic, TRUE);
}

static const char *
expand_symbols (iCode * ic, const char *inlin)
{
  const char *begin = NULL, *p = inlin;
  bool inIdent = FALSE;
  struct dbuf_s dbuf;

  dbuf_init (&dbuf, 128);

  while (*p)
    {
      if (inIdent)
        {
          if ('_' == *p || isalnum (*p))
            /* in the middle of identifier */
            ++p;
          else
            {
              /* end of identifier */
              symbol *sym, *tempsym;
              char *symname = Safe_strndup (p + 1, p - begin - 1);

              inIdent = 0;

              tempsym = newSymbol (symname, ic->level);
              tempsym->block = ic->block;
              sym = (symbol *) findSymWithLevel (SymbolTab, tempsym);
              if (!sym)
                {
                  dbuf_append (&dbuf, begin, p - begin);
                }
              else
                {
                  asmop *aop = aopForSym (ic, sym, FALSE);
                  char *l = aopAdrStr (aop, aop->size - 1, TRUE);

                  if ('#' == *l)
                    l++;
                  sym->isref = 1;
                  if (sym->level && !sym->allocreq && !sym->ismyparm)
                    {
                      werror (E_ID_UNDEF, sym->name);
                      werror (W_CONTINUE,
                              "  Add 'volatile' to the variable declaration so that it\n"
                              "  can be referenced within inline assembly");
                    }
                  dbuf_append_str (&dbuf, l);
                }
              Safe_free (symname);
              begin = p++;
            }
        }
      else if ('_' == *p)
        {
          /* begin of identifier */
          inIdent = TRUE;
          if (begin)
            dbuf_append (&dbuf, begin, p - begin);
          begin = p++;
        }
      else
        {
          if (!begin)
            begin = p;
          p++;
        }
    }

  if (begin)
    dbuf_append (&dbuf, begin, p - begin);

  return dbuf_detach_c_str (&dbuf);
}

/*-----------------------------------------------------------------*/
/* genInline - write the inline code out                           */
/*-----------------------------------------------------------------*/
static void
hc08_genInline (iCode * ic)
{
  char *buf, *bp, *begin;
  const char *expanded;
  bool inComment = FALSE;

  D (emitcode (";", "genInline"));

  genLine.lineElement.isInline += (!options.asmpeep);

  buf = bp = begin = Safe_strdup (IC_INLINE (ic));

  /* emit each line as a code */
  while (*bp)
    {
      switch (*bp)
        {
        case ';':
          inComment = TRUE;
          ++bp;
          break;

        case '\x87':
        case '\n':
          inComment = FALSE;
          *bp++ = '\0';
          expanded = expand_symbols (ic, begin);
          emitcode (expanded, NULL);
          dbuf_free (expanded);
          begin = bp;
          break;

        default:
          /* Add \n for labels, not dirs such as c:\mydir */
          if (!inComment && (*bp == ':') && (isspace ((unsigned char) bp[1])))
            {
              ++bp;
              *bp = '\0';
              ++bp;
              emitcode (begin, NULL);
              begin = bp;
            }
          else
            ++bp;
          break;
        }
    }
  if (begin != bp)
    {
      expanded = expand_symbols (ic, begin);
      emitcode (expanded, NULL);
      dbuf_free (expanded);
    }

  Safe_free (buf);

  /* consumed; we can free it here */
  dbuf_free (IC_INLINE (ic));

  genLine.lineElement.isInline -= (!options.asmpeep);
}

/*-----------------------------------------------------------------*/
/* genRRC - rotate right with carry                                */
/*-----------------------------------------------------------------*/
static void
genRRC (iCode * ic)
{
  operand *left, *result;
  int size, offset = 0;
  bool needpula = FALSE;
  bool resultInA = FALSE;
  char *shift;

  D (emitcode (";     genRRC", ""));

  /* rotate right with carry */
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

  if ((AOP_TYPE (result) == AOP_REG) && (AOP (result)->aopu.aop_reg[0]->rIdx == A_IDX))
    resultInA = TRUE;

  size = AOP_SIZE (result);
  offset = size - 1;

  shift = "lsr";
  if (sameRegs (AOP (IC_LEFT (ic)), AOP (IC_RESULT (ic))))
    {
      while (size--)
        {
          rmwWithAop (shift, AOP (result), offset--);
          shift = "ror";
        }
    }
  else
    {
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          rmwWithReg (shift, hc08_reg_a);
          storeRegToAop (hc08_reg_a, AOP (result), offset--);
          hc08_freeReg (hc08_reg_a);
          shift = "ror";
        }
    }

  if ((!hc08_reg_a->isFree) || resultInA)
    {
      pushReg (hc08_reg_a, TRUE);
      needpula = TRUE;
    }

  /* now we need to put the carry into the
     highest order byte of the result */
  offset = AOP_SIZE (result) - 1;
  emitcode ("clra", "");
  emitcode ("rora", "");
  regalloc_dry_run_cost += 2;
  hc08_dirtyReg (hc08_reg_a, FALSE);
  if (resultInA)
    {
      emitcode ("ora", "1,s");
      emitcode ("ais", "#1");
      regalloc_dry_run_cost += 5;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      needpula = FALSE;
    }
  else
    accopWithAop ("ora", AOP (result), offset);
  storeRegToAop (hc08_reg_a, AOP (result), offset);

  pullOrFreeReg (hc08_reg_a, needpula);

  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genRLC - generate code for rotate left with carry               */
/*-----------------------------------------------------------------*/
static void
genRLC (iCode * ic)
{
  operand *left, *result;
  int size, offset = 0;
  char *shift;
  bool resultInA = FALSE;
  bool needpula = FALSE;

  D (emitcode (";     genRLC", ""));

  /* rotate right with carry */
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

  if ((AOP_TYPE (result) == AOP_REG) && (AOP (result)->aopu.aop_reg[0]->rIdx == A_IDX))
    resultInA = TRUE;

  size = AOP_SIZE (result);
  offset = 0;

  shift = "lsl";
  if (sameRegs (AOP (IC_LEFT (ic)), AOP (IC_RESULT (ic))))
    {
      while (size--)
        {
          rmwWithAop (shift, AOP (result), offset--);
          shift = "rol";
        }
    }
  else
    {
      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), offset);
          rmwWithReg (shift, hc08_reg_a);
          storeRegToAop (hc08_reg_a, AOP (result), offset++);
          hc08_freeReg (hc08_reg_a);
          shift = "rol";
        }
    }

  if ((!hc08_reg_a->isFree) || resultInA)
    {
      pushReg (hc08_reg_a, TRUE);
      needpula = TRUE;
    }

  /* now we need to put the carry into the
     lowest order byte of the result */
  offset = 0;
  emitcode ("clra", "");
  emitcode ("rola", "");
  regalloc_dry_run_cost += 2;
  hc08_dirtyReg (hc08_reg_a, FALSE);
  if (resultInA)
    {
      emitcode ("ora", "1,s");
      emitcode ("ais", "#1");
      regalloc_dry_run_cost += 5;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      needpula = FALSE;
    }
  else
    accopWithAop ("ora", AOP (result), offset);
  storeRegToAop (hc08_reg_a, AOP (result), offset);

  pullOrFreeReg (hc08_reg_a, needpula);

  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genGetHbit - generates code get highest order bit               */
/*-----------------------------------------------------------------*/
static void
genGetHbit (iCode * ic)
{
  operand *left, *result;
  bool needpulla;

  D (emitcode (";     genGetHbit", ""));

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

  /* get the highest order byte into a */
  needpulla = pushRegIfSurv (hc08_reg_a);
  loadRegFromAop (hc08_reg_a, AOP (left), AOP_SIZE (left) - 1);
  emitcode ("rola", "");
  emitcode ("clra", "");
  emitcode ("rola", "");
  regalloc_dry_run_cost += 3;
  hc08_dirtyReg (hc08_reg_a, FALSE);
  storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
  pullOrFreeReg (hc08_reg_a, needpulla);

  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genGetAbit - generates code get a single bit                    */
/*-----------------------------------------------------------------*/
static void
genGetAbit (iCode * ic)
{
  operand *left, *right, *result;
  int shCount;

  D (emitcode (";     genGetAbit", ""));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, FALSE);

  shCount = (int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  /* get the needed byte into a */
  loadRegFromAop (hc08_reg_a, AOP (left), shCount / 8);
  shCount %= 8;
  if (AOP_TYPE (result) == AOP_CRY)
    {
      emitcode ("and", "#0x%02x", 1 << shCount);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      hc08_freeReg (hc08_reg_a);
    }
  else
    {
      switch (shCount)
        {
        case 3:
          emitcode ("lsra", "");
          regalloc_dry_run_cost++;
          //fallthrough
        case 2:
          emitcode ("lsra", "");
          regalloc_dry_run_cost++;
          //fallthrough
        case 1:
          emitcode ("lsra", "");
          regalloc_dry_run_cost++;
          //fallthrough
        case 0:
          emitcode ("and", "#0x01");
          regalloc_dry_run_cost += 2;
          break;
        case 4:
          emitcode ("nsa", "");
          emitcode ("and", "#0x01");
          regalloc_dry_run_cost += 3;
          break;
        case 5:
          emitcode ("rola", "");
          regalloc_dry_run_cost++;
          //fallthrough
        case 6:
          emitcode ("rola", "");
          regalloc_dry_run_cost++;
          //fallthrough
        case 7:
          emitcode ("rola", "");
          emitcode ("clra", "");
          emitcode ("rola", "");
          regalloc_dry_run_cost += 3;
          break;
        }
      hc08_dirtyReg (hc08_reg_a, FALSE);
      storeRegToFullAop (hc08_reg_a, AOP (result), FALSE);
      hc08_freeReg (hc08_reg_a);
    }

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (left, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genGetByte - generates code get a single byte                   */
/*-----------------------------------------------------------------*/
static void
genGetByte (iCode * ic)
{
  operand *left, *right, *result;
  int offset;

  D (emitcode (";", "genGetByte"));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, FALSE);

  offset = (int) ulFromVal (AOP (right)->aopu.aop_lit) / 8;
  transferAopAop (AOP (left), offset, AOP (result), 0);

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (left, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genGetWord - generates code get two bytes                       */
/*-----------------------------------------------------------------*/
static void
genGetWord (iCode * ic)
{
  operand *left, *right, *result;
  int offset;

  D (emitcode (";", "genGetWord"));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (right, ic, FALSE);
  aopOp (result, ic, FALSE);

  offset = (int) ulFromVal (AOP (right)->aopu.aop_lit) / 8;
  transferAopAop (AOP (left), offset + 1, AOP (result), 1);
  transferAopAop (AOP (left), offset, AOP (result), 0);

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (left, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genSwap - generates code to swap nibbles or bytes               */
/*-----------------------------------------------------------------*/
static void
genSwap (iCode * ic)
{
  operand *left, *result;
  bool needpulla;

  D (emitcode (";     genSwap", ""));

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

  switch (AOP_SIZE (left))
    {
    case 1:                    /* swap nibbles in byte */
      needpulla = pushRegIfSurv (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, AOP (left), 0);
      emitcode ("nsa", "");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      storeRegToAop (hc08_reg_a, AOP (result), 0);
      pullOrFreeReg (hc08_reg_a, needpulla);
      break;
    case 2:                    /* swap bytes in a word */
      if (AOP_TYPE (result) == AOP_REG && AOP_TYPE (left) == AOP_REG)
        {
          if (AOP (result)->aopu.aop_reg[1] != AOP (left)->aopu.aop_reg[0]);
            pushReg (AOP (left)->aopu.aop_reg[0], TRUE);
          storeRegToAop (AOP (left)->aopu.aop_reg[1], AOP (result), 0);
          if (AOP (result)->aopu.aop_reg[1] != AOP (left)->aopu.aop_reg[0]);
            pullReg (AOP (result)->aopu.aop_reg[1]);
        }
      else if (operandsEqu (left, result) || sameRegs (AOP (left), AOP (result)))
        {
          needpulla = pushRegIfSurv (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, AOP (left), 0);
          hc08_useReg (hc08_reg_a);
          transferAopAop (AOP (left), 1, AOP (result), 0);
          storeRegToAop (hc08_reg_a, AOP (result), 1);
          pullOrFreeReg (hc08_reg_a, needpulla);
        }
      else
        {
          transferAopAop (AOP (left), 0, AOP (result), 1);
          transferAopAop (AOP (left), 1, AOP (result), 0);
        }
      break;
    default:
      wassertl (FALSE, "unsupported SWAP operand size");
    }

  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}

#if 0
/*-----------------------------------------------------------------*/
/* AccRol - rotate left accumulator by known count                 */
/*-----------------------------------------------------------------*/
static void
AccRol (int shCount)
{
  shCount &= 0x0007;            // shCount : 0..7

  switch (shCount)
    {
    case 0:
      break;
    case 1:
      emitcode ("rola", "");    /* 1 cycle */
      break;
    case 2:
      emitcode ("rola", "");    /* 1 cycle */
      emitcode ("rola", "");    /* 1 cycle */
      break;
    case 3:
      emitcode ("nsa", "");
      emitcode ("rora", "");
      break;
    case 4:
      emitcode ("nsa", "");     /* 3 cycles */
      break;
    case 5:
      emitcode ("nsa", "");     /* 3 cycles */
      emitcode ("rola", "");    /* 1 cycle */
      break;
    case 6:
      emitcode ("nsa", "");     /* 3 cycles */
      emitcode ("rola", "");    /* 1 cycle */
      emitcode ("rola", "");    /* 1 cycle */
      break;
    case 7:
      emitcode ("nsa", "");     /* 3 cycles */
      emitcode ("rola", "");    /* 1 cycle */
      emitcode ("rola", "");    /* 1 cycle */
      emitcode ("rola", "");    /* 1 cycle */
      break;
    }
}
#endif


/*-----------------------------------------------------------------*/
/* AccLsh - left shift accumulator by known count                  */
/*-----------------------------------------------------------------*/
static void
AccLsh (int shCount)
{
  int i;

  shCount &= 0x0007;            // shCount : 0..7

  /* Shift counts of 4 and 5 are currently optimized for code size.        */
  /* Falling through to the unrolled loop would be optimal for code speed. */
  /* For shift counts of 6 and 7, the unrolled loop is never optimal.      */
  switch (shCount)
    {
    case 4:
      if (optimize.codeSpeed)
        break;
      accopWithMisc ("nsa", "");
      accopWithMisc ("and", "#0xf0");
      /* total: 5 cycles, 3 bytes */
      return;
    case 5:
      if (optimize.codeSpeed)
        break;
      accopWithMisc ("nsa", "");
      accopWithMisc ("and", "#0xf0");
      accopWithMisc ("lsla", "");
      /* total: 6 cycles, 4 bytes */
      return;
    case 6:
      accopWithMisc ("rora", "");
      accopWithMisc ("rora", "");
      accopWithMisc ("rora", "");
      accopWithMisc ("and", "#0xc0");
      /* total: 5 cycles, 5 bytes */
      return;
    case 7:
      accopWithMisc ("rora", "");
      accopWithMisc ("clra", "");
      accopWithMisc ("rora", "");
      /* total: 3 cycles, 3 bytes */
      return;
    }

  /* lsla is only 1 cycle and byte, so an unrolled loop is often  */
  /* the fastest (shCount<6) and shortest (shCount<4).            */
  for (i = 0; i < shCount; i++)
    accopWithMisc ("lsla", "");
}


/*-----------------------------------------------------------------*/
/* AccSRsh - signed right shift accumulator by known count         */
/*-----------------------------------------------------------------*/
static void
AccSRsh (int shCount)
{
  int i;

  shCount &= 0x0007;            // shCount : 0..7

  if (shCount == 7)
    {
      accopWithMisc ("rola", "");
      accopWithMisc ("clra", "");
      accopWithMisc ("sbc", zero);
      /* total: 4 cycles, 4 bytes */
      return;
    }

  for (i = 0; i < shCount; i++)
    accopWithMisc ("asra", "");
}

/*-----------------------------------------------------------------*/
/* AccRsh - right shift accumulator by known count                 */
/*-----------------------------------------------------------------*/
static void
AccRsh (int shCount, bool sign)
{
  int i;

  if (sign)
    {
      AccSRsh (shCount);
      return;
    }

  shCount &= 0x0007;            // shCount : 0..7

  /* Shift counts of 4 and 5 are currently optimized for code size.        */
  /* Falling through to the unrolled loop would be optimal for code speed. */
  /* For shift counts of 6 and 7, the unrolled loop is never optimal.      */
  switch (shCount)
    {
    case 4:
      if (optimize.codeSpeed)
        break;
      accopWithMisc ("nsa", "");
      accopWithMisc ("and", "#0x0f");
      /* total: 5 cycles, 3 bytes */
      return;
    case 5:
      if (optimize.codeSpeed)
        break;
      accopWithMisc ("nsa", "");
      accopWithMisc ("and", "#0x0f");
      accopWithMisc ("lsra", "");
      /* total: 6 cycles, 4 bytes */
      return;
    case 6:
      accopWithMisc ("rola", "");
      accopWithMisc ("rola", "");
      accopWithMisc ("rola", "");
      accopWithMisc ("and", "#0x03");
      /* total: 5 cycles, 5 bytes */
      return;
    case 7:
      accopWithMisc ("rola", "");
      accopWithMisc ("clra", "");
      accopWithMisc ("rola", "");
      /* total: 3 cycles, 3 bytes */
      return;
    }

  /* lsra is only 1 cycle and byte, so an unrolled loop is often  */
  /* the fastest (shCount<6) and shortest (shCount<4).            */
  for (i = 0; i < shCount; i++)
    accopWithMisc ("lsra", "");
}


/*-----------------------------------------------------------------*/
/* XAccLsh - left shift register pair XA by known count            */
/*-----------------------------------------------------------------*/
static void
XAccLsh (int shCount)
{
  int i;

  shCount &= 0x000f;            // shCount : 0..15

  if (shCount >= 8)
    {
      AccLsh (shCount - 8);
      transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
      loadRegFromConst (hc08_reg_a, zero);
      return;
    }

  /* if we can beat 2n cycles or bytes for some special case, do it here */
  switch (shCount)
    {
    case 7:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   lsrx       1  1        0abc defg  ijkl mnop   h
       **   rora       1  1        0abc defg  hijk lmno   p
       **   tax        1  1        hijk lmno  hijk lmno   p
       **   clra       1  1        hijk lmno  0000 0000   p
       **   rora       1  1        hijk lmno  p000 0000   0
       ** total: 5 cycles, 5 bytes (beats 14 cycles, 14 bytes)
       */
      rmwWithReg ("lsr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
      transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
      loadRegFromConst (hc08_reg_a, zero);
      rmwWithReg ("ror", hc08_reg_a);
      return;

    default:
      ;
    }

  /* lsla/rolx is only 2 cycles and bytes, so an unrolled loop is often  */
  /* the fastest and shortest.                                           */
  for (i = 0; i < shCount; i++)
    {
      rmwWithReg ("lsl", hc08_reg_a);
      rmwWithReg ("rol", hc08_reg_x);
    }
}

/*-----------------------------------------------------------------*/
/* XAccSRsh - signed right shift register pair XA by known count   */
/*-----------------------------------------------------------------*/
static void
XAccSRsh (int shCount)
{
  int i;

  shCount &= 0x000f;            // shCount : 0..7

  /* if we can beat 2n cycles or bytes for some special case, do it here */
  switch (shCount)
    {
    case 15:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   lslx       1  1        bcde fgh0  ijkl mnop   a
       **   clra       1  1        bcde fgh0  0000 0000   a
       **   rola       1  1        bcde fgh0  0000 000a   0
       **   nega       1  1        bcde fgh0  aaaa aaaa   a
       **   tax        1  1        aaaa aaaa  aaaa aaaa   a
       ** total: 5 cycles, 5 bytes
       */
      rmwWithReg ("lsl", hc08_reg_x);
      loadRegFromConst (hc08_reg_a, zero);
      rmwWithReg ("rol", hc08_reg_a);
      rmwWithReg ("neg", hc08_reg_a);
      transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
      return;

    case 14:
    case 13:
    case 12:
    case 11:
    case 10:
    case 9:
    case 8:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   txa        1  1        abcd efgh  abcd efgh   ?
       **   (AccSRsh) <8 <8        abcd efgh  LSBresult   ?
       **   lsla       1  1        abcd efgh  ???? ????   a
       **   clrx       1  1        0000 0000  ???? ????   a
       **   rolx       1  1        0000 000a  ???? ????   0
       **   negx       1  1        aaaa aaaa  ???? ????   a
       **   rora       1  1        aaaa aaaa  LSBresult   0
       ** total: n-2 cycles, n-2 bytes (beats 2n cycles, 2n bytes (for n>=8))
       */
      transferRegReg (hc08_reg_x, hc08_reg_a, FALSE);
      AccSRsh (shCount - 8);
      rmwWithReg ("lsl", hc08_reg_a);
      loadRegFromConst (hc08_reg_x, zero);
      rmwWithReg ("rol", hc08_reg_x);
      rmwWithReg ("neg", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
      return;

    default:
      ;
    }

  /* asrx/rora is only 2 cycles and bytes, so an unrolled loop is often  */
  /* the fastest and shortest.                                           */
  for (i = 0; i < shCount; i++)
    {
      rmwWithReg ("asr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
    }
}

/*-----------------------------------------------------------------*/
/* XAccRsh - right shift register pair XA by known count           */
/*-----------------------------------------------------------------*/
static void
XAccRsh (int shCount, bool sign)
{
  int i;

  if (sign)
    {
      XAccSRsh (shCount);
      return;
    }

  shCount &= 0x000f;            // shCount : 0..f

  /* if we can beat 2n cycles or bytes for some special case, do it here */
  switch (shCount)
    {
    case 15:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   clra       1  1        abcd efgh  0000 0000   a
       **   lslx       1  1        bcde fgh0  0000 0000   a
       **   rola       1  1        bcde fgh0  0000 000a   0
       **   clrx       1  1        0000 0000  0000 000a   0
       ** total: 4 cycles, 4 bytes
       */
      loadRegFromConst (hc08_reg_x, zero);
      rmwWithReg ("lsl", hc08_reg_x);
      rmwWithReg ("rol", hc08_reg_a);
      loadRegFromConst (hc08_reg_a, zero);
      return;

    case 14:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   clra       1  1        abcd efgh  0000 0000   a
       **   lslx       1  1        bcde fgh0  0000 0000   a
       **   rola       1  1        bcde fgh0  0000 000a   0
       **   lslx       1  1        cdef gh00  0000 000a   b
       **   rola       1  1        cdef gh00  0000 00ab   0
       **   clrx       1  1        0000 0000  0000 00ab   0
       ** total: 6 cycles, 6 bytes
       */
      loadRegFromConst (hc08_reg_x, zero);
      rmwWithReg ("lsl", hc08_reg_x);
      rmwWithReg ("rol", hc08_reg_a);
      rmwWithReg ("lsl", hc08_reg_x);
      rmwWithReg ("rol", hc08_reg_a);
      loadRegFromConst (hc08_reg_a, zero);
      return;

    case 13:
    case 12:
    case 11:
    case 10:
    case 9:
    case 8:
      transferRegReg (hc08_reg_x, hc08_reg_a, FALSE);
      AccRsh (shCount - 8, FALSE);
      loadRegFromConst (hc08_reg_x, zero);
      return;

    case 7:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   lsla       1  1        abcd efgh  jklm nop0   i
       **   txa        1  1        abcd efgh  abcd efgh   i
       **   rola       1  1        abcd efgh  bcde fghi   a
       **   clrx       1  1        0000 0000  bcde fghi   a
       **   rolx       1  1        0000 000a  bcde fghi   0
       ** total: 5 cycles, 5 bytes (beats 14 cycles, 14 bytes)
       */
      rmwWithReg ("lsl", hc08_reg_a);
      transferRegReg (hc08_reg_x, hc08_reg_a, FALSE);
      rmwWithReg ("rol", hc08_reg_a);
      loadRegFromConst (hc08_reg_x, zero);
      rmwWithReg ("rol", hc08_reg_x);
      return;
    case 6:
      /*          bytes  cycles     reg x      reg a   carry
       **                          abcd efgh  ijkl mnop   ?
       **   lsla       1  1        abcd efgh  jklm nop0   i
       **   rolx       1  1        bcde fghi  jklm nop0   a
       **   rola       1  1        bcde fghi  klmn op0a   j
       **   rolx       1  1        cdef ghij  klmn op0a   b
       **   rola       1  1        cdef ghij  lmno p0ab   k
       **   and #3     2  2        cdef ghij  0000 00ab   k
       **   psha       1  2        cdef ghij  0000 00ab   k
       **   txa        1  1        cdef ghij  cdef ghij   k
       **   pula       1  2        0000 00ab  cdef ghij   k
       ** total: 12 cycles, 10 bytes (beats 12 bytes)
       */
    default:
      ;
    }

  /* lsrx/rora is only 2 cycles and bytes, so an unrolled loop is often  */
  /* the fastest and shortest.                                           */
  for (i = 0; i < shCount; i++)
    {
      rmwWithReg ("lsr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
    }

}


#if 0
/*-----------------------------------------------------------------*/
/* shiftR1Left2Result - shift right one byte from left to result   */
/*-----------------------------------------------------------------*/
static void
shiftR1Left2Result (operand * left, int offl, operand * result, int offr, int shCount, int sign)
{
  loadRegFromAop (hc08_reg_a, AOP (left), offl);
  /* shift right accumulator */
  AccRsh (shCount, sign);
  storeRegToAop (hc08_reg_a, AOP (result), offr);
}
#endif

/*-----------------------------------------------------------------*/
/* shiftL1Left2Result - shift left one byte from left to result    */
/*-----------------------------------------------------------------*/
static void
shiftL1Left2Result (operand * left, int offl, operand * result, int offr, int shCount)
{
  bool needpulla = pushRegIfSurv (hc08_reg_a);
  loadRegFromAop (hc08_reg_a, AOP (left), offl);
  /* shift left accumulator */
  AccLsh (shCount);
  storeRegToAop (hc08_reg_a, AOP (result), offr);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* movLeft2Result - move byte from left to result                  */
/*-----------------------------------------------------------------*/
static void
movLeft2Result (operand * left, int offl, operand * result, int offr, int sign)
{
  if (!sameRegs (AOP (left), AOP (result)) || (offl != offr))
    {
      transferAopAop (AOP (left), offl, AOP (result), offr);
    }
}


/*-----------------------------------------------------------------*/
/* shiftL2Left2Result - shift left two bytes from left to result   */
/*-----------------------------------------------------------------*/
static void
shiftL2Left2Result (operand * left, int offl, operand * result, int offr, int shCount)
{
  int i;
  bool needpula = FALSE;
  bool needpulx = FALSE;

  if (!IS_AOP_XA (AOP (left)) && !IS_AOP_A (AOP (left)))
    needpula = pushRegIfUsed (hc08_reg_a);
  else
    needpula = FALSE;
  if (!IS_AOP_XA (AOP (left)))
    needpulx = pushRegIfUsed (hc08_reg_x);
  else
    needpulx = FALSE;

  loadRegFromAop (hc08_reg_xa, AOP (left), offl);

  switch (shCount)
    {
    case 7:
      rmwWithReg ("lsr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
      transferRegReg (hc08_reg_a, hc08_reg_x, FALSE);
      rmwWithReg ("clr", hc08_reg_a);
      rmwWithReg ("ror", hc08_reg_a);
      break;
    default:
      for (i = 0; i < shCount; i++)
        {
          rmwWithReg ("lsl", hc08_reg_a);
          rmwWithReg ("rol", hc08_reg_x);
        }
    }
  storeRegToAop (hc08_reg_xa, AOP (result), offr);

  pullOrFreeReg (hc08_reg_x, needpulx);
  pullOrFreeReg (hc08_reg_a, needpula);

}


#if 0
/*-----------------------------------------------------------------*/
/* shiftR2Left2Result - shift right two bytes from left to result  */
/*-----------------------------------------------------------------*/
static void
shiftR2Left2Result (operand * left, int offl, operand * result, int offr, int shCount, int sign)
{
  int i;
  bool needpula = FALSE;
  bool needpulx = FALSE;

  needpula = pushRegIfUsed (hc08_reg_a);
  needpulx = pushRegIfUsed (hc08_reg_x);

  loadRegFromAop (hc08_reg_xa, AOP (left), offl);
  for (i = 0; i < shCount; i++)
    {
      if (sign)
        rmwWithReg ("asr", hc08_reg_x);
      else
        rmwWithReg ("lsr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
    }
  storeRegToAop (hc08_reg_xa, AOP (result), offl);

  pullOrFreeReg (hc08_reg_x, needpulx);
  pullOrFreeReg (hc08_reg_a, needpula);
}
#endif

#if 0
/*-----------------------------------------------------------------*/
/* shiftLLeftOrResult - shift left one byte from left, or to result */
/*-----------------------------------------------------------------*/
static void
shiftLLeftOrResult (operand * left, int offl, operand * result, int offr, int shCount)
{
  loadRegFromAop (hc08_reg_a, AOP (left), offl);
  /* shift left accumulator */
  AccLsh (shCount);
  /* or with result */
  accopWithAop ("ora", AOP (result), offr);
  /* back to result */
  storeRegToAop (hc08_reg_a, AOP (result), offr);
  hc08_freeReg (hc08_reg_a);
}
#endif

/*-----------------------------------------------------------------*/
/* shiftRLeftOrResult - shift right one byte from left,or to result */
/*-----------------------------------------------------------------*/
static void
shiftRLeftOrResult (operand * left, int offl, operand * result, int offr, int shCount)
{
  loadRegFromAop (hc08_reg_a, AOP (left), offl);
  /* shift left accumulator */
  AccRsh (shCount, FALSE);
  /* or with result */
  accopWithAop ("ora", AOP (result), offr);
  /* back to result */
  storeRegToAop (hc08_reg_a, AOP (result), offr);
  hc08_freeReg (hc08_reg_a);
}

/*-----------------------------------------------------------------*/
/* genlshOne - left shift a one byte quantity by known count       */
/*-----------------------------------------------------------------*/
static void
genlshOne (operand * result, operand * left, int shCount)
{
  D (emitcode (";     genlshOne", ""));

  shiftL1Left2Result (left, LSB, result, LSB, shCount);
}

/*-----------------------------------------------------------------*/
/* genlshTwo - left shift two bytes by known amount != 0           */
/*-----------------------------------------------------------------*/
static void
genlshTwo (operand * result, operand * left, int shCount)
{
  int size;
  bool needpulla, needpullx;

  D (emitcode (";     genlshTwo", ""));


  size = getDataSize (result);

  /* if shCount >= 8 */
  if (shCount >= 8)
    {
      shCount -= 8;

      needpulla = pushRegIfSurv (hc08_reg_a);
      if (size > 1)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), 0);
          AccLsh (shCount);
          storeRegToAop (hc08_reg_a, AOP (result), 1);
        }
      storeConstToAop (zero, AOP (result), LSB);
      pullOrFreeReg (hc08_reg_a, needpulla);
    }

  /*  1 <= shCount <= 7 */
  else
    {
      needpulla = pushRegIfSurv (hc08_reg_a);
      needpullx = pushRegIfSurv (hc08_reg_x);
      loadRegFromAop (hc08_reg_xa, AOP (left), 0);
      XAccLsh (shCount);
      storeRegToFullAop (hc08_reg_xa, AOP (result), 0);
      pullOrFreeReg (hc08_reg_x, needpullx);
      pullOrFreeReg (hc08_reg_a, needpulla);
    }
}

/*-----------------------------------------------------------------*/
/* shiftLLong - shift left one long from left to result            */
/* offr = LSB or MSB16                                             */
/*-----------------------------------------------------------------*/
static void
shiftLLong (operand * left, operand * result, int offr)
{
//  char *l;
//  int size = AOP_SIZE (result);

  bool needpula = FALSE;
  bool needpulx = FALSE;

  needpula = pushRegIfUsed (hc08_reg_a);
  needpulx = pushRegIfUsed (hc08_reg_x);

  loadRegFromAop (hc08_reg_xa, AOP (left), LSB);
  rmwWithReg ("lsl", hc08_reg_a);
  rmwWithReg ("rol", hc08_reg_x);

  if (offr == LSB)
    {
      storeRegToAop (hc08_reg_xa, AOP (result), offr);
      loadRegFromAop (hc08_reg_xa, AOP (left), MSB24);
      rmwWithReg ("rol", hc08_reg_a);
      rmwWithReg ("rol", hc08_reg_x);
      storeRegToAop (hc08_reg_xa, AOP (result), offr + 2);
    }
  else if (offr == MSB16)
    {
      storeRegToAop (hc08_reg_a, AOP (result), offr);
      loadRegFromAop (hc08_reg_a, AOP (left), MSB24);
      storeRegToAop (hc08_reg_x, AOP (result), offr + 1);
      rmwWithReg ("rol", hc08_reg_a);
      storeRegToAop (hc08_reg_a, AOP (result), offr + 2);
      storeConstToAop (zero, AOP (result), 0);
    }

  pullOrFreeReg (hc08_reg_x, needpulx);
  pullOrFreeReg (hc08_reg_a, needpula);
}

/*-----------------------------------------------------------------*/
/* genlshFour - shift four byte by a known amount != 0             */
/*-----------------------------------------------------------------*/
static void
genlshFour (operand * result, operand * left, int shCount)
{
  int size;

  D (emitcode (";     genlshFour", ""));

  size = AOP_SIZE (result);

  /* TODO: deal with the &result == &left case */

  /* if shifting more that 3 bytes */
  if (shCount >= 24)
    {
      shCount -= 24;
      if (shCount)
        /* lowest order of left goes to the highest
           order of the destination */
        shiftL1Left2Result (left, LSB, result, MSB32, shCount);
      else
        movLeft2Result (left, LSB, result, MSB32, 0);
      storeConstToAop (zero, AOP (result), LSB);
      storeConstToAop (zero, AOP (result), MSB16);
      storeConstToAop (zero, AOP (result), MSB24);
      return;
    }

  /* more than two bytes */
  else if (shCount >= 16)
    {
      /* lower order two bytes goes to higher order two bytes */
      shCount -= 16;
      /* if some more remaining */
      if (shCount)
        shiftL2Left2Result (left, LSB, result, MSB24, shCount);
      else
        {
          movLeft2Result (left, MSB16, result, MSB32, 0);
          movLeft2Result (left, LSB, result, MSB24, 0);
        }
      storeConstToAop (zero, AOP (result), LSB);
      storeConstToAop (zero, AOP (result), MSB16);
      return;
    }

  /* if more than 1 byte */
  else if (shCount >= 8)
    {
      /* lower order three bytes goes to higher order  three bytes */
      shCount -= 8;
      if (size == 2)
        {
          if (shCount)
            shiftL1Left2Result (left, LSB, result, MSB16, shCount);
          else
            movLeft2Result (left, LSB, result, MSB16, 0);
        }
      else
        {
          /* size = 4 */
          if (shCount == 0)
            {
              movLeft2Result (left, MSB24, result, MSB32, 0);
              movLeft2Result (left, MSB16, result, MSB24, 0);
              movLeft2Result (left, LSB, result, MSB16, 0);
              storeConstToAop (zero, AOP (result), LSB);
            }
          else if (shCount == 1)
            shiftLLong (left, result, MSB16);
          else
            {
              shiftL2Left2Result (left, MSB16, result, MSB24, shCount);
              shiftL1Left2Result (left, LSB, result, MSB16, shCount);
              shiftRLeftOrResult (left, LSB, result, MSB24, 8 - shCount);
              storeConstToAop (zero, AOP (result), LSB);
            }
        }
    }

  /* 1 <= shCount <= 7 */
  else if (shCount <= 2)
    {
      shiftLLong (left, result, LSB);
      if (shCount == 2)
        shiftLLong (result, result, LSB);
    }
  /* 3 <= shCount <= 7, optimize */
  else
    {
      shiftL2Left2Result (left, MSB24, result, MSB24, shCount);
      shiftRLeftOrResult (left, MSB16, result, MSB24, 8 - shCount);
      shiftL2Left2Result (left, LSB, result, LSB, shCount);
    }
}

/*-----------------------------------------------------------------*/
/* genLeftShiftLiteral - left shifting by known count              */
/*-----------------------------------------------------------------*/
static void
genLeftShiftLiteral (operand * left, operand * right, operand * result, iCode * ic)
{
  int shCount = (int) ulFromVal (AOP (right)->aopu.aop_lit);
  int size;

  D (emitcode (";     genLeftShiftLiteral", ""));

  freeAsmop (right, NULL, ic, TRUE);

  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

//  size = getSize (operandType (result));
  size = AOP_SIZE (result);

#if VIEW_SIZE
  DD (emitcode ("; shift left ", "result %d, left %d", size, AOP_SIZE (left)));
#endif

  if (shCount == 0)
    {
      while (size--)
        transferAopAop (AOP (left), size, AOP (result), size);
    }
  else if (shCount >= (size * 8))
    {
      while (size--)
        storeConstToAop (zero, AOP (result), size);
    }
  else
    {
      switch (size)
        {
        case 1:
          genlshOne (result, left, shCount);
          break;

        case 2:
          genlshTwo (result, left, shCount);
          break;

        case 4:
          genlshFour (result, left, shCount);
          break;
        default:
          werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "*** ack! mystery literal shift!\n");
          break;
        }
    }
  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genLeftShift - generates code for left shifting                 */
/*-----------------------------------------------------------------*/
static void
genLeftShift (iCode * ic)
{
  operand *left, *right, *result;
  int size, offset;
  symbol *tlbl, *tlbl1;
  char *shift;
  asmop *aopResult;
  bool needpullcountreg;
  reg_info *countreg = NULL;

  D (emitcode (";     genLeftShift", ""));

  right = IC_RIGHT (ic);
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  aopOp (right, ic, FALSE);

  /* if the shift count is known then do it
     as efficiently as possible */
  if (AOP_TYPE (right) == AOP_LIT)
    {
      genLeftShiftLiteral (left, right, result, ic);
      return;
    }

  /* shift count is unknown then we have to form
     a loop get the loop count in X : Note: we take
     only the lower order byte since shifting
     more that 32 bits make no sense anyway, ( the
     largest size of an object can be only 32 bits ) */

  aopOp (result, ic, FALSE);
  aopOp (left, ic, FALSE);
  aopResult = AOP (result);

  if (sameRegs (AOP (right), AOP (result)) || IS_AOP_XA (AOP (result)) || isOperandVolatile (result, FALSE))
    aopResult = forceStackedAop (AOP (result), sameRegs (AOP (left), AOP (result)));

  /* now move the left to the result if they are not the
     same */
  if (!sameRegs (AOP (left), aopResult))
    {
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (left), offset, aopResult, offset);
          offset++;
        }
    }
  freeAsmop (left, NULL, ic, TRUE);
  AOP (result) = aopResult;

  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  size = AOP_SIZE (result);
  offset = 0;
  tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  countreg = (hc08_reg_x->isDead && !IS_AOP_X (AOP (result)) && !IS_AOP_XA (AOP (result)) && !IS_AOP_HX (AOP (result))) ? hc08_reg_x : hc08_reg_a;

  needpullcountreg = pushRegIfSurv (countreg);
  loadRegFromAop (countreg, AOP (right), 0);
  emitcode (countreg == hc08_reg_a ? "tsta" : "tstx", "");
  regalloc_dry_run_cost++;
  emitBranch ("beq", tlbl1);
  if (!regalloc_dry_run)
    emitLabel (tlbl);

  shift = "lsl";
  if (countreg == hc08_reg_a && (IS_AOP_A (AOP (result)) || IS_AOP_XA (AOP (result))))
    pushReg (hc08_reg_a, TRUE);
  for (offset = 0; offset < size; offset++)
    {
      rmwWithAop (shift, AOP (result), offset);
      shift = "rol";
    }
  if (countreg == hc08_reg_a && (IS_AOP_A (AOP (result)) || IS_AOP_XA (AOP (result))))
    pullReg (hc08_reg_a);
  if (!regalloc_dry_run)
    emitcode (countreg == hc08_reg_a ? "dbnza" : "dbnzx", "%05d$", labelKey2num (tlbl->key));
  regalloc_dry_run_cost += 2;
  if (!regalloc_dry_run)
    emitLabel (tlbl1);
  pullOrFreeReg (countreg, needpullcountreg);

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genrshOne - right shift a one byte quantity by known count      */
/*-----------------------------------------------------------------*/
static void
genrshOne (operand * result, operand * left, int shCount, int sign)
{
  bool needpulla;
  D (emitcode (";     genrshOne", ""));
  needpulla = pushRegIfSurv (hc08_reg_a);
  loadRegFromAop (hc08_reg_a, AOP (left), 0);
  AccRsh (shCount, sign);
  storeRegToFullAop (hc08_reg_a, AOP (result), sign);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genrshTwo - right shift two bytes by known amount != 0          */
/*-----------------------------------------------------------------*/
static void
genrshTwo (operand * result, operand * left, int shCount, int sign)
{
  bool needpulla, needpullx;
  D (emitcode (";     genrshTwo", ""));

  /* if shCount >= 8 */
  if (shCount >= 8)
    {
      if (shCount != 8 || sign)
        {
          needpulla = pushRegIfSurv (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, AOP (left), 1);
          AccRsh (shCount - 8, sign);
          storeRegToFullAop (hc08_reg_a, AOP (result), sign);
          pullOrFreeReg (hc08_reg_a, needpulla);
        }
      else
        {
          transferAopAop (AOP (left), 1, AOP (result), 0);
          storeConstToAop (zero, AOP (result), 1);
        }
    }

  /*  1 <= shCount <= 7 */
  else
    {
      needpulla = pushRegIfSurv (hc08_reg_a);
      needpullx = pushRegIfSurv (hc08_reg_x);
      loadRegFromAop (hc08_reg_xa, AOP (left), 0);
      XAccRsh (shCount, sign);
      storeRegToAop (hc08_reg_xa, AOP (result), 0);
      pullOrFreeReg (hc08_reg_x, needpullx);
      pullOrFreeReg (hc08_reg_a, needpulla);
    }
}

/*-----------------------------------------------------------------*/
/* shiftRLong - shift right one long from left to result           */
/* offl = LSB or MSB16                                             */
/*-----------------------------------------------------------------*/
static void
shiftRLong (operand * left, int offl, operand * result, int sign)
{
  bool needpula = pushRegIfSurv (hc08_reg_a);
  bool needpulx = pushRegIfSurv (hc08_reg_x);

  if (offl == LSB)
    {
      loadRegFromAop (hc08_reg_xa, AOP (left), MSB24);
      if (sign)
        rmwWithReg ("asr", hc08_reg_x);
      else
        rmwWithReg ("lsr", hc08_reg_x);
      rmwWithReg ("ror", hc08_reg_a);
      storeRegToAop (hc08_reg_xa, AOP (result), MSB24);
      loadRegFromAop (hc08_reg_xa, AOP (left), LSB);
    }
  else if (offl == MSB16)
    {
      loadRegFromAop (hc08_reg_a, AOP (left), MSB32);
      if (sign)
        rmwWithReg ("asr", hc08_reg_a);
      else
        rmwWithReg ("lsr", hc08_reg_a);
      loadRegFromAop (hc08_reg_x, AOP (left), MSB24);
      storeRegToAop (hc08_reg_a, AOP (result), MSB24);
      loadRegFromAop (hc08_reg_a, AOP (left), MSB16);
    }

  rmwWithReg ("ror", hc08_reg_x);
  rmwWithReg ("ror", hc08_reg_a);
  storeRegToAop (hc08_reg_xa, AOP (result), LSB);

  if (offl == MSB16)
    {
      if (sign)
        {
          loadRegFromAop (hc08_reg_a, AOP (left), MSB24);
          storeRegSignToUpperAop (hc08_reg_a, AOP (result), MSB32, sign);
        }
      else
        {
          storeConstToAop (zero, AOP (result), MSB32);
        }
    }

  pullOrFreeReg (hc08_reg_x, needpulx);
  pullOrFreeReg (hc08_reg_a, needpula);
}

/*-----------------------------------------------------------------*/
/* genrshFour - shift four byte by a known amount != 0             */
/*-----------------------------------------------------------------*/
static void
genrshFour (operand * result, operand * left, int shCount, int sign)
{
  bool needpulla = FALSE;
  bool needpullx = FALSE;

  /* TODO: handle cases where left == result */

  D (emitcode (";     genrshFour", ""));

  /* if shifting more that 3 bytes */
  if (shCount >= 24)
    {
      needpulla = pushRegIfSurv (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, AOP (left), 3);
      AccRsh (shCount - 24, sign);
      storeRegToFullAop (hc08_reg_a, AOP (result), sign);
    }
  else if (shCount >= 16)
    {
      needpulla = pushRegIfSurv (hc08_reg_a);
      needpullx = pushRegIfSurv (hc08_reg_x);
      loadRegFromAop (hc08_reg_xa, AOP (left), 2);
      XAccRsh (shCount - 16, sign);
      storeRegToFullAop (hc08_reg_xa, AOP (result), sign);
    }
  else if (shCount >= 8)
    {
      if (shCount == 1)
        {
          shiftRLong (left, MSB16, result, sign);
          return;
        }
      else if (shCount == 8)
        {
          needpulla = pushRegIfSurv (hc08_reg_a);
          transferAopAop (AOP (left), 1, AOP (result), 0);
          transferAopAop (AOP (left), 2, AOP (result), 1);
          loadRegFromAop (hc08_reg_a, AOP (left), 3);
          storeRegToAop (hc08_reg_a, AOP (result), 2);
          storeRegSignToUpperAop (hc08_reg_a, AOP (result), 3, sign);
        }
      else if (shCount == 9)
        {
          shiftRLong (left, MSB16, result, sign);
          return;
        }
      else
        {
          needpulla = pushRegIfSurv (hc08_reg_a);
          needpullx = pushRegIfSurv (hc08_reg_x);
          loadRegFromAop (hc08_reg_xa, AOP (left), 1);
          XAccRsh (shCount - 8, FALSE);
          storeRegToAop (hc08_reg_xa, AOP (result), 0);
          loadRegFromAop (hc08_reg_x, AOP (left), 3);
          loadRegFromConst (hc08_reg_a, zero);
          XAccRsh (shCount - 8, sign);
          accopWithAop ("ora", AOP (result), 1);
          storeRegToAop (hc08_reg_xa, AOP (result), 1);
          storeRegSignToUpperAop (hc08_reg_x, AOP (result), 3, sign);
        }
    }
  else
    {
      /* 1 <= shCount <= 7 */
      if (shCount == 1)
        {
          shiftRLong (left, LSB, result, sign);
          return;
        }
      else
        {
          needpulla = pushRegIfSurv (hc08_reg_a);
          needpullx = pushRegIfSurv (hc08_reg_x);
          loadRegFromAop (hc08_reg_xa, AOP (left), 0);
          XAccRsh (shCount, FALSE);
          storeRegToAop (hc08_reg_xa, AOP (result), 0);
          loadRegFromAop (hc08_reg_a, AOP (left), 2);
          AccLsh (8 - shCount);
          accopWithAop ("ora", AOP (result), 1);
          storeRegToAop (hc08_reg_a, AOP (result), 1);
          loadRegFromAop (hc08_reg_xa, AOP (left), 2);
          XAccRsh (shCount, sign);
          storeRegToAop (hc08_reg_xa, AOP (result), 2);
        }
    }
  pullOrFreeReg (hc08_reg_x, needpullx);
  pullOrFreeReg (hc08_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genRightShiftLiteral - right shifting by known count            */
/*-----------------------------------------------------------------*/
static void
genRightShiftLiteral (operand * left, operand * right, operand * result, iCode * ic, int sign)
{
  int shCount = (int) ulFromVal (AOP (right)->aopu.aop_lit);
  int size;

  D (emitcode (";     genRightShiftLiteral", ""));

  freeAsmop (right, NULL, ic, TRUE);

  aopOp (left, ic, FALSE);
  aopOp (result, ic, FALSE);

#if VIEW_SIZE
  DD (emitcode ("; shift right ", "result %d, left %d", AOP_SIZE (result), AOP_SIZE (left)));
#endif

  size = getDataSize (left);
  /* test the LEFT size !!! */

  /* I suppose that the left size >= result size */
  if (shCount == 0)
    {
      size = getDataSize (result);
      while (size--)
        transferAopAop (AOP (left), size, AOP (result), size);
    }
  else if (shCount >= (size * 8))
    {
      bool needpulla = pushRegIfSurv (hc08_reg_a);
      if (sign)
        {
          
          /* get sign in acc.7 */
          loadRegFromAop (hc08_reg_a, AOP (left), size - 1);
        }
      addSign (result, LSB, sign);
      pullOrFreeReg (hc08_reg_a, needpulla);
    }
  else
    {
      switch (size)
        {
        case 1:
          genrshOne (result, left, shCount, sign);
          break;

        case 2:
          genrshTwo (result, left, shCount, sign);
          break;

        case 4:
          genrshFour (result, left, shCount, sign);
          break;
        default:
          wassertl (0, "Invalid operand size in right shift.");
          break;
        }
    }
  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genRightShift - generate code for right shifting                */
/*-----------------------------------------------------------------*/
static void
genRightShift (iCode * ic)
{
  operand *right, *left, *result;
  sym_link *retype;
  int size, offset;
  symbol *tlbl, *tlbl1;
  char *shift;
  bool sign;
  asmop *aopResult;
  bool needpullx;

  D (emitcode (";     genRightShift", ""));

  /* if signed then we do it the hard way preserve the
     sign bit moving it inwards */
  retype = getSpec (operandType (IC_RESULT (ic)));
  sign = !SPEC_USIGN (retype);

  /* signed & unsigned types are treated the same : i.e. the
     signed is NOT propagated inwards : quoting from the
     ANSI - standard : "for E1 >> E2, is equivalent to division
     by 2**E2 if unsigned or if it has a non-negative value,
     otherwise the result is implementation defined ", MY definition
     is that the sign does not get propagated */

  right = IC_RIGHT (ic);
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  aopOp (right, ic, FALSE);

  /* if the shift count is known then do it
     as efficiently as possible */
  if (AOP_TYPE (right) == AOP_LIT)
    {
      genRightShiftLiteral (left, right, result, ic, sign);
      return;
    }

  /* shift count is unknown then we have to form
     a loop get the loop count in X : Note: we take
     only the lower order byte since shifting
     more that 32 bits make no sense anyway, ( the
     largest size of an object can be only 32 bits ) */

  aopOp (result, ic, FALSE);
  aopOp (left, ic, FALSE);
  aopResult = AOP (result);

  if (sameRegs (AOP (right), AOP (result)) || IS_AOP_XA (AOP (result)) || isOperandVolatile (result, FALSE))
    aopResult = forceStackedAop (AOP (result), sameRegs (AOP (left), AOP (result)));

  /* now move the left to the result if they are not the
     same */
  if (!sameRegs (AOP (left), aopResult))
    {
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (left), offset, aopResult, offset);
          offset++;
        }
    }
  freeAsmop (left, NULL, ic, TRUE);
  AOP (result) = aopResult;

  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  size = AOP_SIZE (result);
  offset = 0;
  tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  needpullx = pushRegIfSurv (hc08_reg_x);
  loadRegFromAop (hc08_reg_x, AOP (right), 0);
  emitcode ("tstx", "");
  regalloc_dry_run_cost++;
  emitBranch ("beq", tlbl1);
  if (!regalloc_dry_run)
    emitLabel (tlbl);

  shift = sign ? "asr" : "lsr";
  for (offset = size - 1; offset >= 0; offset--)
    {
      rmwWithAop (shift, AOP (result), offset);
      shift = "ror";
    }
  rmwWithReg ("dec", hc08_reg_x);
  emitBranch ("bne", tlbl);
  if (!regalloc_dry_run)
    emitLabel (tlbl1);
  pullOrFreeReg (hc08_reg_x, needpullx);

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genUnpackBits - generates code for unpacking bits               */
/*-----------------------------------------------------------------*/
static void
genUnpackBits (operand * result, iCode * ifx)
{
  int offset = 0;               /* result byte offset */
  int rsize;                    /* result size */
  int rlen = 0;                 /* remaining bitfield length */
  sym_link *etype;              /* bitfield type information */
  int blen;                     /* bitfield length */
  int bstr;                     /* bitfield starting bit within byte */

  D (emitcode (";     genUnpackBits", ""));

  etype = getSpec (operandType (result));
  rsize = getSize (operandType (result));
  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  if (ifx && blen <= 8)
    {
      emitcode ("lda", ",x");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      if (blen < 8)
        {
          emitcode ("and", "#0x%02x", (((unsigned char) - 1) >> (8 - blen)) << bstr);
          regalloc_dry_run_cost += 2;
        }
      genIfxJump (ifx, "a");
      return;
    }
  wassert (!ifx);

  /* If the bitfield length is less than a byte */
  if (blen < 8)
    {
      emitcode ("lda", ",x");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      AccRsh (bstr, FALSE);
      emitcode ("and", "#0x%02x", ((unsigned char) - 1) >> (8 - blen));
      regalloc_dry_run_cost += 2;
      if (!SPEC_USIGN (etype))
        {
          /* signed bitfield */
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          emitcode ("bit", "#0x%02x", 1 << (blen - 1));
          if (!regalloc_dry_run)
            emitcode ("beq", "%05d$", labelKey2num (tlbl->key));
          emitcode ("ora", "#0x%02x", (unsigned char) (0xff << blen));
          regalloc_dry_run_cost += 6;
          if (!regalloc_dry_run)
            emitLabel (tlbl);
        }
      storeRegToAop (hc08_reg_a, AOP (result), offset++);
      goto finish;
    }

  /* Bit field did not fit in a byte. Copy all
     but the partial byte at the end.  */
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      emitcode ("lda", ",x");
      regalloc_dry_run_cost++;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      storeRegToAop (hc08_reg_a, AOP (result), offset);
      offset++;
      if (rlen > 8)
        {
          emitcode ("aix", "#1");
          regalloc_dry_run_cost *= 2;
        }
    }

  /* Handle the partial byte at the end */
  if (rlen)
    {
      emitcode ("lda", ",x");
      emitcode ("and", "#0x%02x", ((unsigned char) - 1) >> (8 - rlen));
      regalloc_dry_run_cost += 3;
      if (!SPEC_USIGN (etype))
        {
          /* signed bitfield */
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          emitcode ("bit", "#0x%02x", 1 << (rlen - 1));
          if (!regalloc_dry_run)
            emitcode ("beq", "%05d$", labelKey2num (tlbl->key));
          emitcode ("ora", "#0x%02x", (unsigned char) (0xff << rlen));
         regalloc_dry_run_cost += 6;
         if (!regalloc_dry_run)
            emitLabel (tlbl);
        }
      storeRegToAop (hc08_reg_a, AOP (result), offset++);
    }

finish:
  if (offset < rsize)
    {
      rsize -= offset;
      if (SPEC_USIGN (etype))
        {
          while (rsize--)
            storeConstToAop (zero, AOP (result), offset++);
        }
      else
        {
          /* signed bitfield: sign extension with 0x00 or 0xff */
          emitcode ("rola", "");
          emitcode ("clra", "");
          emitcode ("sbc", zero);
          regalloc_dry_run_cost += 4;

          while (rsize--)
            storeRegToAop (hc08_reg_a, AOP (result), offset++);
        }
    }
}


/*-----------------------------------------------------------------*/
/* genUnpackBitsImmed - generates code for unpacking bits          */
/*-----------------------------------------------------------------*/
static void
genUnpackBitsImmed (operand * left, operand * result, iCode * ic, iCode * ifx)
{
  int size;
  int offset = 0;               /* result byte offset */
  int rsize;                    /* result size */
  int rlen = 0;                 /* remaining bitfield length */
  sym_link *etype;              /* bitfield type information */
  int blen;                     /* bitfield length */
  int bstr;                     /* bitfield starting bit within byte */
  asmop *derefaop;
  bool delayed_a = FALSE;
  bool assigned_a = FALSE;

  D (emitcode (";     genUnpackBitsImmed", ""));

  aopOp (result, ic, TRUE);
  size = AOP_SIZE (result);

  derefaop = aopDerefAop (AOP (left));
  freeAsmop (left, NULL, ic, TRUE);
  derefaop->size = size;

  etype = getSpec (operandType (result));
  rsize = getSize (operandType (result));
  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  /* if the bitfield is a single bit in the direct page */
  if (blen == 1 && derefaop->type == AOP_DIR)
    {
      if (!ifx && bstr)
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          loadRegFromConst (hc08_reg_a, zero);
          if (!regalloc_dry_run)
            emitcode ("brclr", "#%d,%s,%05d$", bstr, aopAdrStr (derefaop, 0, FALSE), labelKey2num ((tlbl->key)));
          regalloc_dry_run_cost += 3;
          if (SPEC_USIGN (etype))
            rmwWithReg ("inc", hc08_reg_a);
          else
            rmwWithReg ("dec", hc08_reg_a);
          if (!regalloc_dry_run)
            emitLabel (tlbl);
          storeRegToAop (hc08_reg_a, AOP (result), offset);
          if (AOP_TYPE (result) == AOP_REG && AOP(result)->aopu.aop_reg[offset]->rIdx == A_IDX)
            assigned_a = TRUE;
          hc08_freeReg (hc08_reg_a);
          offset++;
          goto finish;
        }
      else if (ifx)
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          symbol *jlbl;
          char *inst;

          if (IC_TRUE (ifx))
            {
              jlbl = IC_TRUE (ifx);
              inst = "brclr";
            }
          else
            {
              jlbl = IC_FALSE (ifx);
              inst = "brset";
            }
          if (!regalloc_dry_run)
            emitcode (inst, "#%d,%s,%05d$", bstr, aopAdrStr (derefaop, 0, FALSE), labelKey2num ((tlbl->key)));
          regalloc_dry_run_cost += 3;
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            emitLabel (tlbl);
          ifx->generated = 1;
          offset++;
          goto finish;
        }
    }

  /* If the bitfield length is less than a byte */
  if (blen < 8)
    {
      loadRegFromAop (hc08_reg_a, derefaop, 0);
      if (!ifx)
        {
          AccRsh (bstr, FALSE);
          emitcode ("and", "#0x%02x", ((unsigned char) - 1) >> (8 - blen));
          regalloc_dry_run_cost += 2;
          hc08_dirtyReg (hc08_reg_a, FALSE);
          if (!SPEC_USIGN (etype))
            {
              /* signed bitfield */
              symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

              emitcode ("bit", "#0x%02x", 1 << (blen - 1));
              if (!regalloc_dry_run)
                emitcode ("beq", "%05d$", labelKey2num (tlbl->key));
              emitcode ("ora", "#0x%02x", (unsigned char) (0xff << blen));
              regalloc_dry_run_cost += 6;
              if (!regalloc_dry_run)
                emitLabel (tlbl);
            }
          storeRegToAop (hc08_reg_a, AOP (result), offset);
          if (AOP_TYPE (result) == AOP_REG && AOP(result)->aopu.aop_reg[offset]->rIdx == A_IDX)
            assigned_a = TRUE;
        }
      else
        {
          emitcode ("and", "#0x%02x", (((unsigned char) - 1) >> (8 - blen)) << bstr);
          regalloc_dry_run_cost += 2;
          hc08_dirtyReg (hc08_reg_a, FALSE);
        }
      offset++;
      goto finish;
    }

  /* Bit field did not fit in a byte. Copy all
     but the partial byte at the end.  */
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      if (assigned_a && !delayed_a)
        {
          pushReg (hc08_reg_a, TRUE);
          delayed_a = TRUE;
        }
      loadRegFromAop (hc08_reg_a, derefaop, size - offset - 1);
      if (!ifx)
        {
          storeRegToAop (hc08_reg_a, AOP (result), offset);
          if (AOP_TYPE (result) == AOP_REG && AOP(result)->aopu.aop_reg[offset]->rIdx == A_IDX)
            assigned_a = TRUE;
        }
      else
        {
          emitcode ("tsta", "");
          regalloc_dry_run_cost++;
        }
      offset++;
    }

  /* Handle the partial byte at the end */
  if (rlen)
    {
      if (assigned_a && !delayed_a)
        {
          pushReg (hc08_reg_a, TRUE);
          delayed_a = TRUE;
        }
      loadRegFromAop (hc08_reg_a, derefaop, size - offset - 1);
      emitcode ("and", "#0x%02x", ((unsigned char) - 1) >> (8 - rlen));
      regalloc_dry_run_cost += 2;
      if (!SPEC_USIGN (etype))
        {
          /* signed bitfield */
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          emitcode ("bit", "#0x%02x", 1 << (rlen - 1));
          if (!regalloc_dry_run)
            emitcode ("beq", "%05d$", labelKey2num (tlbl->key));
          emitcode ("ora", "#0x%02x", (unsigned char) (0xff << rlen));
          regalloc_dry_run_cost += 6;
          if (!regalloc_dry_run)
            emitLabel (tlbl);
        }
      storeRegToAop (hc08_reg_a, AOP (result), offset);
      if (AOP_TYPE (result) == AOP_REG && AOP(result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        assigned_a = TRUE;
      offset++;
    }

finish:
  if (offset < rsize)
    {
      rsize -= offset;
      if (SPEC_USIGN (etype))
        {
          while (rsize--)
            storeConstToAop (zero, AOP (result), offset++);
        }
      else
        {
          if (assigned_a && !delayed_a)
            {
              pushReg (hc08_reg_a, TRUE);
              delayed_a = TRUE;
            }

          /* signed bitfield: sign extension with 0x00 or 0xff */
          emitcode ("rola", "");
          emitcode ("clra", "");
          emitcode ("sbc", zero);
          regalloc_dry_run_cost += 4;

          while (rsize--)
            storeRegToAop (hc08_reg_a, AOP (result), offset++);
        }
    }

  freeAsmop (NULL, derefaop, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);

  if (ifx && !ifx->generated)
    {
      genIfxJump (ifx, "a");
    }
  if (delayed_a)
    pullReg (hc08_reg_a);
}


/*-----------------------------------------------------------------*/
/* genDataPointerGet - generates code when ptr offset is known     */
/*-----------------------------------------------------------------*/
static void
genDataPointerGet (operand * left, operand * result, iCode * ic, iCode * ifx)
{
  int size;
  asmop *derefaop;

  D (emitcode (";     genDataPointerGet", ""));

  aopOp (result, ic, TRUE);
  size = AOP_SIZE (result);

  derefaop = aopDerefAop (AOP (left));
  freeAsmop (left, NULL, ic, TRUE);
  derefaop->size = size;

  while (size--)
    {
      if (!ifx)
        transferAopAop (derefaop, size, AOP (result), size);
      else
        loadRegFromAop (hc08_reg_a, derefaop, size);
    }

  freeAsmop (NULL, derefaop, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);

  if (ifx && !ifx->generated)
    {
      genIfxJump (ifx, "a");
    }
}


/*-----------------------------------------------------------------*/
/* genPointerGet - generate code for pointer get                   */
/*-----------------------------------------------------------------*/
static void
genPointerGet (iCode * ic, iCode * pi, iCode * ifx)
{
  operand *left = IC_LEFT (ic);
  operand *result = IC_RESULT (ic);
  int size, offset;
  sym_link *retype = getSpec (operandType (result));
  bool postH = FALSE;
  bool postX = FALSE;

  D (emitcode (";     genPointerGet", ""));

  if (getSize (operandType (result)) > 1)
    ifx = NULL;

  aopOp (left, ic, FALSE);

  /* if left is rematerialisable and
     result is not bit variable type */
  if (AOP_TYPE (left) == AOP_IMMD || AOP_TYPE (left) == AOP_LIT)
    {
      if (!IS_BITVAR (retype))
        {
          genDataPointerGet (left, result, ic, ifx);
          return;
        }
      else
        {
          genUnpackBitsImmed (left, result, ic, ifx);
          return;
        }
    }

  /* if the operand is already in hx
     then we do nothing else we move the value to hx */
  if (AOP_TYPE (left) != AOP_STR)
    {
      /* if this is remateriazable */
      loadRegFromAop (hc08_reg_hx, AOP (left), 0);
    }

  /* so hx now contains the address */
  aopOp (result, ic, FALSE);


  if (AOP_TYPE (result) == AOP_REG && pi)
    { 
      int i;
      for (i = 0; i < AOP_SIZE (result); i++)
        AOP (result)->aopu.aop_reg[i]->isDead = TRUE;
    }

  /* if bit then unpack */
  if (IS_BITVAR (retype))
    genUnpackBits (result, ifx);
  else
    {
      size = AOP_SIZE (result);
      offset = size - 1;

      while (size--)
        {
          accopWithMisc ("lda", ",x");
          if (size || pi)
            {
              emitcode ("aix", "#1");
              regalloc_dry_run_cost += 2;
              hc08_dirtyReg (hc08_reg_hx, FALSE);
            }
          if (!ifx && (pi || size) && AOP_TYPE (result) == AOP_REG && AOP (result)->aopu.aop_reg[offset] == hc08_reg_h)
            {
              pushReg(hc08_reg_a, FALSE);
              postH = TRUE;
            }
          else if (!ifx && (pi || size) && AOP_TYPE (result) == AOP_REG && AOP (result)->aopu.aop_reg[offset] == hc08_reg_x)
            {
              pushReg(hc08_reg_a, FALSE);
              postX = TRUE;
            }
          else if (!ifx)
            storeRegToAop (hc08_reg_a, AOP (result), offset);
          offset--;
        }
    }

  freeAsmop (left, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);

  if (pi)
    {
      aopOp (IC_RESULT (pi), pi, FALSE);
      storeRegToAop (hc08_reg_hx, AOP (IC_RESULT (pi)), 0);
      freeAsmop (IC_RESULT (pi), NULL, pi, TRUE);
      pi->generated = 1;
    }

  if (postX)
    pullReg (hc08_reg_x);
  if (postH)
    pullReg (hc08_reg_h);

  if (ifx && !ifx->generated)
    {
      genIfxJump (ifx, "a");
    }

D (emitcode (";     done", ""));
}

/*-----------------------------------------------------------------*/
/* genPackBits - generates code for packed bit storage             */
/*-----------------------------------------------------------------*/
static void
genPackBits (sym_link * etype, operand * right)
{
  int offset = 0;               /* source byte offset */
  int rlen = 0;                 /* remaining bitfield length */
  int blen;                     /* bitfield length */
  int bstr;                     /* bitfield starting bit within byte */
  int litval;                   /* source literal value (if AOP_LIT) */
  unsigned char mask;           /* bitmask within current byte */
  int xoffset = 0;

  D (emitcode (";     genPackBits", ""));

  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  /* If the bitfield length is less than a byte */
  if (blen < 8)
    {
      mask = ((unsigned char) (0xFF << (blen + bstr)) | (unsigned char) (0xFF >> (8 - bstr)));

      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* Case with a bitfield length <8 and literal source
           */
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval <<= bstr;
          litval &= (~mask) & 0xff;

          emitcode ("lda", ",x");
          regalloc_dry_run_cost++;
          if ((mask | litval) != 0xff)
            {
              emitcode ("and", "#0x%02x", mask);
              regalloc_dry_run_cost += 2;
            }
          if (litval)
            {
              emitcode ("ora", "#0x%02x", litval);
              regalloc_dry_run_cost += 2;
            }
          hc08_dirtyReg (hc08_reg_a, FALSE);
          emitcode ("sta", ",x");
          regalloc_dry_run_cost++;

          hc08_freeReg (hc08_reg_a);
          return;
        }

      /* Case with a bitfield length < 8 and arbitrary source
       */
      loadRegFromAop (hc08_reg_a, AOP (right), 0);
      /* shift and mask source value */
      AccLsh (bstr);
      emitcode ("and", "#0x%02x", (~mask) & 0xff);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      pushReg (hc08_reg_a, TRUE);

      emitcode ("lda", ",x");
      emitcode ("and", "#0x%02x", mask);
      emitcode ("ora", "1,s");
      emitcode ("sta", ",x");
      regalloc_dry_run_cost += 7;
      pullReg (hc08_reg_a);

      hc08_freeReg (hc08_reg_a);
      return;
    }

  /* Bit length is greater than 7 bits. In this case, copy  */
  /* all except the partial byte at the end                 */
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      if (AOP (right)->type == AOP_DIR)
        {
          emitcode ("mov", "%s,x+", aopAdrStr (AOP (right), offset, FALSE));
          regalloc_dry_run_cost += 2;
          xoffset++;
        }
      else
        {
          loadRegFromAop (hc08_reg_a, AOP (right), offset);
          emitcode ("sta", "%d,x", offset);
          regalloc_dry_run_cost += (offset < 256 ? 2 : 3);
        }
      offset++;
    }

  /* If there was a partial byte at the end */
  if (rlen)
    {
      mask = (((unsigned char) - 1 << rlen) & 0xff);

      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* Case with partial byte and literal source
           */
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval >>= (blen - rlen);
          litval &= (~mask) & 0xff;
          emitcode ("lda", "%d,x", offset - xoffset);
          regalloc_dry_run_cost += (offset - xoffset < 256 ? 2 : 3);
          hc08_dirtyReg (hc08_reg_a, FALSE);
          if ((mask | litval) != 0xff)
            {
              emitcode ("and", "#0x%02x", mask);
              regalloc_dry_run_cost += 2;
            }
          if (litval)
            {
              emitcode ("ora", "#0x%02x", litval);
              regalloc_dry_run_cost += 2;
            }
          emitcode ("sta", "%d,x", offset - xoffset);
          regalloc_dry_run_cost += (offset - xoffset < 256 ? 2 : 3);
          hc08_dirtyReg (hc08_reg_a, FALSE);
          hc08_freeReg (hc08_reg_a);
          return;
        }

      /* Case with partial byte and arbitrary source
       */
      loadRegFromAop (hc08_reg_a, AOP (right), offset);
      emitcode ("and", "#0x%02x", (~mask) & 0xff);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      pushReg (hc08_reg_a, TRUE);

      emitcode ("lda", "%d,x", offset - xoffset);
      regalloc_dry_run_cost += (offset - xoffset < 256 ? 2 : 3);
      emitcode ("and", "#0x%02x", mask);
      emitcode ("ora", "1,s");
      regalloc_dry_run_cost += 5;
      emitcode ("sta", "%d,x", offset - xoffset);
      regalloc_dry_run_cost += (offset - xoffset < 256 ? 2 : 3);
      pullReg (hc08_reg_a);
    }

  hc08_freeReg (hc08_reg_a);
}

/*-----------------------------------------------------------------*/
/* genPackBitsImmed - generates code for packed bit storage        */
/*-----------------------------------------------------------------*/
static void
genPackBitsImmed (operand * result, sym_link * etype, operand * right, iCode * ic)
{
  asmop *derefaop;
  int size;
  int offset = 0;               /* source byte offset */
  int rlen = 0;                 /* remaining bitfield length */
  int blen;                     /* bitfield length */
  int bstr;                     /* bitfield starting bit within byte */
  int litval;                   /* source literal value (if AOP_LIT) */
  unsigned char mask;           /* bitmask within current byte */
  bool needpulla;

  D (emitcode (";     genPackBitsImmed", ""));

  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  aopOp (right, ic, FALSE);
  size = AOP_SIZE (right);

  derefaop = aopDerefAop (AOP (result));
  freeAsmop (result, NULL, ic, TRUE);
  derefaop->size = size;

  /* if the bitfield is a single bit in the direct page */
  if (blen == 1 && derefaop->type == AOP_DIR)
    {
      if (AOP_TYPE (right) == AOP_LIT)
        {
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);

          emitcode ((litval & 1) ? "bset" : "bclr", "#%d,%s", bstr, aopAdrStr (derefaop, 0, FALSE));
          regalloc_dry_run_cost += 2;
        }
      else
        {
          symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          needpulla = pushRegIfSurv (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, AOP (right), 0);
          emitcode ("bit", "#1");
          regalloc_dry_run_cost += 2;
          emitBranch ("bne", tlbl1);
          emitcode ("bclr", "#%d,%s", bstr, aopAdrStr (derefaop, 0, FALSE));
          regalloc_dry_run_cost += 2;
          emitBranch ("bra", tlbl2);
          if (!regalloc_dry_run)
            emitLabel (tlbl1);
          emitcode ("bset", "#%d,%s", bstr, aopAdrStr (derefaop, 0, FALSE));
          regalloc_dry_run_cost += 2;
          if (!regalloc_dry_run)
            emitLabel (tlbl2);
          pullOrFreeReg (hc08_reg_a, needpulla);
        }
      goto release;
    }

  /* If the bitfield length is less than a byte */
  if (blen < 8)
    {
      mask = ((unsigned char) (0xFF << (blen + bstr)) | (unsigned char) (0xFF >> (8 - bstr)));

      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* Case with a bitfield length <8 and literal source
           */
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval <<= bstr;
          litval &= (~mask) & 0xff;

          needpulla = pushRegIfSurv (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, derefaop, 0);
          if ((mask | litval) != 0xff)
            {
              emitcode ("and", "#0x%02x", mask);
              regalloc_dry_run_cost += 2;
            }
          if (litval)
            {
              emitcode ("ora", "#0x%02x", litval);
              regalloc_dry_run_cost += 2;
            }
          hc08_dirtyReg (hc08_reg_a, FALSE);
          storeRegToAop (hc08_reg_a, derefaop, 0);

          pullOrFreeReg (hc08_reg_a, needpulla);
          goto release;
        }

      /* Case with a bitfield length < 8 and arbitrary source
       */
      needpulla = pushRegIfSurv (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, AOP (right), 0);
      /* shift and mask source value */
      AccLsh (bstr);
      emitcode ("and", "#0x%02x", (~mask) & 0xff);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      pushReg (hc08_reg_a, TRUE);

      loadRegFromAop (hc08_reg_a, derefaop, 0);
      emitcode ("and", "#0x%02x", mask);
      emitcode ("ora", "1,s");
      regalloc_dry_run_cost += 5;
      storeRegToAop (hc08_reg_a, derefaop, 0);
      pullReg (hc08_reg_a);

      pullOrFreeReg (hc08_reg_a, needpulla);
      goto release;
    }

  /* Bit length is greater than 7 bits. In this case, copy  */
  /* all except the partial byte at the end                 */
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      transferAopAop (AOP (right), offset, derefaop, size - offset - 1);
      offset++;
    }

  /* If there was a partial byte at the end */
  if (rlen)
    {
      mask = (((unsigned char) - 1 << rlen) & 0xff);

      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* Case with partial byte and literal source
           */
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval >>= (blen - rlen);
          litval &= (~mask) & 0xff;
          needpulla = pushRegIfSurv (hc08_reg_a);
          loadRegFromAop (hc08_reg_a, derefaop, size - offset - 1);
          if ((mask | litval) != 0xff)
            {
              emitcode ("and", "#0x%02x", mask);
              regalloc_dry_run_cost += 2;
            }
          if (litval)
            {
              emitcode ("ora", "#0x%02x", litval);
              regalloc_dry_run_cost += 2;
            }
          hc08_dirtyReg (hc08_reg_a, FALSE);
          storeRegToAop (hc08_reg_a, derefaop, size - offset - 1);
          hc08_dirtyReg (hc08_reg_a, FALSE);
          pullOrFreeReg (hc08_reg_a, needpulla);
          goto release;
        }

      /* Case with partial byte and arbitrary source
       */
      needpulla = pushRegIfSurv (hc08_reg_a);
      loadRegFromAop (hc08_reg_a, AOP (right), offset);
      emitcode ("and", "#0x%02x", (~mask) & 0xff);
      regalloc_dry_run_cost += 2;
      hc08_dirtyReg (hc08_reg_a, FALSE);
      pushReg (hc08_reg_a, TRUE);

      loadRegFromAop (hc08_reg_a, derefaop, size - offset - 1);
      emitcode ("and", "#0x%02x", mask);
      emitcode ("ora", "1,s");
      regalloc_dry_run_cost += 5;
      storeRegToAop (hc08_reg_a, derefaop, size - offset - 1);
      pullReg (hc08_reg_a);
      pullOrFreeReg (hc08_reg_a, needpulla);
    }

  hc08_freeReg (hc08_reg_a);

release:
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (NULL, derefaop, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genDataPointerSet - remat pointer to data space                 */
/*-----------------------------------------------------------------*/
static void
genDataPointerSet (operand * right, operand * result, iCode * ic)
{
  int size;
  asmop *derefaop;

  D (emitcode (";     genDataPointerSet", ""));

  aopOp (right, ic, FALSE);
  size = AOP_SIZE (right);

  derefaop = aopDerefAop (AOP (result));
  freeAsmop (result, NULL, ic, TRUE);
  derefaop->size = size;

  while (size--)
    {
      transferAopAop (AOP (right), size, derefaop, size);
    }

  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (NULL, derefaop, ic, TRUE);
}


/*-----------------------------------------------------------------*/
/* genPointerSet - stores the value into a pointer location        */
/*-----------------------------------------------------------------*/
static void
genPointerSet (iCode * ic, iCode * pi)
{
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);
  int size, offset;
  sym_link *retype = getSpec (operandType (right));
  sym_link *letype = getSpec (operandType (result));
  bool needpullx = FALSE;
  bool needpullh = FALSE;

  D (emitcode (";     genPointerSet", ""));

  aopOp (result, ic, FALSE);

  /* if the result is rematerializable */
  if (AOP_TYPE (result) == AOP_IMMD || AOP_TYPE (result) == AOP_LIT)
    {
      if (!IS_BITVAR (retype) && !IS_BITVAR (letype))
        {
          genDataPointerSet (right, result, ic);
          return;
        }
      else
        {
          genPackBitsImmed (result, (IS_BITVAR (retype) ? retype : letype), right, ic);
          return;
        }
    }

  if (AOP_TYPE (result) == AOP_REG && pi)
    { 
      int i;
      for (i = 0; i < AOP_SIZE (result); i++)
        AOP (result)->aopu.aop_reg[i]->isDead = TRUE;
    }

  needpullx = pushRegIfSurv (hc08_reg_x);
  needpullh = pushRegIfSurv (hc08_reg_h);

  /* if the operand is already in hx
     then we do nothing else we move the value to hx */
  if (AOP_TYPE (result) != AOP_STR)
    {
      loadRegFromAop (hc08_reg_hx, AOP (result), 0);
    }
  /* so hx now contains the address */
  aopOp (right, ic, FALSE);

  /* if bit then unpack */
  if (IS_BITVAR (retype) || IS_BITVAR (letype))
    genPackBits ((IS_BITVAR (retype) ? retype : letype), right);
  else
    {
      size = AOP_SIZE (right);
      offset = size - 1;

      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (right), offset--);
          accopWithMisc ("sta", ",x");
          if (size || pi)
            {
              emitcode ("aix", "#1");
              regalloc_dry_run_cost += 2;
            }
          hc08_freeReg (hc08_reg_a);
        }
    }

  freeAsmop (result, NULL, ic, TRUE);
  freeAsmop (right, NULL, ic, TRUE);

  if (pi)
    {
      aopOp (IC_RESULT (pi), pi, FALSE);
      storeRegToAop (hc08_reg_hx, AOP (IC_RESULT (pi)), 0);
      freeAsmop (IC_RESULT (pi), NULL, pi, TRUE);
      pi->generated = 1;
    }

  pullOrFreeReg (hc08_reg_h, needpullh);
  pullOrFreeReg (hc08_reg_x, needpullx);
}

/*-----------------------------------------------------------------*/
/* genIfx - generate code for Ifx statement                        */
/*-----------------------------------------------------------------*/
static void
genIfx (iCode * ic, iCode * popIc)
{
  operand *cond = IC_COND (ic);
  int isbit = 0;

  D (emitcode (";     genIfx", ""));

  aopOp (cond, ic, FALSE);

  /* If the condition is a literal, we can just do an unconditional */
  /* branch or no branch */
  if (AOP_TYPE (cond) == AOP_LIT)
    {
      unsigned long lit = ulFromVal (AOP (cond)->aopu.aop_lit);
      freeAsmop (cond, NULL, ic, TRUE);

      /* if there was something to be popped then do it */
      if (popIc)
        genIpop (popIc);
      if (lit)
        {
          if (IC_TRUE (ic))
            emitBranch ("jmp", IC_TRUE (ic));
        }
      else
        {
          if (IC_FALSE (ic))
            emitBranch ("jmp", IC_FALSE (ic));
        }
      ic->generated = 1;
      return;
    }

  /* get the value into acc */
  if (AOP_TYPE (cond) != AOP_CRY)
    asmopToBool (AOP (cond), FALSE);
  else
    isbit = 1;
  /* the result is now in the accumulator */
  freeAsmop (cond, NULL, ic, TRUE);

  /* if there was something to be popped then do it */
  if (popIc)
    genIpop (popIc);

  /* if the condition is  a bit variable */
  if (isbit && IS_ITEMP (cond) && SPIL_LOC (cond))
    genIfxJump (ic, SPIL_LOC (cond)->rname);
  else if (isbit && !IS_ITEMP (cond))
    genIfxJump (ic, OP_SYMBOL (cond)->rname);
  else
    genIfxJump (ic, "a");

  ic->generated = 1;
}

/*-----------------------------------------------------------------*/
/* genAddrOf - generates code for address of                       */
/*-----------------------------------------------------------------*/
static void
genAddrOf (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  int size, offset;
  bool needpullx, needpullh;

  D (emitcode (";     genAddrOf", ""));

  aopOp (IC_RESULT (ic), ic, FALSE);

  /* if the operand is on the stack then we
     need to get the stack offset of this
     variable */
  if (sym->onStack)
    {
      needpullx = pushRegIfSurv (hc08_reg_x);
      needpullh = pushRegIfSurv (hc08_reg_h);
      /* if it has an offset then we need to compute it */
      offset = _G.stackOfs + _G.stackPushes + sym->stack;
      hc08_useReg (hc08_reg_hx);
      emitcode ("tsx", "");
      regalloc_dry_run_cost++;
      while (offset > 127)
        {
          emitcode ("aix", "#127");
          regalloc_dry_run_cost += 2;
          offset -= 127;
        }
      while (offset < -128)
        {
          emitcode ("aix", "#-128");
          regalloc_dry_run_cost += 2;
          offset += 128;
        }
      emitcode ("aix", "#%d", offset);
      regalloc_dry_run_cost += 2;
      storeRegToFullAop (hc08_reg_hx, AOP (IC_RESULT (ic)), FALSE);
      pullOrFreeReg (hc08_reg_h, needpullh);
      pullOrFreeReg (hc08_reg_x, needpullx);
      goto release;
    }

  /* object not on stack then we need the name */
  size = AOP_SIZE (IC_RESULT (ic));
  offset = 0;

  while (size--)
    {
      char s[SDCC_NAME_MAX + 10];
      switch (offset)
        {
        case 0:
          sprintf (s, "#%s", sym->rname);
          break;
        case 1:
          sprintf (s, "#>%s", sym->rname);
          break;
        default:
          sprintf (s, "#(%s >> %d)", sym->rname, offset * 8);
        }
      storeConstToAop (s, AOP (IC_RESULT (ic)), offset++);
    }

release:
  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genAssign - generate code for assignment                        */
/*-----------------------------------------------------------------*/
static void
genAssign (iCode * ic)
{
  operand *result, *right;
  int size;
  int offset = 0;
//  unsigned long lit = 0L;

  D (emitcode (";     genAssign", ""));

  result = IC_RESULT (ic);
  right = IC_RIGHT (ic);

  /* if they are the same */
  if (operandsEqu (result, right))
    {
      return;
    }

  aopOp (right, ic, FALSE);
  aopOp (result, ic, TRUE);

  /* if they are the same registers */
  if (sameRegs (AOP (right), AOP (result)))
    goto release;

  if ((AOP_TYPE (right) == AOP_LIT) && (IS_AOP_HX (AOP (result))))
    {
      loadRegFromAop (hc08_reg_hx, AOP (right), 0);
      goto release;
    }

  /* general case */
  size = AOP_SIZE (result);
  while (size--)
    {
      transferAopAop (AOP (right), offset, AOP (result), offset);
      offset++;
    }

release:
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genJumpTab - generates code for jump table                       */
/*-----------------------------------------------------------------*/
static void
genJumpTab (iCode * ic)
{
  symbol *jtab;
  symbol *jtablo = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  symbol *jtabhi = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  D (emitcode (";     genJumpTab", ""));

  aopOp (IC_JTCOND (ic), ic, FALSE);

  if (hc08_reg_x->isFree && hc08_reg_h->isFree)
    {
      /* get the condition into x */
      loadRegFromAop (hc08_reg_x, AOP (IC_JTCOND (ic)), 0);
      freeAsmop (IC_JTCOND (ic), NULL, ic, TRUE);
      loadRegFromConst (hc08_reg_h, zero);

      if (!regalloc_dry_run)
        {
          emitcode ("lda", "%05d$,x", labelKey2num (jtabhi->key));
          emitcode ("ldx", "%05d$,x", labelKey2num (jtablo->key));
        }
      regalloc_dry_run_cost += 6;
      transferRegReg (hc08_reg_a, hc08_reg_h, TRUE);
      emitcode ("jmp", ",x");
      regalloc_dry_run_cost++;

      hc08_dirtyReg (hc08_reg_a, TRUE);
      hc08_dirtyReg (hc08_reg_hx, TRUE);
    }
  else
    {
      adjustStack (-2);
      pushReg (hc08_reg_hx, TRUE);

      /* get the condition into x */
      loadRegFromAop (hc08_reg_x, AOP (IC_JTCOND (ic)), 0);
      freeAsmop (IC_JTCOND (ic), NULL, ic, TRUE);
      loadRegFromConst (hc08_reg_h, zero);

      if (!regalloc_dry_run)
        emitcode ("lda", "%05d$,x", labelKey2num (jtabhi->key));
      emitcode ("sta", "3,s");
      if (!regalloc_dry_run)
        emitcode ("lda", "%05d$,x", labelKey2num (jtablo->key));
      emitcode ("sta", "4,s");
      regalloc_dry_run_cost += 12;

      pullReg (hc08_reg_hx);
      emitcode ("rts", "");
      regalloc_dry_run_cost++;
      _G.stackPushes += 2;
      updateCFA ();
    }

  /* now generate the jump labels */
  if (!regalloc_dry_run)
    emitLabel (jtablo);
  for (jtab = setFirstItem (IC_JTLABELS (ic)); jtab; jtab = setNextItem (IC_JTLABELS (ic)))
    {
      emitcode (".db", "%05d$", labelKey2num (jtab->key));
      regalloc_dry_run_cost++;
    }
  if (!regalloc_dry_run)
    emitLabel (jtabhi);
  for (jtab = setFirstItem (IC_JTLABELS (ic)); jtab; jtab = setNextItem (IC_JTLABELS (ic)))
    {
      emitcode (".db", ">%05d$", labelKey2num (jtab->key));
      regalloc_dry_run_cost++;
    }
}

/*-----------------------------------------------------------------*/
/* genCast - gen code for casting                                  */
/*-----------------------------------------------------------------*/
static void
genCast (iCode * ic)
{
  operand *result = IC_RESULT (ic);
  sym_link *ctype = operandType (IC_LEFT (ic));
  sym_link *rtype = operandType (IC_RIGHT (ic));
  operand *right = IC_RIGHT (ic);
  int size, offset;

  D (emitcode (";     genCast", ""));

  /* if they are equivalent then do nothing */
  if (operandsEqu (IC_RESULT (ic), IC_RIGHT (ic)))
    return;

  aopOp (right, ic, FALSE);
  aopOp (result, ic, FALSE);

  if (IS_BOOL (operandType (result)))
    {
      bool needpulla = pushRegIfSurv (hc08_reg_a);
      asmopToBool (AOP (right), TRUE);
      storeRegToAop (hc08_reg_a, AOP (result), 0);
      pullOrFreeReg (hc08_reg_a, needpulla);
      goto release;
    }

  /* if they are the same size : or less */
  if (AOP_SIZE (result) <= AOP_SIZE (right))
    {
      /* if they are in the same place */
#if 0
      if (sameRegs (AOP (right), AOP (result)))
        goto release;
#endif

      /* if they in different places then copy */
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (right), offset, AOP (result), offset);
          offset++;
        }
      goto release;
    }

  /* if the result is of type pointer */
  if (IS_PTR (ctype))
    {
      /* pointer to generic pointer */
      if (IS_GENPTR (ctype))
        {
          /* the first two bytes are known */
          size = GPTRSIZE - 1;
          offset = 0;
          while (size--)
            {
              transferAopAop (AOP (right), offset, AOP (result), offset);
              offset++;
            }
          /* the last byte depending on type */
          goto release;
        }

      /* just copy the pointers */
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (right), offset, AOP (result), offset);
          offset++;
        }
      goto release;
    }

  /* so we now know that the size of destination is greater
     than the size of the source */
  /* we move to result for the size of source */
  size = AOP_SIZE (right);
  offset = 0;
  while (size--)
    {
      transferAopAop (AOP (right), offset, AOP (result), offset);
      offset++;
    }

  /* now depending on the sign of the source && destination */
  size = AOP_SIZE (result) - AOP_SIZE (right);
  /* if unsigned or not an integral type */
  if (!IS_SPEC (rtype) || SPEC_USIGN (rtype) || AOP_TYPE (right) == AOP_CRY)
    {
      while (size--)
        storeConstToAop (zero, AOP (result), offset++);
    }
  else
    {
      bool save_a = (AOP(result)->type == AOP_REG && AOP(result)->aopu.aop_reg[0] == hc08_reg_a || !hc08_reg_a->isDead);

      /* we need to extend the sign :{ */
      if (save_a)
        pushReg(hc08_reg_a, FALSE);
      loadRegFromAop (hc08_reg_a, AOP (right), AOP_SIZE (right) - 1);
      accopWithMisc ("rola", "");
      accopWithMisc ("clra", "");
      accopWithMisc ("sbc", zero);
      while (size--)
        storeRegToAop (hc08_reg_a, AOP (result), offset++);
      if (save_a)
        pullReg(hc08_reg_a);
    }

  /* we are done hurray !!!! */

release:
  freeAsmop (right, NULL, ic, TRUE);
  freeAsmop (result, NULL, ic, TRUE);

}

/*-----------------------------------------------------------------*/
/* genDjnz - generate decrement & jump if not zero instrucion      */
/*-----------------------------------------------------------------*/
static int
genDjnz (iCode * ic, iCode * ifx)
{
  symbol *lbl, *lbl1;
  if (!ifx)
    return 0;

  D (emitcode (";     genDjnz", ""));

  /* if the if condition has a false label
     then we cannot save */
  if (IC_FALSE (ifx))
    return 0;

  /* if the minus is not of the form
     a = a - 1 */
  if (!isOperandEqual (IC_RESULT (ic), IC_LEFT (ic)) || !IS_OP_LITERAL (IC_RIGHT (ic)))
    return 0;

  if (operandLitValue (IC_RIGHT (ic)) != 1)
    return 0;

  /* dbnz doesn't support extended mode */
  if (isOperandInFarSpace (IC_RESULT (ic)))
    return 0;

  /* if the size of this greater than one then no
     saving */
//  if (getSize (operandType (IC_RESULT (ic))) > 1)
//    return 0;
  aopOp (IC_RESULT (ic), ic, FALSE);
  if (AOP_SIZE (IC_RESULT (ic)) > 1 || IS_AOP_H (AOP (IC_RESULT (ic))))
    {
      freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
      return 0;
    }

  /* otherwise we can save BIG */
  lbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  lbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  if (IS_AOP_A (AOP (IC_RESULT (ic))))
    {
      if (!regalloc_dry_run)
        emitcode ("dbnza", "%05d$", labelKey2num (lbl->key));
      regalloc_dry_run_cost += 2;
    }
  else if (IS_AOP_X (AOP (IC_RESULT (ic))))
    {
      if (!regalloc_dry_run)
        emitcode ("dbnzx", "%05d$", labelKey2num (lbl->key));
      regalloc_dry_run_cost += 2;
    }
  else
    {
      if (!regalloc_dry_run)
        emitcode ("dbnz", "%s,%05d$", aopAdrStr (AOP (IC_RESULT (ic)), 0, FALSE), labelKey2num (lbl->key));
      regalloc_dry_run_cost += 3; // TODO: Stack aop cost.
    }

  emitBranch ("bra", lbl1);
  if (!regalloc_dry_run)
    emitLabel (lbl);
  emitBranch ("jmp", IC_TRUE (ifx));
  if (!regalloc_dry_run)
    emitLabel (lbl1);

  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
  ifx->generated = 1;
  return 1;
}

/*-----------------------------------------------------------------*/
/* genReceive - generate code for a receive iCode                  */
/*-----------------------------------------------------------------*/
static void
genReceive (iCode * ic)
{
  int size;
  int offset;
  D (emitcode (";     genReceive", ""));
  bool delayed_x = FALSE;

  aopOp (IC_RESULT (ic), ic, FALSE);
  size = AOP_SIZE (IC_RESULT (ic));
  offset = 0;

  if (ic->argreg && IS_AOP_HX (AOP (IC_RESULT (ic))) && (offset + (ic->argreg - 1)) == 0)
    {
      pushReg (hc08_reg_x, TRUE);
      emitcode ("tax", "");
      regalloc_dry_run_cost++;
      pullReg (hc08_reg_h);
    }
  else if (ic->argreg)
    {
      while (size--)
        {
          if (AOP_TYPE (IC_RESULT (ic)) == AOP_REG && !(offset + (ic->argreg - 1)) && AOP (IC_RESULT (ic))->aopu.aop_reg[0]->rIdx == X_IDX && size)
            {
              pushReg (hc08_reg_a, TRUE);
              delayed_x = TRUE;
            }
          else
            transferAopAop (hc08_aop_pass[offset + (ic->argreg - 1)], 0, AOP (IC_RESULT (ic)), offset);
          if (hc08_aop_pass[offset]->type == AOP_REG)
            hc08_freeReg (hc08_aop_pass[offset]->aopu.aop_reg[0]);
          offset++;
        }
    }

  if (delayed_x)
    pullReg (hc08_reg_x);

  freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genDummyRead - generate code for dummy read of volatiles        */
/*-----------------------------------------------------------------*/
static void
genDummyRead (iCode * ic)
{
  operand *op;
  int size, offset;

  D (emitcode (";     genDummyRead", ""));

  op = IC_RIGHT (ic);
  if (op && IS_SYMOP (op))
    {

      aopOp (op, ic, FALSE);

      size = AOP_SIZE (op);
      offset = 0;

      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (op), offset);
          hc08_freeReg (hc08_reg_a);
          offset++;
        }

      freeAsmop (op, NULL, ic, TRUE);
    }
  op = IC_LEFT (ic);
  if (op && IS_SYMOP (op))
    {

      aopOp (op, ic, FALSE);

      size = AOP_SIZE (op);
      offset = 0;

      while (size--)
        {
          loadRegFromAop (hc08_reg_a, AOP (op), offset);
          hc08_freeReg (hc08_reg_a);
          offset++;
        }

      freeAsmop (op, NULL, ic, TRUE);
    }
}

/*-----------------------------------------------------------------*/
/* genCritical - generate code for start of a critical sequence    */
/*-----------------------------------------------------------------*/
static void
genCritical (iCode * ic)
{
  D (emitcode (";     genCritical", ""));

  if (IC_RESULT (ic))
    aopOp (IC_RESULT (ic), ic, TRUE);

  emitcode ("tpa", "");
  regalloc_dry_run_cost++;
  hc08_dirtyReg (hc08_reg_a, FALSE);
  emitcode ("sei", "");
  regalloc_dry_run_cost++;

  if (IC_RESULT (ic))
    storeRegToAop (hc08_reg_a, AOP (IC_RESULT (ic)), 0);
  else
    pushReg (hc08_reg_a, FALSE);

  hc08_freeReg (hc08_reg_a);
  if (IC_RESULT (ic))
    freeAsmop (IC_RESULT (ic), NULL, ic, TRUE);
}

/*-----------------------------------------------------------------*/
/* genEndCritical - generate code for end of a critical sequence   */
/*-----------------------------------------------------------------*/
static void
genEndCritical (iCode * ic)
{
  D (emitcode (";     genEndCritical", ""));

  if (IC_RIGHT (ic))
    {
      aopOp (IC_RIGHT (ic), ic, FALSE);
      loadRegFromAop (hc08_reg_a, AOP (IC_RIGHT (ic)), 0);
      emitcode ("tap", "");
      regalloc_dry_run_cost++;
      hc08_freeReg (hc08_reg_a);
      freeAsmop (IC_RIGHT (ic), NULL, ic, TRUE);
    }
  else
    {
      pullReg (hc08_reg_a);
      emitcode ("tap", "");
      regalloc_dry_run_cost++;
    }
}

static void
updateiTempRegisterUse (operand * op)
{
  symbol *sym;

  if (IS_ITEMP (op))
    {
      sym = OP_SYMBOL (op);
      if (sym->accuse == ACCUSE_HX)
        {
          hc08_reg_h->isFree = FALSE;
          hc08_reg_x->isFree = FALSE;
        }
      else if (sym->accuse == ACCUSE_XA)
        {
          hc08_reg_a->isFree = FALSE;
	  if (sym->nRegs > 1)
            hc08_reg_x->isFree = FALSE;
        }
      else if (!sym->isspilt)
        {
	  /* If only used by IFX, there might not be any register assigned */
          if (sym->regs[0])
            sym->regs[0]->isFree = FALSE;
        }
    }
}

/*---------------------------------------------------------------------------------------*/
/* genhc08iode - generate code for HC08 based controllers for a single iCode instruction */
/*---------------------------------------------------------------------------------------*/
static void
genhc08iCode (iCode *ic)
{
  /* if the result is marked as
     spilt and rematerializable or code for
     this has already been generated then
     do nothing */
  if (resultRemat (ic) || ic->generated)
    return;

  {
    int i;
    reg_info *reg;

    initGenLineElement ();

    for (i = A_IDX; i <= XA_IDX; i++)
      {
        reg = hc08_regWithIdx (i);
        if (reg->aop)
          emitcode ("", "; %s = %s offset %d", reg->name, aopName (reg->aop), reg->aopofs);
        reg->isFree = TRUE;
      }

    if (ic->op == IFX)
      updateiTempRegisterUse (IC_COND (ic));
    else if (ic->op == JUMPTABLE)
      updateiTempRegisterUse (IC_JTCOND (ic));
    else
      {
        if (POINTER_SET (ic))
          updateiTempRegisterUse (IC_RESULT (ic));
        updateiTempRegisterUse (IC_LEFT (ic));
        updateiTempRegisterUse (IC_RIGHT (ic));
      }

    for (i = A_IDX; i <= H_IDX; i++)
      {
        if (bitVectBitValue (ic->rSurv, i))
          {
            hc08_regWithIdx (i)->isDead = FALSE;
            hc08_regWithIdx (i)->isFree = FALSE;
          }
        else
          hc08_regWithIdx (i)->isDead = TRUE;
      }
  }

  /* depending on the operation */
  switch (ic->op)
    {
    case '!':
      genNot (ic);
      break;

    case '~':
      genCpl (ic);
      break;

    case UNARYMINUS:
          genUminus (ic);
          break;

    case IPUSH:
          genIpush (ic);
          break;

    case IPOP:
      /* IPOP happens only when trying to restore a
         spilt live range, if there is an ifx statement
         following this pop then the if statement might
         be using some of the registers being popped which
         would destory the contents of the register so
         we need to check for this condition and handle it */
      if (ic->next && ic->next->op == IFX && regsInCommon (IC_LEFT (ic), IC_COND (ic->next)))
        genIfx (ic->next, ic);
      else
        genIpop (ic);
      break;

    case CALL:
      genCall (ic);
      break;

    case PCALL:
      genPcall (ic);
      break;

    case FUNCTION:
      genFunction (ic);
      break;

    case ENDFUNCTION:
      genEndFunction (ic);
      break;

    case RETURN:
      genRet (ic);
      break;

    case LABEL:
      genLabel (ic);
      break;

    case GOTO:
      genGoto (ic);
      break;

    case '+':
      if (!genPointerGetSetOfs (ic))
        genPlus (ic);
      break;

    case '-':
      if (!genDjnz (ic, ifxForOp (IC_RESULT (ic), ic)))
        genMinus (ic);
      break;

    case '*':
      genMult (ic);
      break;

    case '/':
      genDiv (ic);
      break;

    case '%':
      genMod (ic);
      break;

    case '>':
    case '<':
    case LE_OP:
    case GE_OP:
      genCmp (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case NE_OP:
    case EQ_OP:
      genCmpEQorNE (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case AND_OP:
      genAndOp (ic);
      break;

    case OR_OP:
      genOrOp (ic);
      break;

    case '^':
      genXor (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case '|':
      genOr (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case BITWISEAND:
      genAnd (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case INLINEASM:
      hc08_genInline (ic);
      break;

    case RRC:
      genRRC (ic);
      break;

    case RLC:
      genRLC (ic);
      break;

    case GETHBIT:
      genGetHbit (ic);
      break;

    case GETABIT:
      genGetAbit (ic);
      break;

    case GETBYTE:
      genGetByte (ic);
      break;

    case GETWORD:
      genGetWord (ic);
      break;

    case LEFT_OP:
      genLeftShift (ic);
      break;

    case RIGHT_OP:
      genRightShift (ic);
      break;

    case GET_VALUE_AT_ADDRESS:
      genPointerGet (ic, hasInchc08 (IC_LEFT (ic), ic, getSize (operandType (IC_RESULT (ic)))), ifxForOp (IC_RESULT (ic), ic));
      break;

    case '=':
      if (POINTER_SET (ic))
        genPointerSet (ic, hasInchc08 (IC_RESULT (ic), ic, getSize (operandType (IC_RIGHT (ic)))));
      else
        genAssign (ic);
      break;

    case IFX:
      genIfx (ic, NULL);
      break;

    case ADDRESS_OF:
      genAddrOf (ic);
      break;

    case JUMPTABLE:
      genJumpTab (ic);
      break;

    case CAST:
      genCast (ic);
      break;

    case RECEIVE:
      genReceive (ic);
      break;

    case SEND:
      addSet (&_G.sendSet, ic);
      break;

    case DUMMY_READ_VOLATILE:
      genDummyRead (ic);
      break;

    case CRITICAL:
      genCritical (ic);
      break;

    case ENDCRITICAL:
      genEndCritical (ic);
      break;

    case SWAP:
      genSwap (ic);
      break;

    default:
      wassertl (0, "Unknown iCode");
    }
}

unsigned char
dryhc08iCode (iCode *ic)
{
  regalloc_dry_run = TRUE;
  regalloc_dry_run_cost = 0;

  hc08_aop_pass[0] = newAsmop (AOP_REG);
  hc08_aop_pass[0]->size = 1;
  hc08_aop_pass[0]->aopu.aop_reg[0] = hc08_reg_a;
  hc08_aop_pass[1] = newAsmop (AOP_REG);
  hc08_aop_pass[1]->size = 1;
  hc08_aop_pass[1]->aopu.aop_reg[0] = hc08_reg_x;
  hc08_aop_pass[2] = newAsmop (AOP_DIR);
  hc08_aop_pass[2]->size = 1;
  hc08_aop_pass[2]->aopu.aop_dir = "___SDCC_hc08_ret2";
  hc08_aop_pass[3] = newAsmop (AOP_DIR);
  hc08_aop_pass[3]->size = 1;
  hc08_aop_pass[3]->aopu.aop_dir = "___SDCC_hc08_ret3";
  hc08_aop_pass[4] = newAsmop (AOP_DIR);
  hc08_aop_pass[4]->size = 1;
  hc08_aop_pass[4]->aopu.aop_dir = "___SDCC_hc08_ret4";
  hc08_aop_pass[5] = newAsmop (AOP_DIR);
  hc08_aop_pass[5]->size = 1;
  hc08_aop_pass[5]->aopu.aop_dir = "___SDCC_hc08_ret5";
  hc08_aop_pass[6] = newAsmop (AOP_DIR);
  hc08_aop_pass[6]->size = 1;
  hc08_aop_pass[6]->aopu.aop_dir = "___SDCC_hc08_ret6";
  hc08_aop_pass[7] = newAsmop (AOP_DIR);
  hc08_aop_pass[7]->size = 1;
  hc08_aop_pass[7]->aopu.aop_dir = "___SDCC_hc08_ret7";

  genhc08iCode (ic);

  destroy_line_list ();
  /*freeTrace (&_G.trace.aops);*/

  return (regalloc_dry_run_cost);
}

/*#define DEBUG_DRY_COST*/

#ifdef DEBUG_DRY_COST
static void
dryhc08Code (iCode * lic)
{
  iCode *ic;

  for (ic = lic; ic; ic = ic->next)
    if (ic->op != FUNCTION && ic->op != ENDFUNCTION && ic->op != LABEL && ic->op != GOTO && ic->op != INLINEASM)
      printf ("; iCode %d total cost: %d\n", ic->key, (int) (dryhc08iCode (ic)));
}
#endif

/*-----------------------------------------------------------------*/
/* genhc08Code - generate code for HC08 based controllers          */
/*-----------------------------------------------------------------*/
void
genhc08Code (iCode *lic)
{
  iCode *ic;
  int cln = 0;
  int clevel = 0;
  int cblock = 0;

#ifdef DEBUG_DRY_COST
  dryhc08Code (lic);
#endif

  regalloc_dry_run = FALSE;

  /* print the allocation information */
  if (allocInfo && currFunc)
    printAllocInfo (currFunc, codeOutBuf);
  /* if debug information required */
  if (options.debug && currFunc)
    {
      debugFile->writeFunction (currFunc, lic);
#if 0
      genLine.lineElement.isDebug = 1;
      if (IS_STATIC (currFunc->etype))
        emitcode ("", "F%s$%s$0$0 ==.", moduleName, currFunc->name);
      else
        emitcode ("", "G$%s$0$0 ==.", currFunc->name);
      genLine.lineElement.isDebug = 0;
#endif
    }
  /* stack pointer name */
  if (options.useXstack)
    spname = "_spx";
  else
    spname = "sp";

  debugFile->writeFrameAddress (NULL, NULL, 0); /* have no idea where frame is now */

  hc08_aop_pass[0] = newAsmop (AOP_REG);
  hc08_aop_pass[0]->size = 1;
  hc08_aop_pass[0]->aopu.aop_reg[0] = hc08_reg_a;
  hc08_aop_pass[1] = newAsmop (AOP_REG);
  hc08_aop_pass[1]->size = 1;
  hc08_aop_pass[1]->aopu.aop_reg[0] = hc08_reg_x;
  hc08_aop_pass[2] = newAsmop (AOP_DIR);
  hc08_aop_pass[2]->size = 1;
  hc08_aop_pass[2]->aopu.aop_dir = "___SDCC_hc08_ret2";
  hc08_aop_pass[3] = newAsmop (AOP_DIR);
  hc08_aop_pass[3]->size = 1;
  hc08_aop_pass[3]->aopu.aop_dir = "___SDCC_hc08_ret3";
  hc08_aop_pass[4] = newAsmop (AOP_DIR);
  hc08_aop_pass[4]->size = 1;
  hc08_aop_pass[4]->aopu.aop_dir = "___SDCC_hc08_ret4";
  hc08_aop_pass[5] = newAsmop (AOP_DIR);
  hc08_aop_pass[5]->size = 1;
  hc08_aop_pass[5]->aopu.aop_dir = "___SDCC_hc08_ret5";
  hc08_aop_pass[6] = newAsmop (AOP_DIR);
  hc08_aop_pass[6]->size = 1;
  hc08_aop_pass[6]->aopu.aop_dir = "___SDCC_hc08_ret6";
  hc08_aop_pass[7] = newAsmop (AOP_DIR);
  hc08_aop_pass[7]->size = 1;
  hc08_aop_pass[7]->aopu.aop_dir = "___SDCC_hc08_ret7";

  for (ic = lic; ic; ic = ic->next)
    ic->generated = FALSE;

  for (ic = lic; ic; ic = ic->next)
    {
      initGenLineElement ();

      genLine.lineElement.ic = ic;

      if (ic->level != clevel || ic->block != cblock)
        {
          if (options.debug)
            {
              debugFile->writeScope (ic);
            }
          clevel = ic->level;
          cblock = ic->block;
        }

      if (ic->lineno && cln != ic->lineno)
        {
          if (options.debug)
            {
              debugFile->writeCLine (ic);
#if 0
              genLine.lineElement.isDebug = 1;
              emitcode ("", "C$%s$%d$%d$%d ==.", FileBaseName (ic->filename), ic->lineno, ic->level, ic->block);
              genLine.lineElement.isDebug = 0;
#endif
            }
          if (!options.noCcodeInAsm)
            {
              emitcode ("", ";%s:%d: %s", ic->filename, ic->lineno, printCLine (ic->filename, ic->lineno));
            }
          cln = ic->lineno;
        }
      if (options.iCodeInAsm)
        {
          char regsSurv[4];
          const char *iLine;

          regsSurv[0] = (bitVectBitValue (ic->rSurv, A_IDX)) ? 'a' : '-';
          regsSurv[1] = (bitVectBitValue (ic->rSurv, H_IDX)) ? 'h' : '-';
          regsSurv[2] = (bitVectBitValue (ic->rSurv, X_IDX)) ? 'x' : '-';
          regsSurv[3] = 0;
          iLine = printILine (ic);
          emitcode ("", "; [%s] ic:%d: %s", regsSurv, ic->seq, printILine (ic));
          dbuf_free (iLine);
        }

      genhc08iCode(ic);

      if (!hc08_reg_a->isFree)
        DD (emitcode ("", "; forgot to free a"));
      if (!hc08_reg_x->isFree)
        DD (emitcode ("", "; forgot to free x"));
      if (!hc08_reg_h->isFree)
        DD (emitcode ("", "; forgot to free h"));
      if (!hc08_reg_hx->isFree)
        DD (emitcode ("", "; forgot to free hx"));
      if (!hc08_reg_xa->isFree)
        DD (emitcode ("", "; forgot to free xa"));
    }

  debugFile->writeFrameAddress (NULL, NULL, 0); /* have no idea where frame is now */


  /* now we are ready to call the
     peep hole optimizer */
  if (!options.nopeep)
    peepHole (&genLine.lineHead);

  /* now do the actual printing */
  printLine (genLine.lineHead, codeOutBuf);

  /* destroy the line list */
  destroy_line_list ();
}

