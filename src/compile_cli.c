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
