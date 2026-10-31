/* Typed tensor IR -> interval interference graph -> physical tile schedule.
 * Every value owns a 1024-byte home at a 64-byte-aligned stack address.
 * Build-time instruction templates are the only assembler dependency. */
#include "tensor_v2.h"
#include <stdint.h>
#include <string.h>
#include "tensor_v2_templates.h"

enum { V2_VALUES=64,V2_OPS=128,V2_ACTIONS=2048,V2_TEXT=262144,NONE=255 };
enum {
    E_PROLOGUE,E_EPILOGUE,E_COPY_IN,E_COPY_OUT,E_ARGS3,E_CALL,E_CALL_R15,
    E_PACK_IN,E_CFG,E_RELEASE,E_LOAD,E_STORE,E_GEMM
};
typedef struct { u32 kind,reg;u64 p[3]; } Action;
static struct {
    const TensorValue *values;const TensorOp *ops;CodegenResult *result;
    u32 nv,no,ni,ne,mask,na,vector_end,edges,spills,evictions;
    u32 start[V2_VALUES],end[V2_VALUES];u64 graph[V2_VALUES];
    u8 color[V2_VALUES],resident[8],location[V2_VALUES],dirty[8],home[V2_VALUES];
    Action action[V2_ACTIONS];
    u8 binary[sizeof(t2_elf)],text[V2_TEXT];u32 text_len;
    int failed;
} v2;

static int fail(const char *why,int code){
    if(!v2.failed&&v2.result){v2.result->error=why;v2.result->exit_code=code;}
    v2.failed=1;return 0;
}
static void w32(u8 *p,u32 x){for(u32 i=0;i<4;i++)p[i]=(u8)(x>>(8*i));}
static void w64(u8 *p,u64 x){for(u32 i=0;i<8;i++)p[i]=(u8)(x>>(8*i));}
static u64 slot(u32 value){return (u64)value*1024;}
static int action(u32 kind,u32 reg,u64 a,u64 b,u64 c){
    if(v2.na==V2_ACTIONS)return fail("tensor schedule capacity exhausted",77);
    v2.action[v2.na++]=(Action){kind,reg,{a,b,c}};return 1;
}
static int use(u32 id,u32 defined,u32 type,u32 at){
    if(id>=defined||v2.values[id].type!=type)return fail("invalid tensor operand or type",65);
    v2.end[id]=at+1;return 1;
}
/* Validate canonical definitions, types and lifetimes independently of the AST. */
static int validate(void){
    if(!v2.values||!v2.ops||!v2.nv||v2.nv>V2_VALUES||!v2.no||v2.no>V2_OPS||
       !v2.ne||v2.ne>V2_OPS||v2.ni>V2_VALUES||v2.mask>255)
        return fail("invalid tensor IR bounds",65);
    u32 defined=0,inputs=0,outputs=0;int gemm=0;
    for(u32 i=0;i<v2.no;i++){
        const TensorOp *o=&v2.ops[i];u32 k=o->kind;
        if(k==4){
            if(o->a||o->b||!use(o->d,defined,3,i))return fail("invalid tensor emit",65);
            outputs++;continue;
        }
        if(k!=1&&k!=2&&k!=3&&k!=6&&k!=7&&k!=8)return fail("unknown tensor IR operation",65);
        if(defined>=v2.nv||o->d!=defined||v2.values[defined].type!=(k<3?k:3))
            return fail("invalid tensor definition order or type",65);
        if(k<3){if(o->a||o->b)return fail("invalid tensor input operands",65);inputs++;}
        else if(k==3){
            if(!use(o->a,defined,1,i)||!use(o->b,defined,2,i))return 0;
            gemm=1;
        } else {
            if(!use(o->a,defined,3,i))return 0;
            if(k==6){if(!use(o->b,defined,3,i))return 0;}
            else if(o->b)return fail("invalid unary tensor operand",65);
        }
        v2.start[defined]=i;v2.end[defined]=i+1;defined++;
    }
    if(defined!=v2.nv||inputs!=v2.ni||outputs!=v2.ne)return fail("inconsistent tensor IR counts",65);
    for(u32 i=0;i<v2.nv;i++)
        if(v2.values[i].start!=v2.start[i]||v2.values[i].end!=v2.end[i])
            return fail("inconsistent tensor lifetime interval",65);
    u32 capacity=0;for(u32 r=0;r<8;r++)if(!(v2.mask&(1u<<r)))capacity++;
    if(gemm&&capacity<3)return fail("GEMM requires three unreserved tile registers",77);
    return 1;
}
/* Definitions are in start order. Greedy interval coloring is optimal without
 * spills; graph vertices that cannot obtain a color retain a stack-only home.
 * The physical scheduler below treats colors as preferences, never as an
 * excuse to overwrite a live register while reloading a spilled value. */
static int color_graph(void){
    memset(v2.color,NONE,sizeof(v2.color));
    for(u32 i=0;i<v2.nv;i++)for(u32 j=0;j<i;j++){
        if(v2.start[i]<v2.end[j]&&v2.start[j]<v2.end[i]){
            v2.graph[i]|=(u64)1<<j;v2.graph[j]|=(u64)1<<i;v2.edges++;
        }
    }
    for(u32 i=0;i<v2.nv;i++){
        u32 forbidden=v2.mask;
        for(u32 j=0;j<i;j++)if((v2.graph[i]&((u64)1<<j))&&v2.color[j]<8)
            forbidden|=1u<<v2.color[j];
        for(u32 r=0;r<8;r++)if(!(forbidden&(1u<<r))){v2.color[i]=(u8)r;break;}
        if(v2.color[i]==NONE)v2.spills++;
    }
    for(u32 i=0;i<v2.nv;i++)if(v2.color[i]<8){
        if(v2.mask&(1u<<v2.color[i]))return fail("reserved tile coloring",65);
        for(u32 j=0;j<i;j++)if((v2.graph[i]&((u64)1<<j))&&v2.color[i]==v2.color[j])
            return fail("interfering tile coloring",65);
    }
    return 1;
}
static int vector_schedule(void){
    if(!action(E_PROLOGUE,0,slot(v2.nv),0,0))return 0;
    u32 input=0,output=0;
    for(u32 i=0;i<v2.no;i++){
        const TensorOp *o=&v2.ops[i];
        if(o->kind<3){
            if(!action(E_COPY_IN,0,T2_INPUTS_VA+slot(input++),slot(o->d),0))return 0;
        } else if(o->kind==4){
            if(!action(E_COPY_OUT,0,slot(o->d),T2_OUTPUTS_VA+slot(output++),0))return 0;
        } else {
            if(!action(E_ARGS3,0,slot(o->a),o->kind==7||o->kind==8?0:slot(o->b),slot(o->d)))return 0;
            if(o->kind==3){if(!action(E_CALL_R15,0,0,0,0))return 0;}
            else if(!action(E_CALL,0,o->kind==6?T2_ADD_VA:o->kind==7?T2_TRANSPOSE_VA:T2_RELU_VA,0,0))return 0;
        }
    }
    if(!action(E_EPILOGUE,0,0,0,0))return 0;
    v2.vector_end=v2.na;return 1;
}
static void expire(u32 at){
    for(u32 r=0;r<8;r++)if(v2.resident[r]!=NONE&&v2.end[v2.resident[r]]<=at){
        v2.location[v2.resident[r]]=NONE;v2.resident[r]=NONE;v2.dirty[r]=0;
    }
}
static int store_register(u32 r,int force){
    u32 value=v2.resident[r];
    if(value>=v2.nv)return fail("invalid tile residency",65);
    if(force||v2.dirty[r]){
        if(!action(E_STORE,r,slot(value),0,0))return 0;
        v2.home[value]=1;v2.dirty[r]=0;
    }
    return v2.home[value]?1:fail("tensor value has no backing store",65);
}
static int sync_memory(u32 value){
    u32 r=v2.location[value];
    if(r<8)return store_register(r,0);
    return v2.home[value]?1:fail("reload of an uninitialized tensor home",65);
}
/* Acquire an unpinned register. All survivors have end > at after expire().
 * Eviction writes an explicit spill, including immutable input tiles; this
 * makes every displaced live tile's backing state independent of its origin. */
static int acquire(u32 value,u32 pinned,u32 *result){
    u32 selected=NONE,preferred=v2.color[value];
    if(preferred<8&&!(pinned&(1u<<preferred))&&v2.resident[preferred]==NONE)selected=preferred;
    if(selected==NONE)for(u32 r=0;r<8;r++)
        if(!(v2.mask&(1u<<r))&&!(pinned&(1u<<r))&&v2.resident[r]==NONE){selected=r;break;}
    if(selected==NONE&&preferred<8&&!(pinned&(1u<<preferred)))selected=preferred;
    if(selected==NONE)for(u32 r=0;r<8;r++)if(!(v2.mask&(1u<<r))&&!(pinned&(1u<<r))){
        if(selected==NONE||v2.end[v2.resident[r]]>v2.end[v2.resident[selected]])selected=r;
    }
    if(selected==NONE)return fail("no legal tile register for required operands",77);
    if(v2.mask&(1u<<selected))return fail("reserved physical tile selected",65);
    if(v2.resident[selected]!=NONE){
        if(!store_register(selected,1))return 0;
        v2.location[v2.resident[selected]]=NONE;v2.evictions++;
    }
    v2.resident[selected]=(u8)value;v2.location[value]=(u8)selected;v2.dirty[selected]=0;
    *result=selected;return 1;
}
static int ensure_tile(u32 value,u32 pinned,u32 *result){
    if(v2.location[value]<8){*result=v2.location[value];return 1;}
    if(!v2.home[value])return fail("missing tensor reload source",65);
    if(!acquire(value,pinned,result))return 0;
    return action(E_LOAD,*result,slot(value),0,0);
}
static int amx_schedule(void){
    memset(v2.resident,NONE,sizeof(v2.resident));memset(v2.location,NONE,sizeof(v2.location));
    if(!action(E_PROLOGUE,0,slot(v2.nv),0,0)||!action(E_CFG,0,T2_TILECFG_VA,0,0))return 0;
    u32 input=0,output=0;
    for(u32 i=0;i<v2.no;i++){
        const TensorOp *o=&v2.ops[i];expire(i);
        if(o->kind<3){
            if(!action(o->kind==1?E_COPY_IN:E_PACK_IN,0,T2_INPUTS_VA+slot(input++),slot(o->d),0))return 0;
            v2.home[o->d]=1;u32 r;
            if(!acquire(o->d,0,&r)||!action(E_LOAD,r,slot(o->d),0,0))return 0;
        } else if(o->kind==3){
            u32 a,b,d,pinned=0;
            if(v2.location[o->a]<8)pinned|=1u<<v2.location[o->a];
            if(v2.location[o->b]<8)pinned|=1u<<v2.location[o->b];
            if(!ensure_tile(o->a,pinned,&a))return 0;
            pinned|=1u<<a;
            if(!ensure_tile(o->b,pinned,&b))return 0;
            pinned|=1u<<b;
            if(!acquire(o->d,pinned,&d))return 0;
            if(a==b||a==d||b==d)return fail("overlapping GEMM physical operands",65);
            if(!action(E_GEMM,d*64+a*8+b,0,0,0))return 0;
            v2.dirty[d]=1;v2.home[o->d]=0;
        } else if(o->kind==4){
            if(!sync_memory(o->d)||!action(E_COPY_OUT,0,slot(o->d),T2_OUTPUTS_VA+slot(output++),0))return 0;
        } else {
            if(!sync_memory(o->a)||(o->kind==6&&!sync_memory(o->b)))return 0;
            if(!action(E_ARGS3,0,slot(o->a),o->kind==6?slot(o->b):0,slot(o->d))||
               !action(E_CALL,0,o->kind==6?T2_ADD_VA:o->kind==7?T2_TRANSPOSE_VA:T2_RELU_VA,0,0))return 0;
            v2.home[o->d]=1;
        }
    }
    return action(E_RELEASE,0,0,0,0)&&action(E_EPILOGUE,0,0,0,0);
}
static int prepare(const TensorValue *values,u32 nv,const TensorOp *ops,
                   u32 no,u32 ni,u32 ne,u32 mask,CodegenResult *result){
    memset(&v2,0,sizeof(v2));v2.result=result;
    if(!result)return fail("missing tensor result",65);
    memset(result,0,sizeof(*result));
    v2.values=values;v2.ops=ops;v2.nv=nv;v2.no=no;v2.ni=ni;v2.ne=ne;v2.mask=mask;
    return validate()&&color_graph()&&vector_schedule()&&amx_schedule();
}
static u32 chunk(const Action *a){
    switch(a->kind){
    case E_PROLOGUE:return T2_PROLOGUE;case E_EPILOGUE:return T2_EPILOGUE;
    case E_COPY_IN:return T2_COPY_IN;case E_COPY_OUT:return T2_COPY_OUT;
    case E_ARGS3:return T2_ARGS3;case E_CALL:return T2_CALL;case E_CALL_R15:return T2_CALL_R15;
    case E_PACK_IN:return T2_PACK_IN;case E_CFG:return T2_LDTILECFG;case E_RELEASE:return T2_TILERELEASE;
    case E_LOAD:return a->reg<8?t2_load[a->reg]:UINT32_MAX;
    case E_STORE:return a->reg<8?t2_store[a->reg]:UINT32_MAX;
    case E_GEMM:return a->reg<512?t2_gemm[a->reg]:UINT32_MAX;
    default:return UINT32_MAX;
    }
}
static int patch_value(u64 parameter,long addend,i64 *value){
    if(parameter>INT64_MAX)return fail("tensor relocation parameter overflow",65);
    i64 x=(i64)parameter,y=(i64)addend;
    if((y>0&&x>INT64_MAX-y)||(y<0&&x<INT64_MIN-y))return fail("tensor relocation addition overflow",65);
    *value=x+y;return 1;
}
static int emit_region(u32 first,u32 end,u32 offset,u32 capacity){
    if(offset>sizeof(v2.binary)||capacity>sizeof(v2.binary)-offset)return fail("invalid tensor executable region",65);
    u32 cursor=0;
    for(u32 i=first;i<end;i++){
        const Action *a=&v2.action[i];u32 id=chunk(a);
        if(id>=sizeof(t2_chunk)/sizeof(t2_chunk[0]))return fail("invalid tensor instruction template",65);
        const T2Chunk *c=&t2_chunk[id];
        if(c->len>capacity-cursor)return fail("tensor machine-code region exhausted",77);
        if(c->off>sizeof(t2_chunks)||c->len>sizeof(t2_chunks)-c->off||
           c->first>sizeof(t2_patch)/sizeof(t2_patch[0])||
           c->count>sizeof(t2_patch)/sizeof(t2_patch[0])-c->first)
            return fail("invalid tensor template bounds",65);
        memcpy(v2.binary+offset+cursor,t2_chunks+c->off,c->len);
        for(u32 j=0;j<c->count;j++){
            const T2Patch *p=&t2_patch[c->first+j];i64 value;
            if(p->param>=3||(p->width!=4&&p->width!=8)||p->off>c->len||p->width>c->len-p->off)
                return fail("invalid tensor instruction relocation",65);
            if(!patch_value(a->p[p->param],p->addend,&value))return 0;
            u8 *at=v2.binary+offset+cursor+p->off;
            if(p->width==4){
                if(value<INT32_MIN||value>(i64)UINT32_MAX)return fail("tensor relocation outside32 bits",65);
                w32(at,(u32)value);
            } else w64(at,(u64)value);
        }
        cursor+=c->len;
    }
    return 1;
}
int tensor_v2_codegen(const TensorValue *values,u32 nv,const TensorOp *ops,
                      u32 no,u32 ni,u32 ne,u32 mask,u8 *out,u32 capacity,CodegenResult *result){
    if(!prepare(values,nv,ops,no,ni,ne,mask,result))return 0;
    if(!out||capacity<sizeof(t2_elf))return fail("tensor executable output capacity exceeded",74);
    if(T2_PLAN_OFFSET>sizeof(t2_elf)||16>sizeof(t2_elf)-T2_PLAN_OFFSET||
       slot(ni)>T2_INPUT_CAPACITY||slot(ne)>T2_OUTPUT_CAPACITY)
        return fail("tensor runtime template capacity exceeded",77);
    memcpy(v2.binary,t2_elf,sizeof(t2_elf));
    w32(v2.binary+T2_PLAN_OFFSET,nv);w32(v2.binary+T2_PLAN_OFFSET+4,no);
    w32(v2.binary+T2_PLAN_OFFSET+8,ni);w32(v2.binary+T2_PLAN_OFFSET+12,ne);
    if(!emit_region(0,v2.vector_end,T2_VECTOR_OFFSET,T2_VECTOR_CAPACITY)||
       !emit_region(v2.vector_end,v2.na,T2_AMX_OFFSET,T2_AMX_CAPACITY))return 0;
    memcpy(out,v2.binary,sizeof(t2_elf));result->size=(u32)sizeof(t2_elf);result->exit_code=0;return 1;
}
static int text(const char *s){
    size_t n=strlen(s);if(n>V2_TEXT-v2.text_len)return fail("tensor assembly buffer exhausted",77);
    memcpy(v2.text+v2.text_len,s,n);v2.text_len+=(u32)n;return 1;
}
static int number(u64 n){
    char b[21];u32 i=20;b[i]=0;do{b[--i]=(char)('0'+n%10);n/=10;}while(n);return text(b+i);
}
static int stack_address(const char *prefix,u64 offset){return text(prefix)&&number(offset)&&text("]\n");}
static int helper(u64 address){
    if(address==T2_ADD_VA)return text("nx_i32_add");
    if(address==T2_TRANSPOSE_VA)return text("nx_i32_transpose");
    if(address==T2_RELU_VA)return text("nx_i32_relu");
    return fail("invalid tensor helper address",65);
}
static int text_action(const Action *a){
    switch(a->kind){
    case E_PROLOGUE:
        return text(" push r12\n push r13\n mov r13,rsp\n sub rsp,")&&number(a->p[0])&&text("\n and rsp,-64\n mov r12,rsp\n");
    case E_EPILOGUE:return text(" mov rsp,r13\n pop r13\n pop r12\n ret\n");
    case E_COPY_IN:
        return stack_address(" lea rsi,[rip+nx_inputs+",a->p[0]-T2_INPUTS_VA)&&
            stack_address(" lea rdi,[r12+",a->p[1])&&text(" mov ecx,128\n rep movsq\n");
    case E_COPY_OUT:
        return stack_address(" lea rsi,[r12+",a->p[0])&&
            stack_address(" lea rdi,[rip+nx_outputs+",a->p[1]-T2_OUTPUTS_VA)&&text(" mov ecx,128\n rep movsq\n");
    case E_ARGS3:
        return stack_address(" lea rdi,[r12+",a->p[0])&&stack_address(" lea rsi,[r12+",a->p[1])&&
            stack_address(" lea rdx,[r12+",a->p[2]);
    case E_CALL:return text(" call ")&&helper(a->p[0])&&text("\n");
    case E_CALL_R15:return text(" call r15\n");
    case E_PACK_IN:
        return stack_address(" lea rdi,[rip+nx_inputs+",a->p[0]-T2_INPUTS_VA)&&
            stack_address(" lea rsi,[r12+",a->p[1])&&text(" call nx_pack_b\n");
    case E_CFG:return text(" lea rax,[rip+nx_tilecfg]\n ldtilecfg [rax]\n");
    case E_RELEASE:return text(" tilerelease\n");
    case E_LOAD:
        return stack_address(" lea rdi,[r12+",a->p[0])&&text(" mov eax,64\n tileloadd tmm")&&
            number(a->reg)&&text(",[rdi+rax*1]\n");
    case E_STORE:
        return stack_address(" lea rdi,[r12+",a->p[0])&&text(" mov eax,64\n tilestored [rdi+rax*1],tmm")&&
            number(a->reg)&&text("\n");
    case E_GEMM:
        return text(" tilezero tmm")&&number(a->reg/64)&&text("\n tdpbssd tmm")&&number(a->reg/64)&&
            text(",tmm")&&number(a->reg/8%8)&&text(",tmm")&&number(a->reg%8)&&text("\n");
    default:return fail("unknown tensor assembly action",65);
    }
}
u32 tensor_v2_export_asm(const TensorValue *values,u32 nv,const TensorOp *ops,
                         u32 no,u32 ni,u32 ne,u32 mask,u8 *out,u32 capacity,CodegenResult *result){
    if(!prepare(values,nv,ops,no,ni,ne,mask,result))return 0;
    if(!text(".intel_syntax noprefix\n# Fixed 16x64-byte tile geometry; all value homes use aligned stack storage.\n# Interference edges: ")||
       !number(v2.edges)||!text("; graph spills: ")||!number(v2.spills)||!text("; physical evictions: ")||!number(v2.evictions)||!text("\n"))return 0;
    for(u32 i=0;i<nv;i++){
        if(!text("# v")||!number(i)||!text(" [")||!number(v2.start[i])||!text(",")||!number(v2.end[i])||!text(") "))return 0;
        if(v2.color[i]==NONE){if(!text("stack"))return 0;}else if(!text("tmm")||!number(v2.color[i]))return 0;
        if(!text("\n"))return 0;
    }
    if(!text(".section .rodata\n.global nx_plan,nx_colors\nnx_plan:\n .long ")||!number(nv)||!text(",")||!number(no)||
       !text(",")||!number(ni)||!text(",")||!number(ne)||!text("\nnx_colors:\n"))return 0;
    for(u32 i=0;i<nv;i++)if(!text(" .byte ")||!number(v2.color[i])||!text("\n"))return 0;
    if(!text(".section .bss\n.balign 64\n.global nx_inputs,nx_outputs\nnx_inputs: .skip ")||!number(slot(ni))||
       !text("\nnx_outputs: .skip ")||!number(slot(ne))||
       !text("\n.section .text\n.global nx_vector_program,nx_amx_program\nnx_vector_program:\n"))return 0;
    for(u32 i=0;i<v2.na;i++){
        if(i==v2.vector_end&&!text("nx_amx_program:\n"))return 0;
        if(!text_action(&v2.action[i]))return 0;
    }
    if(!text(".section .note.GNU-stack,\"\",@progbits\n"))return 0;
    if(!out||v2.text_len>capacity)return (u32)fail("tensor assembly output capacity exceeded",74);
    memcpy(out,v2.text,v2.text_len);result->exit_code=0;result->size=v2.text_len;return v2.text_len;
}
