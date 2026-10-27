#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { SOURCE_MAX=1<<20,NODES_MAX=4096 };
static u8 source[SOURCE_MAX+1];
static AstNode nodes[NODES_MAX];
int main(int argc,char **argv){
    FILE *f;size_t n;Parser p;EvalResult result;u32 root;
    if(argc!=2){fprintf(stderr,"usage: niyahrun FILE\n");return 1;}
    f=fopen(argv[1],"rb");if(!f){perror("open");return 1;}
    n=fread(source,1,SOURCE_MAX+1,f);
    if(ferror(f)||fclose(f)||n>SOURCE_MAX){fprintf(stderr,"error: read failed or input too large\n");return 1;}
    parser_init(&p,nodes,NODES_MAX,source,(u64)n);
    root=parser_parse(&p);
    if(!root){fprintf(stderr,"syntax: %u:%u %s\n",p.error_at.line,p.error_at.col,p.error?p.error:"unknown");return 2;}
    if(!eval_constant_main(&p,root,&result)){fprintf(stderr,"evaluation: %u:%u %s\n",result.at.line,result.at.col,result.error?result.error:"unsupported program");return 3;}
    if (printf("RESULT=%ld\n",(long)result.value) < 0 ||
        fflush(stdout) == EOF) {
        fputs("error: failed to write result\n", stderr);
        return 4;
    }
    return 0;
}
