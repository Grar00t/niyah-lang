#include "../src/check.h"
#include <stdio.h>
#include <string.h>
static AstNode arena[16384];
static int count=0, valid_count=0, invalid_count=0;
static void verify(const char *name,const char *src,int valid,const char *expected){
    Parser p;Checker c;u32 root;
    if(valid)valid_count++;else invalid_count++;
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
 BAD("function used as value","دالة د() { أرجع؛ } دالة رئيسية() -> صحيح { أرجع د؛ }","function name requires a call");
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
 /* Calls are type checked only; recursive fixtures are never executed. */
 GOOD("forward scalar call","دالة رئيسية() -> صحيح { أرجع زد(٤)؛ } دالة زد(س: صحيح) -> صحيح { أرجع س + ١؛ }");
 GOOD("recursive call","دالة عد(س: صحيح) -> صحيح { إذا (س > ٠) { أرجع عد(س - ١)؛ } أرجع ٠؛ }");
 GOOD("mutual recursive calls","دالة أ(س: صحيح) -> صحيح { أرجع ب(س)؛ } دالة ب(س: صحيح) -> صحيح { أرجع أ(س)؛ }");
 GOOD("zero argument call","دالة عدد() -> صحيح { أرجع ٧؛ } دالة رئيسية() -> صحيح { أرجع عدد()؛ }");
 GOOD("byte argument and return","دالة رئيسية(س: بايت) -> بايت { أرجع هوية(س)؛ } دالة هوية(س: بايت) -> بايت { أرجع س؛ }");
 GOOD("boolean argument and return","دالة رئيسية() -> منطقي { أرجع هوية(١ < ٢)؛ } دالة هوية(س: منطقي) -> منطقي { أرجع س؛ }");
 GOOD("void call statement","دالة رئيسية() { عمل()؛ أرجع؛ } دالة عمل() { أرجع؛ }");
 GOOD("global forward function call","متغير س: صحيح = عدد()؛ دالة عدد() -> صحيح { أرجع ٧؛ }");
 GOOD("global prior function call","دالة عدد() -> صحيح { أرجع ٧؛ } ثابت س: صحيح = عدد()؛");
 GOOD("nested argument call","دالة هوية(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع هوية(هوية(٣))؛ }");
 GOOD("call used in condition","دالة موجب(س: صحيح) -> منطقي { أرجع س > ٠؛ } دالة رئيسية() -> صحيح { إذا (موجب(٣)) { أرجع ١؛ } وإلا { أرجع ٠؛ } }");
 GOOD("call assignment and discarded value","دالة عدد() -> صحيح { أرجع ٧؛ } دالة رئيسية() -> صحيح { متغير س: صحيح؛ س = عدد()؛ عدد()؛ أرجع س؛ }");
 GOOD("function visible after shadow scope","دالة عدد() -> صحيح { أرجع ٧؛ } دالة رئيسية() -> صحيح { { متغير عدد: صحيح = ٢؛ } أرجع عدد()؛ }");
 BAD("unknown function","دالة رئيسية() { مفقود()؛ }","undefined identifier");
 BAD("too few arguments","دالة د(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }","argument count mismatch");
 BAD("too many arguments","دالة د(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(١، ٢)؛ }","argument count mismatch");
 BAD("zero arity with argument","دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية() -> صحيح { أرجع د(٢)؛ }","argument count mismatch");
 BAD("boolean argument mismatch","دالة د(س: منطقي) -> منطقي { أرجع س؛ } دالة رئيسية() -> منطقي { أرجع د(١)؛ }","argument type mismatch");
 BAD("byte argument mismatch","دالة د(س: بايت) -> بايت { أرجع س؛ } دالة رئيسية() -> بايت { أرجع د(١)؛ }","argument type mismatch");
 BAD("argument order mismatch","دالة د(س: صحيح، ص: منطقي) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(١ < ٢، ١)؛ }","argument type mismatch");
 BAD("undefined argument preserves error","دالة د(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(مفقود)؛ }","undefined identifier");
 BAD("invalid argument expression preserves error","دالة د(س: صحيح) -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(١ + (١ < ٢))؛ }","integer operands required");
 BAD("local shadows callable","دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية() -> صحيح { متغير د: صحيح = ٢؛ أرجع د()؛ }","call target is not a function");
 BAD("parameter shadows callable","دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية(د: صحيح) -> صحيح { أرجع د()؛ }","call target is not a function");
 BAD("global scalar called","متغير د: صحيح = ٢؛ دالة رئيسية() -> صحيح { أرجع د()؛ }","call target is not a function");
 BAD("nonname call target","دالة رئيسية() -> صحيح { أرجع (١ + ٢)()؛ }","call target must be a function name");
 BAD("call result is not callable","دالة د() -> صحيح { أرجع ١؛ } دالة رئيسية() -> صحيح { أرجع د()()؛ }","call target must be a function name");
 BAD("void call equality","دالة عمل() { أرجع؛ } دالة رئيسية() -> منطقي { أرجع عمل() == عمل()؛ }","void expression cannot be used as a value");
 BAD("void call arithmetic","دالة عمل() { أرجع؛ } دالة رئيسية() -> صحيح { أرجع عمل() + ١؛ }","void expression cannot be used as a value");
 BAD("void call returned from void","دالة عمل() { أرجع؛ } دالة رئيسية() { أرجع عمل()؛ }","void call cannot be returned as a value");
 BAD("void call returned from scalar","دالة عمل() { أرجع؛ } دالة رئيسية() -> صحيح { أرجع عمل()؛ }","void call cannot be returned as a value");
 BAD("call return type mismatch","دالة د() -> منطقي { أرجع ١ < ٢؛ } دالة رئيسية() -> صحيح { أرجع د()؛ }","return type mismatch");
 BAD("forward global remains undefined","متغير س: صحيح = ص؛ متغير ص: صحيح = ١؛","undefined identifier");
 BAD("void call initializer","دالة عمل() { أرجع؛ } دالة رئيسية() { متغير س: صحيح = عمل()؛ }","void expression cannot be used as a value");
 BAD("void call global initializer","متغير س: صحيح = عمل()؛ دالة عمل() { أرجع؛ }","void expression cannot be used as a value");
 BAD("void call assignment","دالة عمل() { أرجع؛ } دالة رئيسية() { متغير س: صحيح؛ س = عمل()؛ }","void expression cannot be used as a value");
 BAD("void call argument","دالة عمل() { أرجع؛ } دالة د(س: صحيح) { أرجع؛ } دالة رئيسية() { د(عمل())؛ }","void expression cannot be used as a value");
 BAD("void call unary","دالة عمل() { أرجع؛ } دالة رئيسية() -> صحيح { أرجع -عمل()؛ }","void expression cannot be used as a value");
 BAD("void call condition","دالة عمل() { أرجع؛ } دالة رئيسية() { إذا (عمل()) {} }","if condition must be boolean");
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
 printf("check_test: %d positive, %d negative, checker reset, return-path and call contracts PASS\n",valid_count,invalid_count);
 return 0;
}
