#include "../src/parser.h"
#include "../src/check.h"
#include "../src/eval.h"
#include "../src/codegen.h"
#include <stdio.h>
#include <string.h>

static AstNode arena[8192];
static char source[32768];
static u8 elf[1 << 20];
static unsigned passed,failed;
static u32 used;
static int append(const char *text){
    size_t n=strlen(text);
    if(n>=sizeof(source)-used)return 0;
    memcpy(source+used,text,n+1);used+=(u32)n;return 1;
}
static int fixture(u32 blocks,u32 operators,u32 unary){
    u32 i;
    used=0;
    if(!append("دالة رئيسية() -> صحيح { "))return 0;
    for(i=0;i<blocks;i++)if(!append("{"))return 0;
    if(!append("أرجع "))return 0;
    for(i=0;i<unary;i++)if(!append("-"))return 0;
    if(!append("١"))return 0;
    for(i=0;i<operators;i++)if(!append(" + ١"))return 0;
    if(!append("؛"))return 0;
    for(i=0;i<blocks;i++)if(!append("}"))return 0;
    return append(" }");
}
static u32 parse(Parser *p){
    parser_init(p,arena,(u32)(sizeof arena/sizeof arena[0]),(const u8 *)source,used);
    return parser_parse(p);
}
static void verify(const char *name,int want_parse,int want_check,i64 wanted){
    Parser p;Checker c;EvalResult er;CodegenResult cr;u32 root;
    int checked,evaluated,compiled;
    root=parse(&p);
    if((root!=0)!=want_parse){
        fprintf(stderr,"FAIL %s parser: %s\n",name,p.error?p.error:"accepted");failed++;return;
    }
    if(!root){
        if(!p.error || !strstr(p.error,"nesting depth exceeded")){
            fprintf(stderr,"FAIL %s wrong parser boundary diagnostic\n",name);failed++;return;
        }
        passed++;return;
    }
    checker_init(&c,&p);checked=checker_run(&c,root);
    evaluated=eval_main(&p,root,&er);
    compiled=codegen_elf64(&p,root,elf,sizeof elf,&cr);
    if(checked!=want_check || evaluated!=want_check || compiled!=want_check ||
       (want_check && er.value!=wanted) ||
       (!want_check && (!c.message || !strstr(c.message,"expression nesting exceeded") ||
                       !er.error || !cr.error || !cr.semantic_error))){
        fprintf(stderr,"FAIL %s check=%d/%s eval=%d/%s/%ld native=%d/%s\n",name,
                checked,c.message?c.message:"OK",evaluated,er.error?er.error:"OK",(long)er.value,
                compiled,cr.error?cr.error:"OK");failed++;return;
    }
    passed++;
}
#define CASE(name,blocks,ops,unary,parsed,checked,want) do { \
    if(!fixture(blocks,ops,unary)){fputs("fixture capacity exceeded\n",stderr);return 1;} \
    verify(name,parsed,checked,want); \
} while(0)
int main(void){
    Parser p;
    u32 blocks,first_rejected=0;
    CASE("flat expression below checker bound",0,CHECK_MAX_DEPTH-1,0,1,1,CHECK_MAX_DEPTH);
    CASE("flat expression at checker bound",0,CHECK_MAX_DEPTH,0,1,1,CHECK_MAX_DEPTH+1);
    CASE("flat expression exceeds checker bound",0,CHECK_MAX_DEPTH+1,0,1,0,0);
    CASE("mixed block and expression budgets",10,CHECK_MAX_DEPTH-8,0,1,1,CHECK_MAX_DEPTH-7);
    CASE("flat 1000 operators fails semantic guard",0,1000,0,1,0,0);
    CASE("20 unary operators",0,0,20,1,1,1);
    CASE("60 unary operators",0,0,60,1,1,1);
    CASE("120 unary operators",0,0,120,1,1,1);
    CASE("unary exceeds parser guard",0,0,NIYAH_PARSE_MAX_DEPTH+1,0,0,0);
    /* Parser recursion counts helper calls; do not pretend one source block
     * equals one depth unit or merge it with the semantic-expression budget. */
    for(blocks=1;blocks<=NIYAH_PARSE_MAX_DEPTH+1;blocks++){
        if(!fixture(blocks,0,0))return 1;
        if(!parse(&p)){first_rejected=blocks;break;}
    }
    if(!first_rejected){fputs("FAIL parser block nesting was not bounded\n",stderr);return 1;}
    CASE("last accepted parser block fixture",first_rejected-1,0,0,1,1,1);
    CASE("first rejected parser block fixture",first_rejected,0,0,0,0,0);
    /* Leave parser-helper headroom for binary operands within deep blocks. */
    CASE("deep blocks retain expression budget",first_rejected>3?first_rejected-3:0,CHECK_MAX_DEPTH,0,1,1,CHECK_MAX_DEPTH+1);
    if(failed)return 1;
    printf("depth_test: %u cases PASS (parser guard=%u, expression guard=%u, block fixture boundary=%u)\n",
           passed,(unsigned)NIYAH_PARSE_MAX_DEPTH,(unsigned)CHECK_MAX_DEPTH,(unsigned)first_rejected);
    return 0;
}
