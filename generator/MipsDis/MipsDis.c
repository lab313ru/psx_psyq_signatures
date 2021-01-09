// Mipsdis - Disassembler for .obj files.
// V1.0		Original version by SurfSmurf.
// V1.1		Bugfix: sra by SurfSmurf.
// V1.2		Addition: mfc0, mtc0, mtc2, mfc2, cfc2, ctc2, lwc2, swc2, cop2, rfe
//			and GTE, cop0 register names by Doomed.
// V1.3		Adds GTE command decoding
// V1.4		Couple of patches and operators implemented.
//			Outputs several symbols per address if they exist.
//			Improved NOP detection
//			Alternative GTE decoding added for CW output

#include	<stdio.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<string.h>
#include <ctype.h>

#include	"types.h"

int	OPT_ALTGTE = 0;
int	iSymbolNumber = 1000000;

FILE* dest = NULL;
char ttmp[8192];

void	Error(const char* s, ...)
{
	char	temp[256];
	va_list	list;

	va_start(list, s);
	vsprintf(temp, s, list);
	fprintf(stderr, "*ERROR* : %s\n", temp);
	va_end(list);
	//getchar();
	exit(EXIT_FAILURE);
}

ULONG	fgetll(FILE* f)
{
	ULONG	r;

	r = fgetc(f);
	r |= fgetc(f) << 8;
	r |= fgetc(f) << 16;
	r |= fgetc(f) << 24;

	return(r);
}

UWORD	fgetlw(FILE* f)
{
	UWORD	r;

	r = fgetc(f);
	r |= fgetc(f) << 8;

	return(r);
}

typedef	enum
{
	OP_CONSTANT,
	OP_SECTBASE,
	OP_ADDROFSYMBOL,
	OP_ADD,
	OP_SECTEND,
	OP_SECTSIZE,
	OP_SUB,
	OP_MUL,
	OP_DIV,
}	EOperator;

typedef	enum
{
	PATCH_WORD,
	PATCH_LONG,
	PATCH_MIPSLO,
	PATCH_MIPSHI,
	PATCH_MIPSGP,
	PATCH_MIPSFP,
}	EPatchType;

typedef	struct	_SExpression
{
	EOperator	Operator;

	SLONG	iValue;
	struct	_SExpression* pLeft;
	struct	_SExpression* pRight;
}	SExpression;

typedef	struct	_SPatch
{
	EPatchType	Type;
	SExpression* pExpr;
	ULONG		iOffset;

	struct	_SPatch* pNext;
}	SPatch;

typedef	enum
{
	SYM_XDEF,
	SYM_XREF,
	SYM_XBSS,
	SYM_LOCAL
}	ESymbolType;

typedef	struct	_SSymbol
{
	char		sName[256];
	ULONG		iOffset;
	ULONG		iNumber;
	ULONG		iSize;
	ESymbolType	Type;

	struct	_SSymbol* pNext;
}	SSymbol;

typedef	struct	_SSection
{
	char	sName[256];
	ULONG	iAlign;
	UWORD	iGroup;
	ULONG	iNumber;

	UBYTE* pData;
	ULONG	iSize;

	struct	_SSection* pNext;
	SPatch* pPatches;
	SSymbol* pSymbols;
	int		oDumped;
}	SSection;

void	Disassemble(SSection* pSect);
void	ByteDump(SSection* pSect);
void	BSSDump(SSection* pSect);

SSymbol* CreateSymbol(SSection* pSect, ESymbolType iType)
{
	SSymbol* s;

	s = malloc(sizeof(SSymbol));
	s->pNext = pSect->pSymbols;
	s->Type = iType;

	pSect->pSymbols = s;

	return s;
}

SPatch* CreatePatch(SSection* pSect, EPatchType iType)
{
	SPatch* p;

	p = malloc(sizeof(SPatch));
	p->pNext = pSect->pPatches;
	p->pExpr = NULL;
	p->Type = iType;

	pSect->pPatches = p;

	return p;
}

SExpression* Expr_Sub(SExpression* pLeft, SExpression* pRight)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = pLeft;
	expr->pRight = pRight;
	expr->Operator = OP_SUB;

	return expr;
}

SExpression* Expr_Constant(SLONG iConst)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = NULL;
	expr->pRight = NULL;
	expr->iValue = iConst;
	expr->Operator = OP_CONSTANT;

	return expr;
}

SExpression* Expr_SectBase(UWORD iSect)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = NULL;
	expr->pRight = NULL;
	expr->iValue = iSect;
	expr->Operator = OP_SECTBASE;

	return expr;
}

SExpression* Expr_SectEnd(UWORD iSect)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = NULL;
	expr->pRight = NULL;
	expr->iValue = iSect;
	expr->Operator = OP_SECTEND;

	return expr;
}

SExpression* Expr_SectSize(UWORD iSect)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = NULL;
	expr->pRight = NULL;
	expr->iValue = iSect;
	expr->Operator = OP_SECTSIZE;

	return expr;
}

SExpression* Expr_SectStart(UWORD iSect)
{
	return Expr_Sub(Expr_SectEnd(iSect), Expr_SectSize(iSect));
}

SExpression* Expr_AddrOfSymbol(ULONG iSymbol)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = NULL;
	expr->pRight = NULL;
	expr->iValue = iSymbol;
	expr->Operator = OP_ADDROFSYMBOL;

	return expr;
}

SExpression* Expr_Add(SExpression* pLeft, SExpression* pRight)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = pLeft;
	expr->pRight = pRight;
	expr->Operator = OP_ADD;

	return expr;
}

SExpression* Expr_Mul(SExpression* pLeft, SExpression* pRight)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = pLeft;
	expr->pRight = pRight;
	expr->Operator = OP_MUL;

	return expr;
}

SExpression* Expr_Div(SExpression* pLeft, SExpression* pRight)
{
	SExpression* expr;

	expr = malloc(sizeof(SExpression));
	expr->pLeft = pLeft;
	expr->pRight = pRight;
	expr->Operator = OP_DIV;

	return expr;
}

SExpression* ReadExpression(FILE* f)
{
	int	op;

	op = fgetc(f);

	switch (op)
	{
	case	0x00:
		return Expr_Constant(fgetll(f));
		break;
	case	0x02:
		return Expr_AddrOfSymbol(fgetlw(f));
		break;
	case	0x04:
		return Expr_SectBase(fgetlw(f));
		break;
	case	0x0C:
		return Expr_SectStart(fgetlw(f));
		break;
	case	0x16:
		return Expr_SectEnd(fgetlw(f));
		break;
	case	0x30:
		return Expr_Mul(ReadExpression(f), ReadExpression(f));
		break;
	case 0x32:
		return Expr_Div(ReadExpression(f), ReadExpression(f));
		break;
	case 0x36:
		return Expr_Add(ReadExpression(f), ReadExpression(f));
		break;
	case	0x2C:
		return Expr_Add(ReadExpression(f), ReadExpression(f));
		break;
	case 0x2E:
		return Expr_Sub(ReadExpression(f), ReadExpression(f));
		break;
	default:
		Error("Unsupported op 0x%02X in patch at 0x%X", op, ftell(f));
		break;
	}

	//	args:
	//	0, constant(LONG)
	//	4, sectbase(WORD)

	return NULL;
}

SSection* pSections = NULL;

SSection* GetSection(ULONG iID)
{
	SSection** ppSect = &pSections;

	while (*ppSect)
	{
		if ((*ppSect)->iNumber == iID)
		{
			return *ppSect;
		}
		ppSect = &((*ppSect)->pNext);
	}

	*ppSect = malloc(sizeof(SSection));
	(*ppSect)->pNext = NULL;
	(*ppSect)->iNumber = iID;
	(*ppSect)->pData = NULL;
	(*ppSect)->iSize = 0;
	(*ppSect)->pPatches = NULL;
	(*ppSect)->pSymbols = NULL;
	(*ppSect)->oDumped = 0;

	return *ppSect;
}

void	SectionDump(SSection* pSect)
{
	if (pSect->oDumped)
		return;

	if (pSect->iNumber == 0)
	{
		SSymbol* pSym;

		pSym = pSect->pSymbols;
		while (pSym)
		{
			pSym = pSym->pNext;
		}
	}
	else
	{
		if ((strcmp(pSect->sName, ".text") == 0)
			|| (strcmp(pSect->sName, ".ctors") == 0)
			|| (strcmp(pSect->sName, ".dtors") == 0))
		{
			Disassemble(pSect);
			pSect->oDumped = 1;
		}
		else if ((strcmp(pSect->sName, ".data") == 0)
			|| (strcmp(pSect->sName, ".rdata") == 0)
			|| (strcmp(pSect->sName, ".sdata") == 0))
		{
			//printf("\n$DATA_%s:\n", pSect->sName + 1);
			//ByteDump(pSect);
			pSect->oDumped = 1;
		}
		else if ((strcmp(pSect->sName, ".sbss") == 0)
			|| (strcmp(pSect->sName, ".bss") == 0))
		{
			//BSSDump(pSect);
			pSect->oDumped = 1;
		}
		else
		{
			//printf("; NOT IMPLEMENTED\n\n\n\n");
		}
	}
	//printf("\n");
}

char* reg[32] =
{
	"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
	"t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra"
};

char* c0reg[32] =
{
	"inx", "rand", "tlblo", "bpc", "ctxt", "bda", "pidmask", "dcic",
	"badvaddr",	"bdam", "tlbhi", "bpcm", "sr", "cause", "epc", "prid",
	"erreg", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
	"r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
};

char* psyq_c2dreg[32] =
{
	"vxy0", "vz0", "vxy1", "vz1", "vxy2", "vz2", "rgb", "otz",
	"ir0", "ir1", "ir2", "ir3", "sxy0", "sxy1", "sxy2", "sxyp",
	"sz0", "sz1", "sz2", "sz3", "rgb0", "rgb1", "rgb2", "r23",
	"mac0", "mac1", "mac2", "mac3", "irgb", "orgb", "lzcs", "lzcr"
};

char* psyq_c2creg[32] =
{
	"r11r12", "r13r21", "r22r23", "r31r32", "r33", "trx", "try", "trz",
	"l11l12", "l13l21", "l22l23", "l31l32", "l33", "rbk", "gbk", "bbk",
	"lr1lr2", "lr3lg1", "lg2lg3", "lb1lb2", "lb3", "rfc", "gfc", "bfc",
	"ofx","ofy", "h", "dqa", "dqb", "zsf3", "zsf4", "flag"
};

char* cw_c2dreg[32] =
{
	"C2_VXY0", "C2_VZ0", "C2_VXY1", "C2_VZ1", "C2_VXY2", "C2_VZ2", "C2_RGB", "C2_OTZ",
	"C2_IR0", "C2_IR1", "C2_IR2", "C2_IR3", "C2_SXY0", "C2_SXY1", "C2_SXY2", "C2_SXYP",
	"C2_SZ0", "C2_SZ1", "C2_SZ2", "C2_SZ3", "C2_RGB0", "C2_RGB1", "C2_RGB2", "r23",
	"C2_MAC0", "C2_MAC1", "C2_MAC2", "C2_MAC3", "C2_IRGB", "C2_ORGB", "C2_LZCS", "C2_LZCR"
};

char* cw_c2creg[32] =
{
	"C2_R11R12", "C2_R13R21", "C2_R22R23", "C2_R31R32", "C2_R33", "C2_TRX", "C2_TRY", "C2_TRZ",
	"C2_L11L12", "C2_L13L21", "C2_L22L23", "C2_L31L32", "C2_L33", "C2_RBK", "C2_GBK", "C2_BBK",
	"C2_LR1LR2", "C2_LR3LG1", "C2_LG2LG3", "C2_LB1LB2", "C2_LB3", "C2_RFC", "C2_GFC", "C2_BFC",
	"C2_OFX","C2_OFY", "C2_H", "C2_DQA", "C2_DQB", "C2_ZSF3", "C2_ZSF4", "C2_FLAG"
};

char** pc2dreg = psyq_c2dreg;
char** pc2creg = psyq_c2creg;

void	PrintUsage(void)
{
	printf("MipsDis V1.4 by SurfSmurf, Minor updates by Doomed.\n"
		"Usage: ObjDis [options] file.obj\n"
		"\n"
		"Available options:\n"
		"\t-a : Alternative GTE decoding (for CW)\n");

	exit(0);
}

void	FreeExpression(SExpression* pExpr)
{
	if (pExpr)
	{
		FreeExpression(pExpr->pLeft);
		FreeExpression(pExpr->pRight);
		free(pExpr);
	}
}

void	FixPatchesAndSymbols(void)
{
	SSection* pSect;

	pSect = pSections;

	while (pSect)
	{
		SPatch* pPatch;

		pPatch = pSect->pPatches;
		while (pPatch)
		{
			SExpression* pExpr = pPatch->pExpr;

			if (pExpr->Operator == OP_ADD
				&& ((pExpr->pLeft->Operator == OP_SECTBASE
					&& pExpr->pRight->Operator == OP_CONSTANT)
					|| (pExpr->pRight->Operator == OP_SECTBASE
						&& pExpr->pLeft->Operator == OP_CONSTANT)))
			{
				SExpression* pSectExpr;
				SExpression* pConstExpr;
				SSymbol* pSym;
				SSection* pSymSect;

				pSectExpr = pExpr->pLeft;
				pConstExpr = pExpr->pRight;

				if (pSectExpr->Operator != OP_SECTBASE)
				{
					SExpression* t;

					t = pSectExpr;
					pSectExpr = pConstExpr;
					pConstExpr = t;
				}

				pSymSect = GetSection(pSectExpr->iValue);

				pSym = pSymSect->pSymbols;
				while (pSym)
				{
					if (pSym->iOffset == pConstExpr->iValue)
						break;
					pSym = pSym->pNext;
				}

				if (pSym == NULL)
				{
					pSym = CreateSymbol(pSymSect, SYM_LOCAL);
					pSym->iNumber = iSymbolNumber;
					pSym->iOffset = pConstExpr->iValue;
					sprintf(pSym->sName, "%s_%X", pSymSect->sName + 1, pSym->iOffset);
					iSymbolNumber += 1;
				}
				FreeExpression(pExpr);
				pPatch->pExpr = Expr_AddrOfSymbol(pSym->iNumber);

			}

			pPatch = pPatch->pNext;
		}
		pSect = pSect->pNext;
	}
}

void	FixRelativeJumps(SSection* pSect);

void parse_obj(const char* path, const char* dst_path, int from_lib) {
	SSection* pCurrentSection = NULL;
	FILE* f;
	ULONG	id;
	int		ok = 1;
	int		totalsections = 0;
	int		PatchOffset = 0;

	pSections = NULL;

	if ((f = fopen(path, "rb")) == NULL)
		Error("File \"%s\" not found", path);


	if (!from_lib)
	{
		if ((dest = fopen(dst_path, "wb")) == NULL)
			Error("File \"%s\" not found", dst_path);
	}

	char* pp = strrchr(path, '/');
	char* pp2 = strrchr(path, '\\');
	sprintf(ttmp, "==%s==\n", pp ? &pp[1] : (pp2 ? &pp2[1] : path));
	fwrite(ttmp, 1, strlen(ttmp), dest);
	fflush(dest);

	id = fgetll(f);
	if (id != 0x024B4E4C)
		Error("Not an object-file");

	while (ok)
	{
		int	chunk;

		chunk = fgetc(f);
		switch (chunk)
		{
		case	0:
		{
			ok = 0;
			break;
		}
		case	2:
		{
			//	Code
			int	len;

			len = fgetlw(f);
			pCurrentSection->pData = realloc(pCurrentSection->pData, pCurrentSection->iSize + len);
			fread(pCurrentSection->pData + pCurrentSection->iSize, 1, len, f);
			pCurrentSection->iSize += len;
			break;
		}
		case	6:
		{
			//	Switch to section
			int	id;

			id = fgetlw(f);
			pCurrentSection = GetSection(id);
			PatchOffset = pCurrentSection->iSize;
			break;
		}
		case	8:
		{
			//	Uninitialised data
			int	size;

			size = fgetll(f);

			pCurrentSection->iSize += size;
			break;
		}
		case	10:
		{
			//	Patch
			int			type;
			int			offset;
			EPatchType	ntype;
			SPatch* p;

			type = fgetc(f);
			offset = fgetlw(f);

			switch (type)
			{
			case	16:
				ntype = PATCH_LONG;
				break;
			case	26:
				ntype = PATCH_WORD;	//	lui
				break;
			case	28:
				ntype = PATCH_WORD;	//	ori
				break;
			case 30:
				ntype = PATCH_MIPSFP;
			case	74:
				ntype = PATCH_LONG;
				break;
			case	82:
				ntype = PATCH_MIPSHI;
				break;
			case	84:
				ntype = PATCH_MIPSLO;
				break;
			case	100:
				ntype = PATCH_MIPSGP;
				break;
			default:
				Error("Patch type %d at 0x%X unsupported", type, ftell(f));
			}
			p = CreatePatch(pCurrentSection, ntype);
			p->iOffset = offset + PatchOffset;
			p->pExpr = ReadExpression(f);

			break;
		}
		case	12:
		{
			//	XDEF symbol
			int		number;
			int		section;
			int		len;
			ULONG	offset;
			SSymbol* pSym;

			number = fgetlw(f);
			section = fgetlw(f);
			offset = fgetll(f);

			pSym = CreateSymbol(GetSection(section), SYM_XDEF);
			pSym->iOffset = offset;
			pSym->iNumber = number;
			len = fgetc(f);
			fread(pSym->sName, 1, len, f);
			pSym->sName[len] = 0;
			break;
		}
		case	14:
		{
			//	XREF symbol
			int		number;
			int		len;

			SSymbol* pSym;

			number = fgetlw(f);

			pSym = CreateSymbol(GetSection(0), SYM_XREF);
			pSym->iNumber = number;
			len = fgetc(f);
			fread(pSym->sName, 1, len, f);
			pSym->sName[len] = 0;
			break;
		}
		case	16:
		{
			//	Create section
			SSection* pSect;
			int			len;
			int			id;

			pSect = GetSection(id = fgetlw(f));
			pSect->iGroup = fgetc(f);	//	GROUP
			pSect->iAlign = fgetlw(f);	//	ALIGNMENT

			len = fgetc(f);
			fread(pSect->sName, 1, len, f);
			pSect->sName[len] = 0;

			if (id > totalsections)
			{
				totalsections = id;
			}
			break;
		}
		case	18:
		{
			//	LOCAL symbol
			int		section;
			int		len;
			ULONG	offset;
			SSymbol* pSym;

			section = fgetlw(f);
			offset = fgetll(f);

			pSym = CreateSymbol(GetSection(section), SYM_LOCAL);
			pSym->iOffset = offset;
			pSym->iNumber = iSymbolNumber++;
			len = fgetc(f);
			fread(pSym->sName, 1, len, f);
			pSym->sName[len] = 0;
			break;
		}
		case	28:
		{
			//	File number and name
			int	number;
			int	len;

			number = fgetlw(f);
			len = fgetc(f);
			fseek(f, len, SEEK_CUR);

			break;
		}
		case	46:
		{
			//	CPU type

			int	cpu;
			cpu = fgetc(f);
			if (cpu != 7)
				Error("CPU type %d not supported", cpu);
			break;
		}
		case	48:
		{
			//	XBSS symbol
			int		number;
			int		section;
			int		len;
			ULONG	size;
			SSymbol* pSym;
			SSection* pSect;

			number = fgetlw(f);
			section = fgetlw(f);
			size = fgetll(f);

			pSect = GetSection(section);

			pSym = CreateSymbol(pSect, SYM_XBSS);
			pSym->iOffset = pSect->iSize;
			pSym->iSize = size;
			pSect->iSize += size;
			pSym->iNumber = number;
			len = fgetc(f);
			fread(pSym->sName, 1, len, f);
			pSym->sName[len] = 0;
			break;

		}
		case 60:
		{
			fgetlw(f);
			break;
		}
		case 74:
		{
			// function start
			int section;
			int offset;
			int file;
			int start_line;
			int frame_reg;
			int frame_size;
			int ret_pc_reg;
			int mask;
			int mask_off;
			int len;
			char name[256];

			section = fgetlw(f);
			offset = fgetll(f);
			file = fgetlw(f);
			start_line = fgetll(f);
			frame_reg = fgetlw(f);
			frame_size = fgetll(f);
			ret_pc_reg = fgetlw(f);
			mask = fgetll(f);
			mask_off = fgetll(f);

			len = fgetc(f);
			fread(name, 1, len, f);
			name[len] = 0;

			break;
		}
		case 76:
		{
			int section;
			int offset;
			int end_line;

			section = fgetlw(f);
			offset = fgetll(f);
			end_line = fgetll(f);

			break;
		}
		default:
			Error("Chunk %d at 0x%X not supported", chunk, ftell(f));
		}
	}

	FixPatchesAndSymbols();
	SectionDump(GetSection(0));
	pCurrentSection = pSections;
	while (pCurrentSection)
	{
		if (strcmp(pCurrentSection->sName, ".text") == 0)
		{
			FixRelativeJumps(pCurrentSection);
		}
		pCurrentSection = pCurrentSection->pNext;
	}

	pCurrentSection = pSections;
	while (pCurrentSection)
	{
		if ((pCurrentSection->iNumber != 0)
			&& ((strcmp(pCurrentSection->sName, ".sbss") == 0)
				|| (strcmp(pCurrentSection->sName, ".sdata") == 0)))
		{
			SectionDump(pCurrentSection);
		}
		pCurrentSection = pCurrentSection->pNext;
	}

	pCurrentSection = pSections;
	while (pCurrentSection)
	{
		if (pCurrentSection->iNumber != 0)
		{
			SectionDump(pCurrentSection);
		}
		pCurrentSection = pCurrentSection->pNext;
	}

	//getchar();

	pCurrentSection = pSections;
	while (pCurrentSection)
	{
		SSection* ptr = pCurrentSection->pNext;
		free(pCurrentSection);
		pCurrentSection = ptr;
	}

	fclose(f);

	if (!from_lib)
		fclose(dest);
}

char* trimwhitespace(char* str)
{
	char* end;

	// Trim leading space
	while (isspace((unsigned char)*str)) str++;

	if (*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;

	// Write new null terminator character
	end[1] = '\0';

	return &end[1];
}

void parse_lib(const char* path, const char* dest_path)
{
	FILE* f;
	ULONG	id;

	if ((f = fopen(path, "rb")) == NULL)
		Error("File \"%s\" not found", path);

	if ((dest = fopen(dest_path, "wb")) == NULL)
		Error("File \"%s\" not found", dest_path);

	id = fgetll(f);
	if (id != 0x0142494C && id != 0x0242494C)
		Error("Not an LIB-file");

	switch (id)
	{
	case 0x0142494C:
	{
		int		ok = 1;
		char name[13];
		unsigned int date;
		unsigned int offset, base_off = 4;
		unsigned int size;
		char* name_end;
		unsigned char* tmp;

		while (1)
		{
			ok = fread(name, 1, 8, f) != 0;
			if (!ok)
				break;

			name[8] = 0;
			name_end = trimwhitespace(name);
			name_end[0] = '.';
			name_end[1] = 'O';
			name_end[2] = 'B';
			name_end[3] = 'J';
			name_end[4] = 0;

			fread(&date, 1, 4, f);
			fread(&offset, 1, 4, f);
			fread(&size, 1, 4, f);

			tmp = (unsigned char*)malloc(size - offset);
			fseek(f, offset + base_off, SEEK_SET);
			fread(tmp, 1, size - offset, f);

			FILE* w = fopen(name, "wb");
			fwrite(tmp, 1, size - offset, w);
			fflush(dest);
			fclose(w);

			free(tmp);

			parse_obj(name, NULL, 1);
			remove(name);
			fwrite("\n\n", 1, 2, dest);
			fflush(dest);

			base_off += size;
			fseek(f, base_off, SEEK_SET);
		}
	} break;
	case 0x0242494C:
	{
		int		ok = 1;
		unsigned int info_off = 0;
		unsigned int info_len = 0;

		ok = fread(&info_off, 1, 4, f) != 0;
		if (!ok)
			break;

		ok = fread(&info_len, 1, 4, f) != 0;

		fseek(f, info_off, SEEK_SET);

		while (info_len)
		{
			unsigned int data_offset = 0;
			unsigned int data_size = 0;
			unsigned int time = 0;
			unsigned char name_len = 0;
			char name[257];
			unsigned char items_count = 0;

			fread(&data_offset, 1, 4, f); info_len -= 4;
			fread(&data_size, 1, 4, f); info_len -= 4;
			fread(&time, 1, 4, f); info_len -= 4;
			fread(&name_len, 1, 1, f); info_len -= 1;
			name_len += 1;

			fread(name, 1, name_len, f); info_len -= name_len;
			//printf("%s:\n", name);

			fread(&items_count, 1, 1, f); info_len -= 1;

			long pos = ftell(f);

			unsigned char* tmp = (unsigned char*)malloc(data_size);
			fseek(f, data_offset, SEEK_SET);
			fread(tmp, 1, data_size, f);

			FILE* w = fopen(name, "wb");
			fwrite(tmp, 1, data_size, w);
			fflush(dest);
			fclose(w);

			free(tmp);

			parse_obj(name, NULL, 1);
			remove(name);
			fwrite("\n\n", 1, 2, dest);
			fflush(dest);

			fseek(f, pos, SEEK_SET);

			while (items_count)
			{
				short unkn;
				unsigned char name_len2 = 0;
				char name2[257];

				fread(&unkn, 1, 2, f); info_len -= 2;
				fread(&name_len2, 1, 1, f); info_len -= 1;
				name_len2 += 1;

				fread(name2, 1, name_len2, f); info_len -= name_len2;
				//printf("\t%s\n", name2);

				fread(&items_count, 1, 1, f); info_len -= 1;
			}
		}
	} break;
	}

	fclose(f);
	fclose(dest);
}

int	main(int argc, char* argv[])
{
	int		argn = 1;

	if (argc <= 1)
		PrintUsage();

	while (argn < argc && argv[argn][0] == '-')
	{
		switch (argv[argn][1])
		{
		case	'a':
			pc2creg = cw_c2creg;
			pc2dreg = cw_c2dreg;
			OPT_ALTGTE = 1;
			break;
		default:
			Error("Unknown option '%c'", argv[argn][1]);
			break;
		}
		argn += 1;
	}

	int name_len = strlen(argv[argn]);
	char* dest_name = (char*)malloc(name_len + 4 + 1);
	strncpy(dest_name, argv[argn], name_len);
	dest_name[name_len + 0] = '.';
	dest_name[name_len + 1] = 'T';
	dest_name[name_len + 2] = 'X';
	dest_name[name_len + 3] = 'T';
	dest_name[name_len + 4] = 0;

	if (strstr(argv[argn], ".OBJ") || strstr(argv[argn], ".obj"))
		parse_obj(argv[argn], dest_name, 0);
	else if (strstr(argv[argn], ".LIB") || strstr(argv[argn], ".lib"))
		parse_lib(argv[argn], dest_name);

	free(dest_name);

	return EXIT_SUCCESS;
}

unsigned char getB0(unsigned int dw)
{
	return (dw >> 24) & 0xFF;
}

unsigned char getB1(unsigned int dw)
{
	return (dw >> 16) & 0xFF;
}

unsigned char getB2(unsigned int dw)
{
	return (dw >> 8) & 0xFF;
}

unsigned char getB3(unsigned int dw)
{
	return (dw >> 0) & 0xFF;
}

void printWordAndUWordMasked(unsigned int dw)
{
	unsigned char b1 = getB1(dw);
	unsigned char b0 = getB0(dw);

	sprintf(ttmp, "?? ?? %02X %02X ", b1, b0);

	fwrite(ttmp, 1, strlen(ttmp), dest);
	fflush(dest);
}

void printWord(unsigned int dw)
{
	unsigned char b1 = getB1(dw);
	unsigned char b0 = getB0(dw);

	sprintf(ttmp, "%02X %02X ", b1, b0);

	fwrite(ttmp, 1, strlen(ttmp), dest);
	fflush(dest);
}

void printDword(unsigned int dw)
{
	unsigned char b0 = getB3(dw);
	unsigned char b1 = getB2(dw);
	unsigned char b2 = getB1(dw);
	unsigned char b3 = getB0(dw);

	sprintf(ttmp, "%02X %02X %02X %02X ", b0, b1, b2, b3);

	fwrite(ttmp, 1, strlen(ttmp), dest);
	fflush(dest);
}

void	DumpLong(ULONG data)
{
	printDword(data);
	//printf("DW\t$%08X", data);
}

SSymbol* GetSymbol(ULONG iID)
{
	SSection* pSect;

	pSect = pSections;
	while (pSect)
	{
		SSymbol* pSym;

		pSym = pSect->pSymbols;
		while (pSym)
		{
			if (pSym->iNumber == iID)
			{
				return pSym;
			}
			pSym = pSym->pNext;
		}
		pSect = pSect->pNext;
	}

	return NULL;
}

char* GetSymbolName(SSection* pSect, ULONG iOffset, SLONG iRel)
{
	static	char	temp[256];
	SSymbol* pSym;

	iOffset += iRel;

	pSym = pSect->pSymbols;
	while (pSym)
	{
		if (pSym->iOffset == iOffset)
		{
			sprintf(temp, "%s", pSym->sName);
			return temp;
		}
		pSym = pSym->pNext;
	}

	sprintf(temp, "*%+d", iRel);
	return temp;
}

char* FormatExpr(SExpression* pExpr, char* pBuf)
{
	switch (pExpr->Operator)
	{
	case	OP_CONSTANT:
		sprintf(pBuf, "$%X", pExpr->iValue);
		break;
	case	OP_SECTBASE:
		sprintf(pBuf, "sectbase(%s)", GetSection(pExpr->iValue)->sName);
		break;
	case	OP_SECTEND:
		sprintf(pBuf, "sectend(%s)", GetSection(pExpr->iValue)->sName);
		break;
	case	OP_SECTSIZE:
		sprintf(pBuf, "sectsize(%s)", GetSection(pExpr->iValue)->sName);
		break;
	case	OP_ADDROFSYMBOL:
		sprintf(pBuf, "%s", GetSymbol(pExpr->iValue)->sName);
		break;
	case	OP_ADD:
		sprintf(pBuf, "(");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pLeft, pBuf);
		sprintf(pBuf, "+");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pRight, pBuf);
		sprintf(pBuf, ")");
		pBuf += 1;
		break;
	case	OP_MUL:
		pBuf = FormatExpr(pExpr->pLeft, pBuf);
		sprintf(pBuf, "*");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pRight, pBuf);
		break;
	case OP_DIV:
		pBuf = FormatExpr(pExpr->pLeft, pBuf);
		sprintf(pBuf, "/");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pRight, pBuf);
		break;
	case	OP_SUB:
		sprintf(pBuf, "(");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pLeft, pBuf);
		sprintf(pBuf, "-");
		pBuf += 1;
		pBuf = FormatExpr(pExpr->pRight, pBuf);
		sprintf(pBuf, ")");
		pBuf += 1;
		break;
	}

	return pBuf + strlen(pBuf);
}

void WordPatch(SPatch* pPatch, ULONG data, int size)
{
	if (pPatch)
	{
		for (int i = 0; i < size; ++i)
		{
			fwrite("?? ", 1, 3, dest);
			fflush(dest);
		}
	}
	else
	{
		for (int i = 0; i < size; ++i)
		{
			sprintf(ttmp, "%02X ", data & 0xFF);
			fwrite(ttmp, 1, strlen(ttmp), dest);
			fflush(dest);
			data >>= 8;
		}
	}
}

void	Disassemble(SSection* pSect)
{
	ULONG	index = 0;
	ULONG	size;
	ULONG	PC = 0;
	SSymbol* pSym;
	int has_name = 0;

	pSym = pSect->pSymbols;
	while (pSym)
	{
		pSym = pSym->pNext;
	}

	size = pSect->iSize;

	while (size)
	{
		ULONG	data;
		ULONG	code1,
			code2;
		SPatch* pPatch;

		pSym = pSect->pSymbols;
		while (pSym)
		{
			if (pSym->iOffset == index)
			{
				sprintf(ttmp, "\n%s:\n", pSym->sName);
				fwrite(ttmp, 1, strlen(ttmp), dest);
				fflush(dest);
				has_name = 1;
			}
			pSym = pSym->pNext;
		}

		if (!has_name)
		{
			sprintf(ttmp, "\nloc_%X:\n", index);
			fwrite(ttmp, 1, strlen(ttmp), dest);
			fflush(dest);
			has_name = 1;
		}

		pPatch = pSect->pPatches;
		while (pPatch)
		{
			if ((pPatch->iOffset >= index) && (pPatch->iOffset <= index + 3))
			{
				break;
			}
			pPatch = pPatch->pNext;
		}

		data = pSect->pData[index++];
		data |= pSect->pData[index++] << 8;
		data |= pSect->pData[index++] << 16;
		data |= pSect->pData[index++] << 24;
		size -= 4;

		PC += 4;

		code1 = (data >> 29) & 0x7;
		code2 = (data >> 26) & 0x7;

		switch (code1)
		{
		case 0:
			switch (code2)
			{
			case 0:
			case 1:
			case 4:
			case 5:
			case 6:
			case 7:
				printDword(data);
				break;
			case 2:
			case 3:
				WordPatch(pPatch, ((data & 0x03FFFFFF) << 2), 3);
				sprintf(ttmp, "%02X ", getB0(data));
				fwrite(ttmp, 1, strlen(ttmp), dest);
				fflush(dest);
				break;
			}
			break;
		case 1:
		case 4:
		case 5:
		case 6:
		case 7:
			WordPatch(pPatch, (UWORD)data, 2);
			printWord(data);
			break;
		case 2:
			printDword(data);
			break;
		default:
			DumpLong(data);
			break;
		}
	}
}

void	BSSDump(SSection* pSect)
{
	SSymbol** ppSym;
	SSymbol* pSym;
	int	oSwapped = 1;

	while (oSwapped)
	{

		ppSym = &pSect->pSymbols;
		oSwapped = 0;
		while ((*ppSym) && (*ppSym)->pNext)
		{
			if ((*ppSym)->iOffset > (*ppSym)->pNext->iOffset)
			{
				SSymbol* t;

				t = (*ppSym);
				*ppSym = t->pNext;
				t->pNext = t;

				oSwapped = 1;
			}

			ppSym = &(*ppSym)->pNext;
		}
	}

	pSym = pSect->pSymbols;
	while (pSym)
	{
		int	size;

		if (pSym->pNext)
		{
			size = pSym->pNext->iOffset - pSym->iOffset;
		}
		else
		{
			size = pSect->iSize - pSym->iOffset;
		}
		pSym = pSym->pNext;
	}
}

void	FixRelativeJumps(SSection* pSect)
{
	ULONG	index = 0;
	ULONG	size;

	if (pSect->pData == NULL)
		return;

	size = pSect->iSize;

	while (size)
	{
		ULONG	data;
		ULONG	code1,
			code2;
		int		found;

		found = 0;

		data = pSect->pData[index++];
		data |= pSect->pData[index++] << 8;
		data |= pSect->pData[index++] << 16;
		data |= pSect->pData[index++] << 24;
		size -= 4;

		code1 = (data >> 29) & 0x7;
		code2 = (data >> 26) & 0x7;

		switch (code1)
		{
		case 0:
			switch (code2)
			{
			case 1:
				//	REGIMM function
			{
				ULONG	code3,
					code4;

				code3 = (data >> 19) & 0x3;
				code4 = (data >> 16) & 0x7;

				switch (code3)
				{
				case 0:
					switch (code4)
					{
					case 0:
					case 1:
					case 2:
					case 3:
						found = 1;
						break;
					}
					break;
				}
				break;
			}
			case 4:
			case 5:
			case 6:
			case 7:
				found = 1;
				break;
			}
			break;
		case 2:
			switch (code2)
			{
			case 4:
			case 5:
			case 6:
			case 7:
				found = 1;
				break;
			}
			break;
		}

		if (found)
		{
			SSymbol* pSym;
			int	offset = (SWORD)data;
			offset = (offset << 2) + index;

			pSym = pSect->pSymbols;
			while (pSym)
			{
				if (pSym->iOffset == offset)
					break;
				pSym = pSym->pNext;
			}

			if (pSym == NULL)
			{
				pSym = CreateSymbol(pSect, SYM_LOCAL);
				pSym->iNumber = iSymbolNumber;
				pSym->iOffset = offset;
				sprintf(pSym->sName, "%s_%X", "loc", pSym->iOffset);
				iSymbolNumber += 1;
			}
		}
	}
}

