#ifdef NIYAH_TENSOR_CORE
/* Unified compiler entry: direct syscalls, no external compiler subprocesses. */
#include "codegen.h"
#include "tensor_core.h"
#include <stdint.h>
#include <string.h>
extern long niyah_sys6(long,long,long,long,long,long,long);
#define SYS(n,a,b,c) niyah_sys6((n),(long)(a),(long)(b),(long)(c),0,0,0)
enum { SOURCE_MAX=1<<20, NODES_MAX=4096, OUTPUT_MAX=2<<20, PATH_MAXIMUM=4096 };
static u8 source[SOURCE_MAX+1],output[OUTPUT_MAX];static AstNode nodes[NODES_MAX];
static int eq(const char *a,const char *b){size_t n=strlen(a);return n==strlen(b)&&!memcmp(a,b,n);}
static int write_all(long fd,const void *p,size_t n){
    const u8 *s=p;while(n){long v=SYS(1,fd,s,n);if(v==-4)continue;if(v<=0)return 0;s+=v;n-=(size_t)v;}return 1;
}
static void msg(const char *s){(void)write_all(2,s,strlen(s));}
static void decimal(u32 v){char b[11];u32 i=10;b[i]=0;do{b[--i]=(char)('0'+v%10);v/=10;}while(v);msg(b+i);}
static void diagnostic(const char *prefix,Token at,const char *s){msg(prefix);msg(": ");decimal(at.line);msg(":");decimal(at.col);msg(" ");msg(s?s:"compilation failed");msg("\n");}
/* Before the source is readable, explicit tensor options select its exit ABI. */
static int tensor_requested(int argc,char **argv){
    for(int i=1;i<argc;i++){
        if(eq(argv[i],"--"))break;
        if(eq(argv[i],"-o")){i++;continue;}
        if(eq(argv[i],"--tensor")||eq(argv[i],"--emit-tensor-asm")||eq(argv[i],"--emit-tensor-ir"))return 1;
    }
    return 0;
}
static int usage(int tensor_cli){msg("usage: niyahc [--tensor] [--emit-tensor-asm|--emit-tensor-ir] [-o OUTPUT] [--] FILE\n");return tensor_cli?64:1;}
static u32 append_number(char *s,u32 at,u64 v){char b[21];u32 n=0;do{b[n++]=(char)('0'+v%10);v/=10;}while(v);while(n)s[at++]=b[--n];s[at]=0;return at;}
/* O_EXCL temporary inode -> fsync -> no-replace hard link. Never opens destination. */
static int publish(const char *path,const u8 *bytes,u32 n,int executable){
    char tmp[PATH_MAXIMUM];size_t len=strlen(path);long fd=-1;int made=0,linked=0;
    if(!len||len>sizeof(tmp)-48){msg("output: invalid path length\n");return 0;}
    memcpy(tmp,path,len);memcpy(tmp+len,".tmp.",5);u32 start=append_number(tmp,(u32)len+5,(u64)SYS(39,0,0,0));tmp[start++]='.';
    for(u32 i=0;i<32;i++){
        append_number(tmp,start,i);fd=SYS(2,tmp,0xA00C1,0600); /* WRONLY|CREAT|EXCL|NOFOLLOW|CLOEXEC */
        if(fd>=0){made=1;break;}if(fd!=-17)break;
    }
    if(fd<0){msg("output: exclusive temporary creation failed\n");return 0;}
    int ok=write_all(fd,bytes,n);
    if(ok&&SYS(91,fd,executable?0755:0644,0)<0)ok=0;
    if(ok&&SYS(74,fd,0,0)<0)ok=0;
    if(SYS(3,fd,0,0)<0)ok=0;
    if(ok&&SYS(86,tmp,path,0)==0)linked=1;
    if(made&&SYS(87,tmp,0,0)<0&&linked)msg("warning: executable published; temporary cleanup failed\n");
    if(!linked)msg("output: write or no-replace publication failed\n");
    return linked;
}
int main(int argc,char **argv){
    const char *input=0,*destination="a.out";int options=1,have_output=0,mode=0,force_tensor=0;
    int tensor_cli=tensor_requested(argc,argv);
    static const u64 ignore_sigpipe[4]={1,0,0,0};
    if(niyah_sys6(13,13,(long)ignore_sigpipe,0,8,0,0)<0)return tensor_cli?74:1;
    for(int i=1;i<argc;i++){
        if(options&&eq(argv[i],"--")){options=0;continue;}
        if(options&&eq(argv[i],"--tensor")){if(force_tensor)return usage(tensor_cli);force_tensor=1;continue;}
        if(options&&eq(argv[i],"-o")){if(have_output||++i>=argc||!argv[i][0])return usage(tensor_cli);destination=argv[i];have_output=1;continue;}
        if(options&&(eq(argv[i],"--emit-tensor-asm")||eq(argv[i],"--emit-tensor-ir"))){if(mode)return usage(tensor_cli);mode=eq(argv[i],"--emit-tensor-asm")?1:2;continue;}
        if((options&&argv[i][0]=='-')||input||!argv[i][0])return usage(tensor_cli);
        input=argv[i];
    }
    if(!input)return usage(tensor_cli);
    long fd=SYS(2,input,0x80000,0);if(fd<0){msg("input: read failed\n");return tensor_cli?66:1;}
    u32 n=0;int bad=0;
    while(n<sizeof(source)){
        long v=SYS(0,fd,source+n,sizeof(source)-n);if(v==-4)continue;if(v<0){bad=1;break;}if(!v)break;n+=(u32)v;
    }
    if(SYS(3,fd,0,0)<0)bad=1;
    if(bad){msg("input: read failed\n");return tensor_cli?66:1;}
    if(n>SOURCE_MAX){msg("input: source exceeds 1 MiB capacity\n");return tensor_cli?77:1;}
    Parser p;CodegenResult result;parser_init(&p,nodes,NODES_MAX,source,n);u32 root=parser_parse(&p);
    if(!root){
        diagnostic("syntax",p.error_at,p.error);
        if(!(tensor_cli||p.has_tensor))return 2;
        return p.error&&(eq(p.error,"AST arena capacity exceeded")||eq(p.error,"nesting depth exceeded"))?77:65;
    }
    int is_tensor=tensor_cli||p.has_tensor;
    if(tensor_cli&&!p.has_tensor){msg("tensor: expected tensor AST\n");return 65;}
    u32 length=0;
    if(mode){
        if(!p.has_tensor){msg("tensor: expected tensor AST\n");return 65;}
        if(!tensor_prepare(&p,root,&result)){diagnostic("tensor",result.at,result.error);return result.exit_code?result.exit_code:65;}
        length=mode==1?tensor_export_asm(output,sizeof(output)):tensor_export_ir(output,sizeof(output));
        if(!length){msg("tensor: emission capacity exceeded\n");return result.exit_code?result.exit_code:77;}
    } else {
        if(!codegen_elf64(&p,root,output,sizeof(output),&result)){
            diagnostic(p.has_tensor?"tensor":result.semantic_error?"semantic":"codegen",result.at,result.error);
            return is_tensor?(result.exit_code?result.exit_code:65):(result.semantic_error?3:4);
        }
        length=result.size;
    }
    if(mode&&!have_output)return write_all(1,output,length)?0:74;
    return publish(destination,output,length,!mode)?0:(is_tensor?74:1);
}

#else
/* Hosted scalar-only driver retained for existing mutation/sanitizer harnesses.
 * The production niyahc target always defines NIYAH_TENSOR_CORE. */
#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { SOURCE_MAX=1<<20, NODES_MAX=4096, OUTPUT_MAX=1<<20 };
static u8 source[SOURCE_MAX+1];
static u8 output[OUTPUT_MAX];
static AstNode nodes[NODES_MAX];

/* Publish a complete executable by a no-replace hard link in the same
 * directory. Existing files, directories and symlinks are never opened for
 * writing. The temporary inode is mode 0600 while being written. */
static int publish(const char *path,const u8 *data,u32 size) {
    size_t length=strlen(path),written=0;
    char *temporary;
    int fd=-1,saved=0,linked=0,created=0;
    if(length>SIZE_MAX-12){errno=ENAMETOOLONG;return 0;}
    temporary=(char *)malloc(length+12);
    if(!temporary){errno=ENOMEM;return 0;}
    memcpy(temporary,path,length);
    memcpy(temporary+length,".tmp.XXXXXX",12);
    fd=mkstemp(temporary);
    if(fd<0){saved=errno;goto done;}
    created=1;
    while(written<size) {
        ssize_t n=write(fd,data+written,(size_t)size-written);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){saved=n<0?errno:EIO;goto done;}
        written+=(size_t)n;
    }
    if(fchmod(fd,0755)!=0 || fsync(fd)!=0){saved=errno;goto done;}
    if(close(fd)!=0){saved=errno;fd=-1;goto done;}
    fd=-1;
    if(link(temporary,path)!=0){saved=errno;goto done;}
    linked=1;
done:
    if(fd>=0 && close(fd)!=0 && !saved)saved=errno;
    /* Only remove the unique file created by this invocation. */
    if(created && unlink(temporary)!=0 && linked) {
        fprintf(stderr,"warning: executable published; temporary cleanup failed: %s\n",strerror(errno));
    }
    free(temporary);
    if(!linked)errno=saved?saved:EIO;
    return linked;
}
static int usage(void) {
    fputs("usage: niyahc [-o OUTPUT] [--] FILE\n",stderr);return 1;
}
int main(int argc,char **argv) {
    const char *input=NULL,*destination="a.out";
    int i,options=1,have_output=0,read_failed;
    FILE *f;
    size_t length;
    Parser parser;
    CodegenResult result;
    u32 root;
    for(i=1;i<argc;i++) {
        if(options && strcmp(argv[i],"--")==0){options=0;continue;}
        if(options && strcmp(argv[i],"-o")==0) {
            if(have_output || ++i>=argc || !argv[i][0])return usage();
            destination=argv[i];have_output=1;continue;
        }
        if((options && argv[i][0]=='-') || input || !argv[i][0])return usage();
        input=argv[i];
    }
    if(!input)return usage();
    f=fopen(input,"rb");
    if(!f){perror("input");return 1;}
    length=fread(source,1,SOURCE_MAX+1,f);read_failed=ferror(f);
    if(fclose(f)!=0)read_failed=1;
    if(read_failed || length>SOURCE_MAX) {
        fputs("error: source read failed or exceeds 1 MiB\n",stderr);return 1;
    }
    parser_init(&parser,nodes,NODES_MAX,source,(u64)length);
    root=parser_parse(&parser);
    if(!root) {
        fprintf(stderr,"syntax: %u:%u %s\n",parser.error_at.line,parser.error_at.col,
                parser.error?parser.error:"invalid program");return 2;
    }
    if(!codegen_elf64(&parser,root,output,OUTPUT_MAX,&result)) {
        fprintf(stderr,"%s: %u:%u %s\n",result.semantic_error?"semantic":"codegen",
                result.at.line,result.at.col,result.error?result.error:"compilation failed");
        return result.semantic_error?3:4;
    }
    if(!publish(destination,output,result.size)){perror("output");return 1;}
    return 0;
}

#endif
