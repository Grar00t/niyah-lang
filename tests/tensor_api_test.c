#include "tensor_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static AstNode nodes[4096],saved[4096];static unsigned char output[65536],ir[131072];
static unsigned char previous[sizeof(output)];
static const unsigned char source[]="reserve 1; input_a a; input_b b; gemm c = a * b; emit c;";
static void w(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static unsigned r32(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
int main(void){
    unsigned tests=0;
    assert(tensor_export_ir(ir,sizeof(ir))==0);assert(tensor_export_asm(ir,sizeof(ir))==0);tests+=2;
    Parser p;CodegenResult r;parser_init(&p,nodes,4096,source,sizeof(source)-1);unsigned root=parser_parse(&p);
    assert(root&&p.has_tensor);assert(tensor_prepare(&p,root,&r));tests++;
    assert(!tensor_prepare(&p,root,0));
    assert(tensor_export_ir(ir,sizeof(ir))==0&&tensor_export_asm(ir,sizeof(ir))==0);tests++;
    assert(tensor_prepare(&p,root,&r));
    unsigned annotations=0;for(unsigned i=1;i<p.count;i++)if(nodes[i].kind==A_TENSOR_OP&&nodes[i].tensor.opcode<=TI_GEMM){assert(nodes[i].tensor.metadata==++annotations);}
    assert(annotations==3);tests++;
    for(unsigned i=1;i<p.count;i++)if(nodes[i].kind==A_TENSOR_OP){
        Node_TensorOp *x=&nodes[i].tensor;
        if(x->opcode==TI_RESERVE){
            assert(x->value_id==NIYAH_TENSOR_NO_VALUE&&x->value_type==0&&x->metadata==0);
            assert(x->operand_ids[0]==NIYAH_TENSOR_NO_VALUE&&x->operand_ids[1]==NIYAH_TENSOR_NO_VALUE);
        } else {
            assert(x->value_id<3&&x->value_type==x->value_id+1&&x->metadata==x->value_id+1);
            assert(x->tmm==(int)x->value_id+1);
            if(x->opcode==TI_GEMM)assert(x->value_id==2&&x->operand_ids[0]==0&&x->operand_ids[1]==1);
            else if(x->opcode==TI_EMIT)assert(x->value_id==2&&x->operand_ids[0]==2&&x->operand_ids[1]==NIYAH_TENSOR_NO_VALUE);
            else assert(x->operand_ids[0]==NIYAH_TENSOR_NO_VALUE&&x->operand_ids[1]==NIYAH_TENSOR_NO_VALUE);
        }
        tests++;
    }
    unsigned n=tensor_export_ir(ir,sizeof(ir));assert(n==32+3*1024+4*16+sizeof(source)-1);tests++;
    assert(tensor_codegen(&p,root,output,sizeof(output),&r));
    unsigned baseline_size=r.size;assert(baseline_size>0&&baseline_size<=sizeof(output));
    memcpy(previous,output,baseline_size);tests++;
    memset(output,0xA5,sizeof(output));assert(!tensor_codegen(&p,root,output,baseline_size-1,&r)&&r.exit_code==74);
    for(unsigned i=0;i<sizeof(output);i++)assert(output[i]==0xA5);
    tests++;
    assert(tensor_codegen(&p,root,output,baseline_size,&r)&&r.size==baseline_size);assert(!memcmp(output,"\177ELF",4));
    for(unsigned i=baseline_size;i<sizeof(output);i++)assert(output[i]==0xA5);
    assert(!memcmp(output,previous,baseline_size));tests++;
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
    parser_init(&p,nodes,4096,native,sizeof(native)-1);root=parser_parse(&p);assert(root&&p.has_tensor);assert(tensor_codegen(&p,root,output,sizeof(output),&r));assert(r.size==baseline_size&&!memcmp(output,previous,baseline_size));tests++;
    /* Every token consumer must reject malformed spans before dereferencing. */
    memcpy(saved,nodes,sizeof(nodes));
    unsigned function=nodes[root].first,intrinsic=0,gemm=0,definition=0;
    for(unsigned i=1;i<p.count;i++){
        if(nodes[i].kind==A_TENSOR_OP&&nodes[i].tensor.opcode==TI_INPUT_A)intrinsic=i;
        if(nodes[i].kind==A_TENSOR_OP&&nodes[i].tensor.opcode==TI_GEMM)gemm=i;
        if(nodes[i].kind==A_VAR&&!definition)definition=i;
    }
    assert(intrinsic&&gemm&&definition);
    unsigned targets[]={intrinsic,nodes[intrinsic].first,function,nodes[function].first,
                        nodes[nodes[gemm].first].next,definition};
    for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++)for(unsigned variant=0;variant<5;variant++){
        memcpy(nodes,saved,sizeof(nodes));Token *tok=&nodes[targets[i]].tok;
        if(variant==0)tok->start=(const unsigned char *)1;
        else if(variant==1)tok->start=p.source+p.source_len;
        else if(variant==2)tok->len=(unsigned)p.source_len+1;
        else if(variant==3){tok->start=0;tok->len=0;}
        else {tok->start=p.source;tok->len=1;}
        memset(ir,0xA5,sizeof(ir));
        assert(!tensor_prepare(&p,root,&r)&&r.exit_code==65);
        assert(tensor_export_ir(ir,sizeof(ir))==0);
        for(unsigned j=0;j<sizeof(ir);j++)assert(ir[j]==0xA5);
        tests++;
    }
    /* The production subset rejects prior experimental operators and spills. */
    const char *unsupported[]={
        "input_a a; input_b b; gemm c = a * b; add d = c + c; emit d;",
        "input_a a; input_b b; gemm c = a * b; transpose d = c; emit d;",
        "input_a a; input_b b; gemm c = a * b; relu d = c; emit d;"
    };
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++){
        parser_init(&p,nodes,4096,(const u8 *)unsupported[i],strlen(unsupported[i]));root=parser_parse(&p);
        assert(root&&p.has_tensor&&!tensor_prepare(&p,root,&r)&&r.exit_code==65);
        assert(!tensor_export_ir(ir,sizeof(ir))&&!tensor_export_asm(ir,sizeof(ir)));tests++;
    }
    const unsigned char alias[]="reserve 1; input_a a; input_b b; matmul c = a * b; emit c;";
    parser_init(&p,nodes,4096,alias,sizeof(alias)-1);root=parser_parse(&p);
    assert(root&&tensor_codegen(&p,root,output,sizeof(output),&r));
    assert(r.size==baseline_size&&!memcmp(output,previous,baseline_size));
    n=tensor_export_ir(ir,sizeof(ir));assert(n&&!memcmp(ir,"NIRG0001",8));tests++;
    const unsigned char pressure[]="reserve 248; input_a a; input_a d; input_b b; gemm c = a * b; gemm e = d * b; emit c; emit e;";
    parser_init(&p,nodes,4096,pressure,sizeof(pressure)-1);root=parser_parse(&p);assert(root&&p.has_tensor);
    memset(output,0xA5,sizeof(output));
    assert(!tensor_codegen(&p,root,output,sizeof(output),&r)&&r.exit_code==77);
    for(unsigned i=0;i<sizeof(output);i++)assert(output[i]==0xA5);
    assert(!tensor_export_ir(ir,sizeof(ir))&&!tensor_export_asm(ir,sizeof(ir)));tests++;
    /* Alif variants, harakat and common roots never define binding identity. */
    const char *distinct[]={
        "input_a أ;\ninput_b ا;\ngemm c = أ * ا;\nemit c;\n",
        "input_a كتب; input_b كَتَب; gemm c = كتب * كَتَب; emit c;",
        "input_a والكتاب; input_b سيكتب; gemm c = والكتاب * سيكتب; emit c;"
    };
    for(unsigned i=0;i<sizeof(distinct)/sizeof(distinct[0]);i++){
        parser_init(&p,nodes,4096,(const u8 *)distinct[i],strlen(distinct[i]));root=parser_parse(&p);
        assert(root&&tensor_prepare(&p,root,&r));
        n=tensor_export_ir(ir,sizeof(ir));assert(n&&!memcmp(ir,"NIRG0001",8)&&r32(ir+8)==3);
        const unsigned char *a=ir+32,*b=a+1024;
        assert(r32(a)==1&&r32(b)==2&&r32(a+4)==0&&r32(b+4)==1);
        assert(r32(a+8)==3&&r32(b+8)==3);
        assert(r32(a+12)!=r32(b+12)||memcmp(a+32,b+32,r32(a+12)));
        if(i==0){
            assert(r32(a+12)==2&&r32(b+12)==2&&!memcmp(a+32,"أ",2)&&!memcmp(b+32,"ا",2));
            assert(r32(a+16)==1&&r32(a+20)==9&&r32(b+16)==2&&r32(b+20)==9);
        }
        if(i==2){
            assert(r32(a+28)>0&&r32(b+28)>0);
            assert(r32(a+160)==0x643&&r32(a+164)==0x62a&&r32(a+168)==0x628);
            assert(!memcmp(a+160,b+160,12));
        }
        tests++;
    }
    const char *undefined_alias[]={
        "input_a أ; input_b b; gemm c = ا * b; emit c;",
        "input_a كَتَب; input_b b; gemm c = كتب * b; emit c;",
        "input_a والكتاب; input_b b; gemm c = كتب * b; emit c;"
    };
    for(unsigned i=0;i<sizeof(undefined_alias)/sizeof(undefined_alias[0]);i++){
        parser_init(&p,nodes,4096,(const u8 *)undefined_alias[i],strlen(undefined_alias[i]));root=parser_parse(&p);
        assert(root&&!tensor_prepare(&p,root,&r)&&r.exit_code==65);
        assert(!tensor_export_ir(ir,sizeof(ir)));tests++;
    }
    /* Scalar declarations/references retain their historical identifier space. */
    const char *scalar_names[]={
        "دالة tensor_input_a() -> صحيح { أرجع 0; }",
        "دالة رئيسية() -> صحيح { متغير tensor_gemm: صحيح = 7; أرجع tensor_gemm; }",
        "دالة رئيسية(tensor_emit: صحيح) -> صحيح { أرجع tensor_emit; }",
        "هيكل tensor_reserve { x: صحيح; }",
        "هيكل S { tensor_input_b: صحيح; }",
        "متغير tensor_gemm: صحيح = 7; دالة رئيسية() -> صحيح { أرجع tensor_gemm; }"
    };
    for(unsigned i=0;i<sizeof(scalar_names)/sizeof(scalar_names[0]);i++){
        parser_init(&p,nodes,4096,(const u8 *)scalar_names[i],strlen(scalar_names[i]));root=parser_parse(&p);
        assert(root&&!p.error&&!p.has_tensor);tests++;
    }
    /* Intrinsic call positions and tensor definitions have explicit reservations. */
    const unsigned char reserved_native[]="دالة رئيسية() -> صحيح { متغير tensor_gemm: i8x16x64 = tensor_input_a(); متغير b: i8x64x16 = tensor_input_b(); متغير c: i32x16x16 = tensor_gemm(tensor_gemm,b); tensor_emit(c); أرجع 0; }";
    parser_init(&p,nodes,4096,reserved_native,sizeof(reserved_native)-1);root=parser_parse(&p);
    assert(root&&p.has_tensor&&!tensor_prepare(&p,root,&r)&&r.exit_code==65);
    assert(r.error&&!strcmp(r.error,"tensor intrinsic name is reserved"));tests++;
    const unsigned char reserved_compat[]="input_a tensor_input_a; input_b b; gemm c = tensor_input_a * b; emit c;";
    parser_init(&p,nodes,4096,reserved_compat,sizeof(reserved_compat)-1);root=parser_parse(&p);
    assert(root&&!tensor_prepare(&p,root,&r)&&r.exit_code==65);tests++;
    /* Serialized tensor nodes cannot reconstruct original source provenance. */
    const char serialized[]="(PROGRAM (TENSOR_OP \"input_a\" (NAME \"a\")))";
    unsigned char payload[4096];Parser decoded;
    assert(!ast_deserialize(&decoded,saved,4096,payload,sizeof(payload),serialized,sizeof(serialized)-1));
    assert(decoded.error&&strstr(decoded.error,"provenance"));tests++;
    const unsigned char scalar[]="دالة رئيسية() -> صحيح { أرجع 7; }";
    parser_init(&p,nodes,4096,scalar,sizeof(scalar)-1);root=parser_parse(&p);assert(root&&!p.has_tensor);
    n=ast_serialize(&p,root,(char *)ir,sizeof(ir));assert(n);
    assert(ast_deserialize(&decoded,saved,4096,payload,sizeof(payload),(const char *)ir,n));
    assert(!decoded.error&&!decoded.has_tensor);tests++;
    printf("AST_API_PASS cases=%u\n",tests);return 0;
}
