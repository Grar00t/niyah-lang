#include "morph.h"
#define OFFSET(T,F) __builtin_offsetof(T,F)
_Static_assert(sizeof(Token)==40 && sizeof(Lexer)==32,"lexer ABI drift");
_Static_assert(OFFSET(Token,start)==16 && OFFSET(Token,len)==24,"token ABI drift");
_Static_assert(OFFSET(Token,value)==32,"integer token ABI drift");
_Static_assert(T_EOF==0 && T_ERROR==1 && T_IDENT==2 && T_INT==3,"token enum drift");
_Static_assert(T_SEMI==20 && T_STAR==26 && T_ASSIGN==34,"operator enum drift");
_Static_assert(sizeof(MorphAnalysis)==128 && sizeof(MorphResult)==8200,"morph ABI drift");
_Static_assert(OFFSET(MorphAnalysis,root)==108 && OFFSET(MorphAnalysis,root_len)==120,"root ABI drift");
_Static_assert(OFFSET(MorphResult,count)==8192 && OFFSET(MorphResult,truncated)==8196,"result ABI drift");
