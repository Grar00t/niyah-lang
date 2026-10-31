#define _POSIX_C_SOURCE 200809L
#include "../src/parser.h"
#include "../src/check.h"
#include "../src/eval.h"
#include "../src/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static AstNode arena[CODEGEN_MAX_NODES];
static char source[1 << 20];
static u8 elf[1 << 20];
static size_t used;
static unsigned passed,failed,native_runs,cli_runs;
static int add(const char *format,...){
    va_list args;int n;
    va_start(args,format);n=vsnprintf(source+used,sizeof source-used,format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof source-used){fputs("resource fixture capacity exceeded\n",stderr);exit(2);}
    used+=(size_t)n;return 1;
}
static void reset(void){used=0;source[0]=0;}
static void globals(u32 count){
    u32 i;
    for(i=0;i<count;i++)add("متغير g%u: صحيح = %u؛ ",i,i);
}
/* Exercise the generated program too; fixtures use exact observable return
 * values 0..127 or a documented runtime trap, never an ambiguous large result. */
static int execute(u32 size,int expected,const char *message){
    char path[]="/tmp/niyah-resource-test-XXXXXX",output[256],diagnostic[128];
    int fd,pipefd[2],status=0;pid_t pid;size_t done=0,total=0;ssize_t n;
    fd=mkstemp(path);if(fd<0)return 0;
    while(done<size){
        n=write(fd,elf+done,size-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){close(fd);unlink(path);return 0;}done+=(size_t)n;
    }
    if(fchmod(fd,0700)!=0){close(fd);unlink(path);return 0;}
    if(close(fd)!=0 || pipe(pipefd)!=0){unlink(path);return 0;}
    pid=fork();
    if(pid<0){close(pipefd[0]);close(pipefd[1]);unlink(path);return 0;}
    if(pid==0){
        close(pipefd[0]);
        if(dup2(pipefd[1],1)<0 || dup2(pipefd[1],2)<0)_exit(127);
        close(pipefd[1]);alarm(5);execl(path,path,(char *)0);_exit(127);
    }
    close(pipefd[1]);
    while((n=read(pipefd[0],output+total,sizeof output-1-total))>0){
        total+=(size_t)n;if(total==sizeof output-1)break;
    }
    close(pipefd[0]);
    while(waitpid(pid,&status,0)<0){if(errno!=EINTR){unlink(path);return 0;}}
    unlink(path);output[total]=0;native_runs++;
    if(message){
        int length=snprintf(diagnostic,sizeof diagnostic,"runtime: %s\n",message);
        if(length<0 || (size_t)length>=sizeof diagnostic || strcmp(output,diagnostic)!=0)return 0;
    }else if(total!=0)return 0;
    return WIFEXITED(status) && WEXITSTATUS(status)==expected;
}
static u32 parse(Parser *p,u32 capacity){
    parser_init(p,arena,capacity,(const u8 *)source,(u64)used);return parser_parse(p);
}
/* want_check=0 is a semantic resource rejection. native_error is an explicit
 * backend bound; those limits must not be imposed on the source language. */
static void verify(const char *name,int want_check,i64 want,const char *eval_error,
                   const char *native_error,int native_exit){
    Parser p;Checker checker;EvalResult er;CodegenResult cr;u32 root;int checked,ok;
    root=parse(&p,CODEGEN_MAX_NODES);
    if(!root){fprintf(stderr,"FAIL %s parse: %s\n",name,p.error);failed++;return;}
    checker_init(&checker,&p);checked=checker_run(&checker,root);
    if(checked!=want_check || checker.count>CHECK_MAX_SYMBOLS){
        fprintf(stderr,"FAIL %s checker: %s\n",name,checker.message?checker.message:"accepted");failed++;return;
    }
    if(!want_check){
        ok=codegen_elf64(&p,root,elf,sizeof elf,&cr);
        if(!checker.message || !strstr(checker.message,"symbol capacity exceeded") ||
           ok || !cr.semantic_error || cr.size){fprintf(stderr,"FAIL %s semantic rejection\n",name);failed++;return;}
        passed++;return;
    }
    ok=eval_main(&p,root,&er);
    if((eval_error && (ok || !er.error || !strstr(er.error,eval_error))) ||
       (!eval_error && (!ok || er.value!=want))){
        fprintf(stderr,"FAIL %s eval: %s value=%ld\n",name,er.error?er.error:"OK",er.value);failed++;return;
    }
    ok=codegen_elf64(&p,root,elf,sizeof elf,&cr);
    if(native_error){
        if(ok || cr.semantic_error || !cr.error || !strstr(cr.error,native_error) || cr.size){
            fprintf(stderr,"FAIL %s backend bound: %s\n",name,cr.error?cr.error:"accepted");failed++;return;
        }
    }else if(!ok || !execute(cr.size,native_exit,eval_error)){
        fprintf(stderr,"FAIL %s native execution: %s\n",name,cr.error?cr.error:"wrong exit/output");failed++;return;
    }
    passed++;
}
static int execute_cli_capacity(void){
    const char *compiler=getenv("NIYAHC");
    char path[]="/tmp/niyah-resource-cli-XXXXXX",destination[96],output[512];
    struct stat st;
    int fd,pipefd[2],status=0,artifact;pid_t pid;size_t done=0,total=0;ssize_t n;
    if(!compiler || !*compiler)compiler="./niyahc";
    fd=mkstemp(path);if(fd<0)return 0;
    snprintf(destination,sizeof destination,"%s.elf",path);
    if(lstat(destination,&st)==0 || errno!=ENOENT){close(fd);unlink(path);return 0;}
    while(done<used){
        n=write(fd,source+done,used-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){close(fd);unlink(path);return 0;}done+=(size_t)n;
    }
    if(close(fd)!=0 || pipe(pipefd)!=0){unlink(path);return 0;}
    pid=fork();
    if(pid<0){close(pipefd[0]);close(pipefd[1]);unlink(path);return 0;}
    if(pid==0){
        close(pipefd[0]);
        if(dup2(pipefd[1],1)<0 || dup2(pipefd[1],2)<0)_exit(127);
        close(pipefd[1]);alarm(10);
        execl(compiler,compiler,"-o",destination,path,(char *)0);_exit(127);
    }
    close(pipefd[1]);
    while((n=read(pipefd[0],output+total,sizeof output-1-total))>0){
        total+=(size_t)n;if(total==sizeof output-1)break;
    }
    close(pipefd[0]);
    while(waitpid(pid,&status,0)<0){if(errno!=EINTR){unlink(path);return 0;}}
    output[total]=0;cli_runs++;
    artifact=lstat(destination,&st)==0;
    if(artifact)unlink(destination);
    unlink(path);
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=2 || artifact ||
       strncmp(output,"syntax: ",8)!=0 || !strstr(output,"AST arena capacity exceeded\n")){
        fprintf(stderr,"CLI capacity failure: status=%d artifact=%d output=%s\n",status,artifact,output);return 0;
    }
    return 1;
}
static void cli_capacity(const char *name){
    Parser p;
    if(parse(&p,4096) || !p.error || !strstr(p.error,"AST arena capacity exceeded")){
        fprintf(stderr,"FAIL %s parser API arena=4096 bound\n",name);failed++;
    }else if(!execute_cli_capacity()){
        fprintf(stderr,"FAIL %s actual compiler CLI capacity contract\n",name);failed++;
    }else passed++;
}
static u32 minimum(u32 a,u32 b){return a<b?a:b;}
static void aggregate(u32 extra){
    u32 i,nargs=minimum(EVAL_MAX_ARGS,CODEGEN_MAX_ARGS);
    u32 nlocals=CHECK_MAX_SYMBOLS-512-2-nargs+extra;
    reset();globals(512);add("دالة عمل(");
    for(i=1;i<=nargs;i++)add("%sp%u: صحيح",i>1?",":"",i);
    add(") -> صحيح { ");
    for(i=0;i<nlocals;i++)add("متغير v%u: صحيح = ٢؛ ",i);
    add("أرجع p1 + p%u + v%u؛ } دالة رئيسية() -> صحيح { أرجع عمل(",nargs,nlocals-1);
    for(i=1;i<=nargs;i++)add("%s%u",i>1?",":"",i);
    add(")؛ }");
}
int main(void){
    u32 i,nargs=minimum(EVAL_MAX_ARGS,CODEGEN_MAX_ARGS);
    u32 calls=minimum(EVAL_MAX_CALLS,CODEGEN_CALL_LIMIT);
    reset();globals(CHECK_MAX_SYMBOLS-1);
    add("دالة رئيسية() -> صحيح { إذا (g%u == %u) { أرجع ١؛ } أرجع ٠؛ }",CHECK_MAX_SYMBOLS-2,CHECK_MAX_SYMBOLS-2);
    verify("globals plus main at aggregate bound",1,1,0,0,1);cli_capacity("large globals");
    reset();globals(CHECK_MAX_SYMBOLS);add("دالة رئيسية() -> صحيح { أرجع ١؛ }");
    verify("globals plus main exceed aggregate bound",0,0,0,0,0);
    aggregate(0);verify("globals parameters locals exact aggregate",1,(i64)nargs+3,0,0,(int)nargs+3);cli_capacity("large mixed bindings");
    aggregate(1);verify("globals parameters locals exceed aggregate",0,0,0,0,0);
    reset();add("دالة رئيسية() -> صحيح { ");
    for(i=0;i<CODEGEN_MAX_SLOTS;i++)add("{ متغير س: صحيح = ١؛ }");
    add("أرجع ٧؛ }");verify("disjoint scope locals at native frame bound",1,7,0,0,7);cli_capacity("large disjoint scopes");
    reset();add("دالة رئيسية() -> صحيح { ");
    for(i=0;i<=CODEGEN_MAX_SLOTS;i++)add("{ متغير س: صحيح = ١؛ }");
    add("أرجع ٧؛ }");verify("native frame bound remains backend specific",1,7,0,"native local slot limit exceeded",0);
    reset();add("دالة د(n: صحيح) -> صحيح { ");
    for(i=0;i<CHECK_MAX_SYMBOLS-3;i++)add("متغير v%u: صحيح = ٠؛ ",i);
    add("إذا (n > ٠) { أرجع ١ + د(n - ١)؛ } أرجع ٠؛ } دالة رئيسية() -> صحيح { أرجع د(%u)؛ }",calls-2);
    verify("maximum local bindings across recursive frames",1,(i64)calls-2,0,0,(int)calls-2);cli_capacity("large recursive frame");
    reset();add("متغير س: صحيح = ٩؛ دالة د(س: صحيح) -> صحيح { متغير س: صحيح = س + ١؛ أرجع س؛ } دالة رئيسية() -> صحيح { أرجع د(١) * ١٠ + س؛ }");
    verify("global parameter local shadow and initializer",1,29,0,0,29);
    reset();add("دالة رئيسية() -> صحيح { متغير i: صحيح = ٠؛ طالما (i < ٢) { متغير س: صحيح؛ إذا (i == ٠) { س = ٧؛ } وإلا { أرجع س؛ } i = i + ١؛ } أرجع ٠؛ }");
    verify("uninitialized flag resets each loop",1,0,"uninitialized variable",0,125);
    reset();add("دالة د(n: صحيح) -> صحيح { متغير س: صحيح؛ إذا (n == ٠) { س = ٧؛ } أرجع س؛ } دالة رئيسية() -> صحيح { د(٠)؛ أرجع د(١)؛ }");
    verify("uninitialized flag resets each invocation",1,0,"uninitialized variable",0,125);
    reset();add("متغير س: صحيح = ٧؛ دالة د() -> صحيح { أرجع س؛ } دالة رئيسية() -> صحيح { متغير س: صحيح = ٩٩؛ أرجع د()؛ }");
    verify("callee cannot see caller local",1,7,0,0,7);
    reset();add("دالة د() -> صحيح { { متغير س: صحيح = ٩٩؛ أرجع س؛ } } دالة رئيسية() -> صحيح { متغير س: صحيح = ١؛ د()؛ أرجع س؛ }");
    verify("return unwinds nested scopes",1,1,0,0,1);
    if(failed)return 1;
    printf("resource_limits_test: %u cases, %u native executions, %u CLI rejections PASS (symbols=%u, native slots=%u, calls=%u, CLI arena=4096, API arena=%u)\n",
           passed,native_runs,cli_runs,(unsigned)CHECK_MAX_SYMBOLS,(unsigned)CODEGEN_MAX_SLOTS,calls,(unsigned)CODEGEN_MAX_NODES);
    return 0;
}
