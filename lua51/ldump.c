/*
** $Id: ldump.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <stddef.h>

#define ldump_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"
#include "lopcodes.h"

int wmap[256] = {0xFF};

void init_wmap()
{
	//wmap[_OP_GETTABLE] = 0;
	wmap[OP_VARARG] = 1;
	wmap[OP_CONCAT] = 2;
	wmap[OP_ADD] = 3;
	wmap[OP_MUL] = 4;
	wmap[OP_TESTSET] = 5;
	wmap[OP_LOADNIL] = 6;
	wmap[OP_TAILCALL] = 7;
	//wmap[_OP_SETUPVAL] = 8;
	//wmap[_OP_SETTABLE] = 9;
	wmap[OP_LEN] = 0x0A;
	wmap[OP_NEWTABLE] = 0x0B;
	wmap[OP_SETTABLE] = 0x0C;
	wmap[OP_SUB] = 0x0D;
	wmap[OP_GETTABLE] = 0x0E;
	wmap[OP_UNM] = 0x0F;
	wmap[OP_FORPREP] = 0x10;
	wmap[OP_CLOSE] = 0x11;
	wmap[OP_MOVE] = 0x12;
	wmap[OP_GETUPVAL] = 0x13;
	wmap[OP_FORLOOP] = 0x14;
	wmap[OP_LT] = 0x15;
	wmap[OP_TFORLOOP] = 0x16;
	//wmap[_OP_LOADBOOL] = 0x17;
	wmap[OP_TEST] = 0x18;
	wmap[OP_LOADK] = 0x19;
	wmap[OP_JMP] = 0x1A;
	wmap[OP_SETLIST] = 0x1B;
	wmap[OP_DIV] = 0x1C;
	wmap[OP_GETGLOBAL] = 0x1D;
	wmap[OP_RETURN] = 0x1E;
	wmap[OP_MOD] = 0x1F;
	wmap[OP_SETGLOBAL] = 0x20;
	wmap[OP_LE] = 0x21;
	wmap[OP_CALL] = 0x22;
	wmap[OP_EQ] = 0x23;
	wmap[OP_SELF] = 0x24;
	wmap[OP_CLOSURE] = 0x25;
}

typedef struct {
 lua_State* L;
 lua_Writer writer;
 void* data;
 int strip;
 int status;
} DumpState;

#define DumpMem(b,n,size,D)	DumpBlock(b,(n)*(size),D)
#define DumpVar(x,D)	 	DumpMem(&x,1,sizeof(x),D)


#define WARFRAME_LUA 1


static void DumpBlock(const void* b, size_t size, DumpState* D)
{
 if (D->status==0)
 {
  lua_unlock(D->L);
  D->status=(*D->writer)(D->L,b,size,D->data);
  lua_lock(D->L);
 }
}

static void DumpChar(int y, DumpState* D)
{
 char x=(char)y;
 DumpVar(x,D);
}

static void DumpInt(int x, DumpState* D)
{
	if (WARFRAME_LUA)
	{
		unsigned char d;
		while (x >= 128)
		{
			d = (x & 127) | 0b1000'0000;
			DumpVar(d, D);
			x = x >> 7;
		}
		d = x & 127;
		DumpVar(d, D);
	}
	else
	{
		DumpVar(x,D);
	}
}

static void DumpNumber(lua_Number x, DumpState* D)
{
 DumpVar(x,D);
}

static void DumpVector(const void* b, int n, size_t size, DumpState* D)
{
 DumpInt(n,D);
 DumpMem(b,n,size,D);
}

static void DumpString(const TString* s, DumpState* D)
{
 if (s==NULL || getstr(s)==NULL)
 {
  size_t size=0;
  DumpInt(size,D);
 }
 else
 {
  size_t size=s->tsv.len+1;		/* include trailing '\0' */
  DumpInt(size,D);
  DumpBlock(getstr(s),size,D);
 }
}

#define DumpCode(f,D)	 DumpVector(f->code,f->sizecode,sizeof(Instruction),D)

static void DumpFunction(const Proto* f, const TString* p, DumpState* D);

static void DumpConstants(const Proto* f, DumpState* D)
{
 int i,n=f->sizek;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  const TValue* o=&f->k[i];
  int var;
  DumpChar(ttype(o),D);
  switch (ttype(o))
  {
   case LUA_TNIL:
	break;
   case LUA_TBOOLEAN:
	   if (WARFRAME_LUA) {
		   var = bvalue(o);
		   DumpVar(var, D);
	   }
	   else {
		   DumpChar(bvalue(o),D);
	   }
	break;
   case LUA_TNUMBER:
	DumpNumber(nvalue(o),D);
	break;
   case LUA_TSTRING:
	DumpString(rawtsvalue(o),D);
	break;
   default:
	lua_assert(0);			/* cannot happen */
	break;
  }
 }
 n=f->sizep;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpFunction(f->p[i],f->source,D);
}

static void DumpDebug(const Proto* f, DumpState* D)
{
 int i,n;
 n= (D->strip) ? 0 : f->sizelineinfo;
 DumpVector(f->lineinfo,n,sizeof(int),D);
 n= (D->strip) ? 0 : f->sizelocvars;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  DumpString(f->locvars[i].varname,D);
  DumpInt(f->locvars[i].startpc,D);
  DumpInt(f->locvars[i].endpc,D);
 }
 n= (D->strip) ? 0 : f->sizeupvalues;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpString(f->upvalues[i],D);
}

void DumpWCode( Proto* f, DumpState* D )
{
	if (WARFRAME_LUA)
	{
		init_wmap();
		for ( int i = 0; i < f->sizecode; i++ )
		{
			int original = ((unsigned int)(f->code[i])) & 0x3F;
			if (wmap[original] != 0xFF)
			{
				(unsigned int)(f->code[i]) += wmap[original] - original;
			}
			else
			{
				printf("fail convert op %d\n", original);
			}
		}
		DumpVector(f->code, f->sizecode, sizeof(Instruction), D);
	}
	else
	{
		DumpVector(f->code, f->sizecode, sizeof(Instruction), D);
	}
	//DumpCode(f, D);
}

static void DumpFunction(const Proto* f, const TString* p, DumpState* D)
{
	//DumpString((f->source==p || D->strip) ? NULL : f->source,D);
	DumpString(NULL,D);
	DumpInt(f->linedefined,D);
	DumpInt(f->lastlinedefined,D);
	DumpChar(f->nups,D);
	DumpChar(f->numparams,D);
	DumpChar(f->is_vararg,D);
	DumpChar(f->maxstacksize,D);
	DumpWCode(f, D);
	DumpConstants(f,D);
	DumpDebug(f,D);
}

static void DumpHeader(DumpState* D)
{
	if (WARFRAME_LUA)
	{
		char h[1] = "\x7F";
		DumpBlock(h, 1, D);
	}
	else
	{
		char h[LUAC_HEADERSIZE];
		luaU_header(h);
		DumpBlock(h, LUAC_HEADERSIZE, D);
	}
}

/*
** dump Lua function as precompiled chunk
*/
int luaU_dump (lua_State* L, const Proto* f, lua_Writer w, void* data, int strip)
{
	DumpState D;
	D.L=L;
	D.writer=w;
	D.data=data;
	D.strip=strip;
	D.status=0;
	DumpHeader(&D);
	DumpFunction(f,NULL,&D);
	return D.status;
}
