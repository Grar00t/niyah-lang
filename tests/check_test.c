#include "../src/check.h"
#include <stdio.h>
#include <string.h>
static AstNode arena[16384];
static int count=0;
static void verify(const char *name,const char *src,int valid,const char *expected){
    Parser p;Checker c;u32 root;
    parser_init(&p,arena,16384,(const u8 *)src,(u64)strlen(src));
    root=parser_parse(&p);
    if(!root){fprintf(stderr,"syntax fixture error %s: %s\n",name,p.error);count++;return;}
    checker_init(&c,&p);
    if(checker_run(&c,root)!=valid||(!valid && (!c.message || (expected && strcmp(c.message,expected)!=0)))){
        fprintf(stderr,"FAIL %s: errors=%u actual=%s wanted=%s\n",name,c.errors,c.message?c.message:"OK",expected?expected:"OK");count++;
    }
}
#define GOOD(name,s) verify(name,s,1,0)
#define BAD(name,s,why) verify(name,s,0,why)
int main(void){
 GOOD("scalar global","متغير س: صحيح = ١؛ ثابت ص: صحيح = ٢؛");
 GOOD("param and assignment","دالة د(س: صحيح) -> صحيح { متغير ص: صحيح = س + ١؛ ص = ص + ١؛ أرجع ص؛ }");
 GOOD("scope shadow","دالة د() -> صحيح { متغير س: صحيح = ١؛ { متغير س: صحيح = ٢؛ } أرجع س؛ }");
 GOOD("bool comparison","دالة د(س: صحيح) -> منطقي { أرجع س > ١؛ }");
 GOOD("byte identifier","دالة د(س: بايت) -> بايت { أرجع س؛ }");
 GOOD("global lookup","متغير س: صحيح = ١؛ دالة د() -> صحيح { أرجع س؛ }");
 GOOD("void function","دالة د() { أرجع؛ }");
 GOOD("two distinct functions","دالة أ() { أرجع؛ } دالة ب() { أرجع؛ }");
 GOOD("if else both return","دالة د(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع ١؛ } وإلا { أرجع ٢؛ } }");
 GOOD("nested if branches","دالة د(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع ١؛ } وإلا إذا (س < ٠) { أرجع ٢؛ } وإلا { أرجع ٣؛ } }");
 GOOD("return after if","دالة د(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع ١؛ } أرجع ٢؛ }");
 BAD("missing return empty","دالة د() -> صحيح { }","not all paths return a value");
 BAD("missing return only if","دالة د(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع ١؛ } }","not all paths return a value");
 BAD("missing return else block","دالة د(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع ١؛ } وإلا {} }","not all paths return a value");
 BAD("while not assured","دالة د(س: صحيح) -> صحيح { طالما (س > ٠) { أرجع ١؛ } }","not all paths return a value");
 BAD("duplicate functions","دالة د() { أرجع؛ } دالة د() { أرجع؛ }","duplicate symbol in scope");
 BAD("function then variable","دالة د() { أرجع؛ } متغير د: صحيح = ١؛","duplicate symbol in scope");
 BAD("variable then function","متغير د: صحيح = ١؛ دالة د() { أرجع؛ }","duplicate symbol in scope");
 BAD("function used as value","دالة د() { أرجع؛ } دالة رئيسية() -> صحيح { أرجع د؛ }","function value requires call typing");
 BAD("assignment to function","دالة د() { أرجع؛ } دالة رئيسية() { د = ١؛ }","cannot assign to function");
 BAD("undefined","دالة د() -> صحيح { أرجع غير_موجود؛ }","undefined identifier");
 BAD("duplicate","دالة د() { متغير س: صحيح؛ متغير س: صحيح؛ }","duplicate symbol in scope");
 BAD("duplicate global","متغير س: صحيح؛ ثابت س: صحيح = ١؛","duplicate symbol in scope");
 BAD("constant assignment","دالة د() { ثابت س: صحيح = ١؛ س = ٢؛ }","cannot assign to constant");
 BAD("literal lvalue","دالة د() { ١ = ٢؛ }","invalid assignment target");
 BAD("bad type","متغير س: غيرمعروف؛","unknown or unsupported type");
 BAD("implicit numeric","متغير س: بايت = ١؛","initializer type mismatch");
 BAD("implicit bool","دالة د() { متغير س: منطقي = ١؛ }","initializer type mismatch");
 BAD("bad return","دالة د() -> بايت { أرجع ١؛ }","return type mismatch");
 BAD("bad condition","دالة د() { إذا (١) {} }","if condition must be boolean");
 BAD("wrong compare","دالة د(س: بايت) -> منطقي { أرجع س > ١؛ }","integer comparison required");
 BAD("wrong bool","دالة د(س: صحيح) -> منطقي { أرجع س && س؛ }","boolean operands required");
 BAD("out of scope","دالة د() { { متغير س: صحيح؛ } س = ١؛ }","undefined identifier");
 /* A Checker is reusable after explicitly resetting its live metadata. */
 {
   static const char invalid[]="دالة د() -> صحيح { أرجع مجهول؛ }";
   static const char valid[]="دالة د() -> صحيح { أرجع ١؛ }";
   Parser p;
   Checker c;
   u32 root;
   parser_init(&p,arena,16384,(const u8 *)invalid,sizeof(invalid)-1);
   root=parser_parse(&p);
   if(!root){count++;}else {
     checker_init(&c,&p);
     if(checker_run(&c,root)||!c.errors)count++;
   }
   parser_init(&p,arena,16384,(const u8 *)valid,sizeof(valid)-1);
   root=parser_parse(&p);
   checker_init(&c,&p);
   if(!root||!checker_run(&c,root)||c.errors||c.count!=1||c.scope)count++;
 }
 if(count)return 1;
 puts("check_test: 11 positive, 22 negative, checker reset and return-path contracts PASS");
 return 0;
}
