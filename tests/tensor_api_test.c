#include "tensor_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static AstNode nodes[4096],saved[4096];static unsigned char output[65536],ir[131072];
static const unsigned char source[]="reserve 1; input_a a; input_b b; gemm c = a * b; emit c;";
static void w(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
int main(void){
    unsigned tests=0;
    assert(tensor_export_ir(ir,sizeof(ir))==0);assert(tensor_export_asm(ir,sizeof(ir))==0);tests+=2;
    Parser p;CodegenResult r;parser_init(&p,nodes,4096,source,sizeof(source)-1);unsigned root=parser_parse(&p);
    assert(root&&p.has_tensor);assert(tensor_prepare(&p,root,&r));tests++;
    unsigned annotations=0;for(unsigned i=1;i<p.count;i++)if(nodes[i].kind==A_TENSOR_OP&&nodes[i].tensor.opcode<=TI_GEMM){assert(nodes[i].tensor.metadata==++annotations);}
    assert(annotations==3);tests++;
    unsigned n=tensor_export_ir(ir,sizeof(ir));assert(n==32+3*1024+4*16+sizeof(source)-1);tests++;
    memset(output,0xA5,sizeof(output));assert(!tensor_codegen(&p,root,output,11887,&r)&&r.exit_code==65);
    for(unsigned i=0;i<sizeof(output);i++)assert(output[i]==0xA5);tests++;
    assert(tensor_codegen(&p,root,output,11888,&r)&&r.size==11888);assert(!memcmp(output,"\177ELF",4));
    for(unsigned i=11888;i<sizeof(output);i++)assert(output[i]==0xA5);tests++;
    memcpy(saved,nodes,sizeof(nodes));
    for(unsigned kind=0;kind<8;kind++){
        memcpy(nodes,saved,sizeof(nodes));Parser bad=p;
        if(kind==0)nodes[root].first=bad.count+4;
        if(kind==1)nodes[root].last=bad.count+4;
        if(kind==2)nodes[nodes[root].first].next=nodes[root].first;
        if(kind==3)bad.count=bad.capacity+1;
        if(kind==4)bad.source=0;
        if(kind==5){bad.count++;nodes[bad.count-1]=(AstNode){0};nodes[bad.count-1].kind=A_INT;}
        if(kind==6)nodes[nodes[root].first].tensor.opcode=99;
        if(kind==7){unsigned q=nodes[nodes[root].first].next;unsigned name=nodes[q].first;nodes[name].tok.start=(const unsigned char *)1;}
        assert(!tensor_prepare(&bad,root,&r)&&r.exit_code==65);assert(tensor_export_ir(ir,sizeof(ir))==0);tests++;
    }
    memcpy(nodes,saved,sizeof(nodes));assert(tensor_prepare(&p,root,&r));tests++;
    unsigned char input[40]={0},dest[72],before[72];memcpy(input,"NTILE001",8);input[8]=3;input[9]=1;
    w(input+16,0);w(input+20,3);w(input+24,1);w(input+28,3);w(input+32,2);w(input+36,4);
    memset(dest,0xA5,sizeof(dest));assert(tensor_allocate(input,40,dest,11)==0);assert(!memcmp(dest,"TMM1\003\0\0\0\1\2\3",11));for(unsigned i=11;i<72;i++)assert(dest[i]==0xA5);tests++;
    for(unsigned kind=0;kind<8;kind++){
        memset(dest,0xA5,sizeof(dest));memcpy(before,dest,72);input[9]=1;w(input+36,4);input[10]=0;input[8]=3;
        int code;
        if(kind==0)code=tensor_allocate(input,39,dest,72);
        else if(kind==1)code=tensor_allocate(input,40,dest,10);
        else if(kind==2){input[10]=1;code=tensor_allocate(input,40,dest,72);}
        else if(kind==3){input[8]=0;code=tensor_allocate(input,40,dest,72);}
        else if(kind==4){input[9]=255;w(input+36,0);code=tensor_allocate(input,40,dest,72);}
        else if(kind==5)code=tensor_allocate(0,40,dest,72);
        else if(kind==6)code=tensor_allocate(input,40,0,72);
        else {input[9]=255;code=tensor_allocate(input,40,dest,72);}
        assert(code==(kind==7?77:65));assert(!memcmp(dest,before,72));tests++;
    }
    const unsigned char native[]="\xd8\xaf\xd8\xa7\xd9\x84\xd8\xa9 \xd8\xb1\xd8\xa6\xd9\x8a\xd8\xb3\xd9\x8a\xd8\xa9() -> \xd8\xb5\xd8\xad\xd9\x8a\xd8\xad { tensor_reserve(1); \xd9\x85\xd8\xaa\xd8\xba\xd9\x8a\xd8\xb1 a: i8x16x64 = tensor_input_a(); \xd9\x85\xd8\xaa\xd8\xba\xd9\x8a\xd8\xb1 b: i8x64x16 = tensor_input_b(); \xd9\x85\xd8\xaa\xd8\xba\xd9\x8a\xd8\xb1 c: i32x16x16 = tensor_gemm(a,b); tensor_emit(c); \xd8\xa3\xd8\xb1\xd8\xac\xd8\xb9 0; }";
    unsigned char previous[11888];memcpy(previous,output,11888);parser_init(&p,nodes,4096,native,sizeof(native)-1);root=parser_parse(&p);assert(root&&p.has_tensor);assert(tensor_codegen(&p,root,output,sizeof(output),&r));assert(r.size==11888&&!memcmp(output,previous,11888));tests++;
    printf("AST_API_PASS cases=%u ASAN_UBSAN=enabled\n",tests);return 0;
}
