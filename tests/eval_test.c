#include "../src/eval.h"
#include <stdio.h>
#include <string.h>

static AstNode arena[16384];
static unsigned passed, failed;
static void verify(const char *name,const char *source,int want_ok,i64 want,const char *message){
    Parser parser;
    EvalResult result;
    u32 root;
    int ok;
    parser_init(&parser,arena,16384,(const u8 *)source,(u64)strlen(source));
    root=parser_parse(&parser);
    if(!root){fprintf(stderr,"FAIL %s: syntax fixture: %s\n",name,parser.error);failed++;return;}
    ok=eval_main(&parser,root,&result);
    if(ok!=want_ok || (ok && result.value!=want) ||
       (!ok && (!result.error || (message && !strstr(result.error,message))))){
        fprintf(stderr,"FAIL %s: ok=%d value=%ld error=%s wanted=%ld/%s\n",
                name,ok,(long)result.value,result.error?result.error:"none",
                (long)want,message?message:"success");
        failed++;
    } else passed++;
}
#define GOOD(n,s,v) verify(n,s,1,v,0)
#define BAD(n,s,m) verify(n,s,0,0,m)
int main(void){
    char generated[8192],name[64];
    size_t used;
    int i;
    GOOD("precedence","دالة رئيسية() -> صحيح { أرجع ٢ + ٣ * ٤؛ }",14);
    GOOD("parentheses","دالة رئيسية() -> صحيح { أرجع (٢ + ٣) * ٤؛ }",20);
    GOOD("negative division","دالة رئيسية() -> صحيح { أرجع -٧ / ٣؛ }",-2);
    GOOD("negative remainder","دالة رئيسية() -> صحيح { أرجع -٧ % ٣؛ }",-1);
    GOOD("bitwise","دالة رئيسية() -> صحيح { أرجع (٦ & ٣) | (٨ ^ ١)؛ }",11);
    GOOD("bitwise not","دالة رئيسية() -> صحيح { أرجع ~١؛ }",-2);
    GOOD("left shift","دالة رئيسية() -> صحيح { أرجع ٣ << ٤؛ }",48);
    GOOD("arithmetic right shift","دالة رئيسية() -> صحيح { أرجع -١٦ >> ٢؛ }",-4);
    GOOD("right shift 63","دالة رئيسية() -> صحيح { أرجع -١ >> ٦٣؛ }",-1);
    GOOD("zero left shift 63","دالة رئيسية() -> صحيح { أرجع ٠ << ٦٣؛ }",0);
    GOOD("negative left shift boundary","دالة رئيسية() -> صحيح { أرجع -١ << ٦٣؛ }",(-9223372036854775807L-1));
    GOOD("signed minimum","دالة رئيسية() -> صحيح { أرجع -9223372036854775807 - ١؛ }",(-9223372036854775807L-1));
    GOOD("signed maximum","دالة رئيسية() -> صحيح { أرجع 9223372036854775807؛ }",9223372036854775807L);
    GOOD("local assignment","دالة رئيسية() -> صحيح { متغير س: صحيح؛ س = ٧؛ س = س + ٢؛ أرجع س؛ }",9);
    GOOD("nested lexical shadow","دالة رئيسية() -> صحيح { متغير س: صحيح = ٥؛ { متغير س: صحيح = ٩؛ س = ١؛ } أرجع س؛ }",5);
    GOOD("while sum","دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ متغير ج: صحيح = ٠؛ طالما (س <= ١٠) { ج = ج + س؛ س = س + ١؛ } أرجع ج؛ }",55);
    GOOD("loop local lifetime","دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ متغير ج: صحيح = ٠؛ طالما (س < ٣) { متغير ق: صحيح = ٢؛ ج = ج + ق؛ س = س + ١؛ } أرجع ج؛ }",6);
    GOOD("nested loops","دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ متغير ج: صحيح = ٠؛ طالما (س < ٣) { متغير ص: صحيح = ٠؛ طالما (ص < ٤) { ج = ج + ١؛ ص = ص + ١؛ } س = س + ١؛ } أرجع ج؛ }",12);
    GOOD("else if","دالة رئيسية() -> صحيح { متغير س: صحيح = ٢؛ إذا (س == ١) { أرجع ١؛ } وإلا إذا (س == ٢) { أرجع ٧؛ } وإلا { أرجع ٣؛ } }",7);
    GOOD("return through loop","دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < ٥) { إذا (س == ٣) { أرجع س؛ } س = س + ١؛ } أرجع -١؛ }",3);
    GOOD("forward function","دالة رئيسية() -> صحيح { أرجع زد(٨)؛ } دالة زد(س: صحيح) -> صحيح { أرجع س + ١؛ }",9);
    GOOD("recursive factorial","دالة رئيسية() -> صحيح { أرجع مضروب(٦)؛ } دالة مضروب(س: صحيح) -> صحيح { إذا (س <= ١) { أرجع ١؛ } أرجع س * مضروب(س - ١)؛ }",720);
    GOOD("mutual recursion","دالة زوج(س: صحيح) -> منطقي { إذا (س == ٠) { أرجع ١ == ١؛ } أرجع فرد(س - ١)؛ } دالة فرد(س: صحيح) -> منطقي { إذا (س == ٠) { أرجع ١ == ٠؛ } أرجع زوج(س - ١)؛ } دالة رئيسية() -> صحيح { إذا (زوج(١٠)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }",1);
    GOOD("eight arguments","دالة جمع(أ: صحيح، ب: صحيح، ج: صحيح، د: صحيح، ه: صحيح، و: صحيح، ز: صحيح، ح: صحيح) -> صحيح { أرجع أ + ب + ج + د + ه + و + ز + ح؛ } دالة رئيسية() -> صحيح { أرجع جمع(١،٢،٣،٤،٥،٦،٧،٨)؛ }",36);
    GOOD("byte call","متغير ب: بايت؛ دالة هوية(س: بايت) -> بايت { أرجع س؛ } دالة رئيسية() -> صحيح { إذا (هوية(ب) == ب) { أرجع ١؛ } وإلا { أرجع ٠؛ } }",1);
    GOOD("boolean call","دالة نفي(س: منطقي) -> منطقي { أرجع !س؛ } دالة رئيسية() -> صحيح { إذا (نفي(١ > ٢)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }",1);
    GOOD("void function effect","متغير س: صحيح = ١؛ دالة زد() { س = س + ٤؛ } دالة رئيسية() -> صحيح { زد()؛ أرجع س؛ }",5);
    GOOD("global call initializer","متغير س: صحيح = عدد()؛ دالة عدد() -> صحيح { أرجع ٩؛ } دالة رئيسية() -> صحيح { أرجع س؛ }",9);
    GOOD("default global initialization","متغير س: صحيح؛ دالة رئيسية() -> صحيح { أرجع س؛ }",0);
    GOOD("left to right arguments","متغير س: صحيح = ٠؛ دالة زد() -> صحيح { س = س + ١؛ أرجع س؛ } دالة جمع(أ: صحيح، ب: صحيح) -> صحيح { أرجع أ * ١٠ + ب؛ } دالة رئيسية() -> صحيح { أرجع جمع(زد()، زد())؛ }",12);
    GOOD("short circuit and","دالة رئيسية() -> صحيح { إذا ((١ == ٠) && (١ / ٠ > ٠)) { أرجع ٩؛ } وإلا { أرجع ٣؛ } }",3);
    GOOD("short circuit or","دالة رئيسية() -> صحيح { إذا ((١ == ١) || (١ / ٠ > ٠)) { أرجع ٤؛ } وإلا { أرجع ٩؛ } }",4);
    GOOD("short circuit skips effects","متغير س: صحيح = ٠؛ دالة أثر() -> منطقي { س = س + ١؛ أرجع ١ == ١؛ } دالة رئيسية() -> صحيح { متغير ب: منطقي = (١ == ٠) && أثر()؛ ب = (١ == ١) || أثر()؛ أرجع س؛ }",0);
    GOOD("parameter shadow","دالة د(س: صحيح) -> صحيح { { متغير س: صحيح = ٩؛ } أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(٧)؛ }",7);
    GOOD("block assignment outer","دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ { س = ٨؛ } أرجع س؛ }",8);
    GOOD("negative exit domain","دالة رئيسية() -> صحيح { أرجع -١؛ }",-1);
    GOOD("comparison operators","دالة رئيسية() -> صحيح { إذا ((١ < ٢) && (٢ <= ٢) && (٣ > ٢) && (٣ >= ٣) && (٢ != ١) && (١ == ١)) { أرجع ٦؛ } وإلا { أرجع ٠؛ } }",6);
    GOOD("global initialized by earlier call","متغير أ: صحيح = جهز()؛ متغير ب: صحيح؛ دالة جهز() -> صحيح { ب = ٥؛ أرجع ب؛ } دالة رئيسية() -> صحيح { أرجع أ + ب؛ }",5);
    GOOD("later declaration replaces assigned global","متغير أ: صحيح = جهز()؛ متغير ب: صحيح = ٤؛ دالة جهز() -> صحيح { ب = ٩؛ أرجع ب؛ } دالة رئيسية() -> صحيح { أرجع أ * ١٠ + ب؛ }",94);
    GOOD("else block fuel parity","دالة رئيسية() -> صحيح { متغير س: صحيح = ٠؛ طالما (س < 300000) { إذا (١ < ٠) {} وإلا {} س = س + ١؛ } أرجع س؛ }",300000);
    used=(size_t)snprintf(generated,sizeof generated,"دالة جمع(");
    for(i=1;i<=64;i++) used+=(size_t)snprintf(generated+used,sizeof generated-used,"p%d: صحيح%s",i,i==64?")":"،");
    used+=(size_t)snprintf(generated+used,sizeof generated-used," -> صحيح { أرجع p1 + p64؛ } دالة رئيسية() -> صحيح { أرجع جمع(");
    for(i=1;i<=64;i++) used+=(size_t)snprintf(generated+used,sizeof generated-used,"%d%s",i,i==64?")":"،");
    snprintf(generated+used,sizeof generated-used,"؛ }");
    verify("64 argument boundary",generated,1,65,0);
    /* The valid per-frame binding limit must compose with the valid call-depth limit. */
    used=(size_t)snprintf(generated,sizeof generated,"دالة د(n: صحيح) -> صحيح { ");
    for(i=0;i<130;i++)used+=(size_t)snprintf(generated+used,sizeof generated-used,"متغير v%d: صحيح = ٠؛ ",i);
    snprintf(generated+used,sizeof generated-used,"إذا (n > ٠) { أرجع ١ + د(n - ١)؛ } أرجع ٠؛ } دالة رئيسية() -> صحيح { أرجع د(126)؛ }");
    verify("recursive local capacity",generated,1,126,0);
    for(i=1;i<=50;i++){
        snprintf(name,sizeof name,"arithmetic sample %d",i);
        snprintf(generated,sizeof generated,"دالة رئيسية() -> صحيح { متغير س: صحيح = %d؛ أرجع (س * ٧ - ٣) / ٢ + س %% ٥؛ }",i);
        verify(name,generated,1,(i*7-3)/2+i%5,0);
    }
    BAD("addition overflow","دالة رئيسية() -> صحيح { أرجع 9223372036854775807 + ١؛ }","integer overflow");
    BAD("subtraction overflow","دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) - ١؛ }","integer overflow");
    BAD("multiplication overflow","دالة رئيسية() -> صحيح { أرجع 9223372036854775807 * ٢؛ }","integer overflow");
    BAD("negation overflow","دالة رئيسية() -> صحيح { أرجع -(-9223372036854775807 - ١)؛ }","integer overflow");
    BAD("division overflow","دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) / -١؛ }","integer overflow");
    BAD("remainder overflow","دالة رئيسية() -> صحيح { أرجع (-9223372036854775807 - ١) % -١؛ }","integer overflow");
    BAD("division zero","دالة رئيسية() -> صحيح { أرجع ١ / ٠؛ }","division by zero");
    BAD("remainder zero","دالة رئيسية() -> صحيح { أرجع ١ % ٠؛ }","modulo by zero");
    BAD("negative shift","دالة رئيسية() -> صحيح { أرجع ١ << -١؛ }","invalid shift count");
    BAD("wide shift","دالة رئيسية() -> صحيح { أرجع ١ >> ٦٤؛ }","invalid shift count");
    BAD("shift overflow","دالة رئيسية() -> صحيح { أرجع ١ << ٦٣؛ }","integer overflow");
    BAD("uninitialized local","دالة رئيسية() -> صحيح { متغير س: صحيح؛ أرجع س؛ }","uninitialized variable");
    BAD("uninitialized shadow","دالة رئيسية() -> صحيح { متغير س: صحيح = ٩؛ { متغير س: صحيح؛ أرجع س؛ } }","uninitialized variable");
    BAD("uninitialized conditional","دالة رئيسية() -> صحيح { متغير س: صحيح؛ إذا (١ == ٠) { س = ١؛ } أرجع س؛ }","uninitialized variable");
    BAD("uninitialized global through call","متغير س: صحيح = اقرأ()؛ متغير ص: صحيح = ٩؛ دالة اقرأ() -> صحيح { أرجع ص؛ } دالة رئيسية() -> صحيح { أرجع س؛ }","uninitialized variable");
    BAD("runaway loop","دالة رئيسية() -> صحيح { طالما (١ == ١) {} أرجع ٠؛ }","execution step limit exceeded");
    BAD("runaway recursion","دالة د() -> صحيح { أرجع د()؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }","call depth exceeded");
    /* Expression nesting and function recursion must compose without a host stack crash. */
    used=(size_t)snprintf(generated,sizeof generated,"دالة د() -> صحيح { أرجع ");
    for(i=0;i<110;i++)generated[used++]='-';
    snprintf(generated+used,sizeof generated-used,"د()؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }");
    verify("deep expression recursive guard",generated,0,0,"call depth exceeded");
    BAD("missing main","دالة أخرى() -> صحيح { أرجع ١؛ }",0);
    BAD("main has parameters","دالة رئيسية(س: صحيح) -> صحيح { أرجع س؛ }",0);
    BAD("main wrong return type","دالة رئيسية() -> منطقي { أرجع ١ == ١؛ }",0);
    if(failed){fprintf(stderr,"eval_test: %u failed, %u passed\n",failed,passed);return 1;}
    printf("eval_test: %u scalar execution and failure cases PASS\n",passed);
    return 0;
}
