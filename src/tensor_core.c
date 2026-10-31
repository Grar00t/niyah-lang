/* AST -> bounded typed IR -> proven interval coloring -> native ELF. */
#include "tensor_core.h"
#include "morph.h"
#include <stdint.h>
#include <string.h>
#include "niyah_legacy_tensor_templates.h"
_Static_assert(sizeof(TensorValue)==1024,"canonical value ABI");
_Static_assert(sizeof(TensorOp)==16,"canonical instruction ABI");
enum { MAX_AST=4096, MAX_VALUES=64, MAX_OPS=128, MAX_STMTS=129, MAX_CODE=32768, MAX_PATCH=2048 };
typedef struct { u32 node,kind,arg0,arg1,ir; Token name; i64 mask; } Stmt;
typedef struct { u32 place,base; i64 add; } Fix;
static struct {
    const Parser *p; CodegenResult *r; u32 nv,no,ni,ne,mask,ns;
    TensorValue values[MAX_VALUES]; TensorOp ops[MAX_OPS];
    u64 uses[MAX_VALUES][(MAX_OPS+63)/64]; /* Explicit instruction-index set U(v). */
    Stmt stmt[MAX_STMTS]; MorphResult morph; u8 visited[MAX_AST];
    u8 intervals[528],colors[72],code[MAX_CODE]; Fix fix[MAX_PATCH];
    u32 cp,nfix,vec_size; int failed,ready;
} t;
static u64 up(u64 x,u64 n){return (x+n-1)&~(n-1);}
static void w32(u8 *p,u32 v){for(u32 i=0;i<4;i++)p[i]=(u8)(v>>(8*i));}
static void w64(u8 *p,u64 v){for(u32 i=0;i<8;i++)p[i]=(u8)(v>>(8*i));}
static int equal(Token a,const char *s){size_t n=strlen(s);return a.start && n==a.len && !memcmp(a.start,s,n);}
static int same(Token a,Token b){return a.start&&b.start&&a.len==b.len&&!memcmp(a.start,b.start,a.len);}
static int fail(u32 id,const char *why,int code){
    if(!t.failed){t.r->error=why;t.r->exit_code=code;if(id && id<t.p->count)t.r->at=t.p->nodes[id].tok;}
    t.failed=1;return 0;
}
static AstNode *an(u32 id){return &t.p->nodes[id];}
static int token_in_source(Token n);
static int walk(u32 id,u32 depth){
    if(!id||id>=t.p->count||depth>=128||t.visited[id])return fail(0,"invalid or cyclic tensor AST",65);
    t.visited[id]=1;AstNode *n=an(id);
    if(n->kind<=A_NONE||n->kind>A_TENSOR_OP)return fail(id,"invalid AST kind",65);
    /* Validate every token before collect/statement compare any token bytes. */
    if(!token_in_source(n->tok))return fail(id,"AST token span outside original source",65);
    u32 tail=0,c=n->first;
    while(c){if(!walk(c,depth+1))return 0;tail=c;c=an(c)->next;}
    if(tail!=n->last)return fail(id,"inconsistent AST child boundary",65);
    return 1;
}
static int leaf(u32 id,AstKind kind){
    if(!id||id>=t.p->count||an(id)->kind!=kind||an(id)->first||an(id)->last)return 0;
    if((kind==A_NAME||kind==A_TYPE_NAME)&&an(id)->tok.kind!=T_IDENT)return 0;
    if(kind==A_INT&&an(id)->tok.kind!=T_INT)return 0;
    return 1;
}
static int token_in_source(Token n){
    u64 a=(u64)(uintptr_t)n.start,b=(u64)(uintptr_t)t.p->source;
    return n.start&&a>=b&&a-b<=t.p->source_len&&n.len<=t.p->source_len-(a-b);
}
static int identifier(Token n){
    if(n.kind!=T_IDENT||!n.len||n.len>128||!token_in_source(n))return 0;
    Lexer lex;lex_init(&lex,n.start,n.len);Token parsed=lex_next(&lex);
    return parsed.kind==T_IDENT&&parsed.start==n.start&&parsed.len==n.len&&lex_next(&lex).kind==T_EOF;
}
static int definition(u32 k){return k==TI_INPUT_A||k==TI_INPUT_B||k==TI_GEMM;}
static u32 result_type(u32 k){return k==TI_INPUT_A?1:k==TI_INPUT_B?2:3;}
static u32 expected_op(Token n,int compat){
    const char *a[]={"", "input_a","input_b","gemm","emit","reserve","add","transpose","relu"};
    const char *b[]={"", "tensor_input_a","tensor_input_b","tensor_gemm","tensor_emit","tensor_reserve","tensor_add","tensor_transpose","tensor_relu"};
    if(n.kind!=T_IDENT)return 0;
    for(u32 i=1;i<=TI_RELU;i++)if(equal(n,compat?a[i]:b[i]))return i;
    if(equal(n,compat?"matmul":"tensor_matmul"))return TI_GEMM;
    return 0;
}
static int statement(u32 node,Token name,u32 declared){
    AstNode *n=an(node);u32 k=n->tensor.opcode,c=n->first,ids[4],count=0;
    if(n->kind!=A_TENSOR_OP||n->tensor.syntax>1||k<1||k>TI_RELU||expected_op(n->tok,n->tensor.syntax)!=k)
        return fail(node,"invalid tensor intrinsic",65);
    if(k>TI_RESERVE)return fail(node,"tensor intrinsic outside supported fixed-shape subset",65);
    if(!n->tensor.syntax){
        if(!leaf(c,A_NAME)||!same(an(c)->tok,n->tok))return fail(node,"invalid intrinsic callee",65);
        c=an(c)->next;
    }
    while(c){if(count==4)return fail(node,"too many intrinsic operands",65);ids[count++]=c;c=an(c)->next;}
    Stmt s={0};s.node=node;s.kind=k;s.name=name;
    if(n->tensor.syntax){
        u32 want=k==TI_GEMM?3:1;
        if(count!=want)return fail(node,"invalid compatibility operands",65);
        if(k!=TI_RESERVE){if(!leaf(ids[0],A_NAME))return fail(node,"invalid tensor name",65);s.name=an(ids[0])->tok;}
        if(k==TI_GEMM){s.arg0=ids[1];s.arg1=ids[2];}
        if(k==TI_EMIT)s.arg0=ids[0];
    } else {
        u32 want=k==TI_GEMM?2:((k==TI_EMIT||k==TI_RESERVE)?1:0);
        if(count!=want)return fail(node,"wrong intrinsic arity",65);
        if(definition(k)){if(!name.start||declared!=result_type(k))return fail(node,"tensor declaration shape mismatch",65);}
        else if(name.start)return fail(node,"void tensor intrinsic in declaration",65);
        if(count)s.arg0=ids[0];
        if(count==2)s.arg1=ids[1];
    }
    if(k==TI_RESERVE){
        if(!leaf(ids[0],A_INT)||an(ids[0])->tok.value<0||an(ids[0])->tok.value>255)
            return fail(node,"reserved mask outside 0..255",65);
        s.mask=an(ids[0])->tok.value;
    } else if(k==TI_GEMM){
        if(!leaf(s.arg0,A_NAME)||!leaf(s.arg1,A_NAME))return fail(node,"binary tensor intrinsic requires named tensors",65);
    } else if(k==TI_EMIT && !leaf(s.arg0,A_NAME))return fail(node,"tensor emit requires a tensor name",65);
    n->tensor.left_idx=s.arg0;n->tensor.right_idx=s.arg1;
    if(t.ns==MAX_STMTS)return fail(node,"too many tensor statements",77);
    t.stmt[t.ns++]=s;return 1;
}
static int collect(u32 root){
    u32 c=an(root)->first;
    if(!c)return fail(root,"empty tensor program",65);
    if(an(c)->kind==A_FUNC){
        if(an(c)->next||!equal(an(c)->tok,"\xd8\xb1\xd8\xa6\xd9\x8a\xd8\xb3\xd9\x8a\xd8\xa9"))
            return fail(c,"tensor entry must be a single main function",65);
        u32 ty=an(c)->first;
        if(!leaf(ty,A_TYPE_NAME)||!equal(an(ty)->tok,"\xd8\xb5\xd8\xad\xd9\x8a\xd8\xad"))return fail(c,"tensor main must return i64 and take no parameters",65);
        u32 body=an(ty)->next;
        if(!body||an(body)->kind!=A_BLOCK||an(body)->next)return fail(c,"invalid tensor main body",65);
        c=an(body)->first;int returned=0;
        for(;c;c=an(c)->next){
            AstNode *n=an(c);
            if(n->kind==A_VAR||n->kind==A_CONST){
                ty=n->first;if(!leaf(ty,A_TYPE_NAME))return fail(c,"invalid tensor type",65);
                u32 shape=equal(an(ty)->tok,"i8x16x64")?1:equal(an(ty)->tok,"i8x64x16")?2:equal(an(ty)->tok,"i32x16x16")?3:0;
                u32 init=an(ty)->next;
                if(!shape||!init||an(init)->kind!=A_TENSOR_OP||an(init)->next||an(init)->tensor.syntax)
                    return fail(c,"only initialized fixed-shape tensors are supported",65);
                if(!statement(init,n->tok,shape))return 0;
            } else if(n->kind==A_EXPR_STMT){
                u32 x=n->first;
                if(!x||an(x)->next||an(x)->kind!=A_TENSOR_OP||an(x)->tensor.syntax||!statement(x,(Token){0},0))return fail(c,"unsupported tensor expression statement",65);
            } else if(n->kind==A_RETURN){
                if(n->next||!leaf(n->first,A_INT)||an(n->first)->next||an(n->first)->tok.value!=0)return fail(c,"tensor main must end with return 0",65);
                returned=1;
            } else return fail(c,"tensor control flow or mixed scalar statements are unsupported",65);
        }
        if(!returned)return fail(root,"tensor main requires final return 0",65);
    } else {
        for(;c;c=an(c)->next){if(an(c)->kind!=A_TENSOR_OP||!an(c)->tensor.syntax||!statement(c,(Token){0},0))return fail(c,"mixed scalar/tensor top-level program",65);}
    }
    return 1;
}
/* Exact UTF-8 surface bytes define symbol identity; roots are provenance only. */
static int annotate(void){
    u32 n=0;
    for(u32 i=0;i<t.ns;i++){
        Stmt *s=&t.stmt[i];if(!definition(s->kind))continue;
        if(n==MAX_VALUES)return fail(s->node,"tensor value capacity exceeded",77);
        Token x=s->name;if(!identifier(x))return fail(s->node,"invalid tensor definition span or spelling",65);
        if(parser_tensor_reserved(x))return fail(s->node,"tensor intrinsic name is reserved",65);
        for(u32 j=0;j<n;j++)if(t.values[j].name_len==x.len&&!memcmp(t.values[j].name,x.start,x.len))
            return fail(s->node,"duplicate tensor definition",65);
        if(morph_analyze(x.start,x.len,&t.morph)!=MORPH_OK||t.morph.truncated||t.morph.count>64)return fail(s->node,"morphology annotation rejected",65);
        TensorValue *v=&t.values[n];v->type=result_type(s->kind);v->name_len=x.len;v->line=x.line;v->col=x.col;v->candidates=t.morph.count;
        memcpy(v->name,x.start,x.len);
        for(u32 j=0;j<t.morph.count;j++)if(t.morph.analyses[j].root_len==3){memcpy(v->roots[v->root_count++],t.morph.analyses[j].root,12);}
        an(s->node)->tensor.metadata=++n;
    }
    t.nv=n;return n?1:fail(0,"no tensor definitions",65);
}
static int use(u32 id,u32 defined,u32 type,u32 at,u32 *out){
    Token x=an(id)->tok;if(!identifier(x))return fail(id,"invalid operand span or spelling",65);
    for(u32 i=0;i<defined;i++)if(t.values[i].name_len==x.len&&!memcmp(t.values[i].name,x.start,x.len)){
        if(t.values[i].type!=type)return fail(id,"tensor operand shape mismatch",65);
        if(at>=MAX_OPS)return fail(id,"tensor use instruction outside bounds",65);
        t.uses[i][at/64]|=UINT64_C(1)<<(at%64);*out=i;return 1;
    }
    return fail(id,"undefined tensor operand",65);
}
int tensor_prepare(const Parser *p,u32 root,CodegenResult *r){
    if(!r){t.ready=0;t.failed=1;return 0;}
    memset(r,0,sizeof(*r));memset(&t,0,sizeof(t));t.p=p;t.r=r;
    if(!p||!p->nodes||!root||root>=p->count||p->count>p->capacity||p->count>MAX_AST||!p->source||p->source_len>1048576||p->error||an(root)->kind!=A_PROGRAM||an(root)->next)
        return fail(0,"invalid tensor compiler input",65);
    if(!walk(root,0))return 0;
    for(u32 i=1;i<p->count;i++){
        if(!t.visited[i])return fail(0,"unreachable tensor AST node",65);
        Node_TensorOp *n=&an(i)->tensor;
        n->metadata=n->left_idx=n->right_idx=n->value_type=0;
        n->value_id=n->operand_ids[0]=n->operand_ids[1]=NIYAH_TENSOR_NO_VALUE;
        n->tmm=-1;n->lifespan_start=n->lifespan_end=-1;
    }
    if(!collect(root)||!annotate())return 0;
    u32 defined=0;int reserved_seen=0;
    for(u32 i=0;i<t.ns;i++){
        Stmt *s=&t.stmt[i];u32 k=s->kind;
        if(k==TI_RESERVE){if(reserved_seen||t.no)return fail(s->node,"reserve must occur once before instructions",65);reserved_seen=1;t.mask=(u32)s->mask;continue;}
        if(t.no==MAX_OPS)return fail(s->node,"tensor operation limit exceeded",77);
        s->ir=t.no;
        TensorOp *op=&t.ops[t.no];op->kind=k;
        if(definition(k)){
            if(an(s->node)->tensor.metadata!=defined+1)return fail(s->node,"AST morphology index mismatch",65);
            op->d=defined;TensorValue *v=&t.values[defined];v->start=t.no;
            if(k==TI_GEMM){if(!use(s->arg0,defined,1,t.no,&op->a)||!use(s->arg1,defined,2,t.no,&op->b))return 0;}
            else t.ni++;
            defined++;
        } else {if(!use(s->arg0,defined,3,t.no,&op->d))return 0;t.ne++;}
        t.no++;
    }
    if(!t.ne)return fail(0,"tensor program has no output",65);
    /* Derive intervals only after all definitions and explicit uses are known. */
    if(!t.no||t.no>MAX_OPS)return fail(0,"tensor instruction count outside bounds",65);
    for(u32 i=0;i<t.nv;i++){
        TensorValue *v=&t.values[i];
        if(v->start>=t.no)return fail(0,"tensor definition instruction outside bounds",65);
        u32 last=v->start;
        for(u32 at=0;at<t.no;at++)if(t.uses[i][at/64]&(UINT64_C(1)<<(at%64))){
            if(at<v->start)return fail(0,"tensor use precedes definition",65);
            if(at>last)last=at;
        }
        v->end=last+1;
    }
    memcpy(t.intervals,"NTILE001",8);t.intervals[8]=(u8)t.nv;t.intervals[9]=(u8)t.mask;
    for(u32 i=0;i<t.nv;i++){w32(t.intervals+16+8*i,t.values[i].start);w32(t.intervals+20+8*i,t.values[i].end);}
    u32 available=0;for(u32 i=0;i<8;i++)if(!(t.mask&(1u<<i)))available++;
    for(u32 i=0;i<t.no;i++)if(t.ops[i].kind==TI_GEMM&&available<3)
        return fail(0,"GEMM requires three available physical tiles",77);
    int status=tensor_allocate(t.intervals,16+8*t.nv,t.colors,sizeof(t.colors));
    if(status)return fail(0,status==77?"tensor live values exceed available tiles":"invalid allocator input",status==77?77:65);
    /* Independent contract guard around the assembly allocator. */
    if(memcmp(t.colors,"TMM1",4)||t.colors[4]!=t.nv||t.colors[5]||t.colors[6]||t.colors[7])
        return fail(0,"invalid allocator result header",65);
    for(u32 i=0;i<t.nv;i++){
        u32 c=t.colors[8+i];if(c>=8||(t.mask&(1u<<c)))return fail(0,"invalid allocated tile",65);
        for(u32 j=0;j<i;j++)if(c==t.colors[8+j]&&t.values[i].start<t.values[j].end&&t.values[j].start<t.values[i].end)
            return fail(0,"interfering allocated values",65);
    }
    for(u32 i=0;i<t.ns;i++){
        Stmt *s=&t.stmt[i];if(s->kind==TI_RESERVE)continue;
        u32 value=t.ops[s->ir].d;Node_TensorOp *n=&an(s->node)->tensor;
        n->value_id=value;n->value_type=t.values[value].type;n->metadata=value+1;
        if(s->kind==TI_GEMM){n->operand_ids[0]=t.ops[s->ir].a;n->operand_ids[1]=t.ops[s->ir].b;}
        else if(s->kind==TI_EMIT)n->operand_ids[0]=value;
        n->lifespan_start=(int)t.values[value].start;n->lifespan_end=(int)t.values[value].end;
        n->tmm=(int)t.colors[8+value];
    }
    t.ready=1;return 1;
}
u32 tensor_export_ir(u8 *out,u32 capacity){
    if(!t.ready||t.failed||!out)return 0;
    u32 n=32+t.nv*1024+t.no*16+(u32)t.p->source_len;if(capacity<n)return 0;
    memcpy(out,"NIRG0001",8);w32(out+8,t.nv);w32(out+12,t.no);w32(out+16,t.ni);w32(out+20,t.ne);w32(out+24,t.mask);w32(out+28,(u32)t.p->source_len);
    memcpy(out+32,t.values,t.nv*1024);memcpy(out+32+t.nv*1024,t.ops,t.no*16);memcpy(out+32+t.nv*1024+t.no*16,t.p->source,t.p->source_len);return n;
}
/* Byte-identical textual emitter, used for differential verification only. */
typedef struct { u8 *p;u32 n,cap;int bad; } Text;
static void text(Text *o,const char *s){u32 n=(u32)strlen(s);if(o->bad||n>o->cap-o->n){o->bad=1;return;}memcpy(o->p+o->n,s,n);o->n+=n;}
static void number(Text *o,u32 n){char b[11];u32 p=10;b[p]=0;do{b[--p]=(char)('0'+n%10);n/=10;}while(n);text(o,b+p);}
static u32 col(u32 v){return t.colors[8+v];}
u32 tensor_export_asm(u8 *out,u32 capacity){
    if(!t.ready||t.failed||!out)return 0;
    Text b={out,0,capacity,0};Text *o=&b;
    text(o,asm_head);number(o,t.nv);text(o,s_comma);number(o,t.no);text(o,s_comma);number(o,t.ni);text(o,s_comma);number(o,t.ne);text(o,s_nl);
    for(u32 i=0;i<t.no;i++){text(o,s_long);number(o,t.ops[i].kind);text(o,s_comma);number(o,t.ops[i].d);text(o,s_comma);number(o,t.ops[i].a);text(o,s_comma);number(o,t.ops[i].b);text(o,s_nl);}
    text(o,s_colors);for(u32 i=0;i<t.nv;i++){text(o,s_byte);number(o,col(i));text(o,s_nl);}
    text(o,s_bss);number(o,t.ni*1024);text(o,s_slots);number(o,t.ne*1024);text(o,s_pack);
    u32 input=0,output=0;
    for(u32 i=0;i<t.no;i++){TensorOp *p=&t.ops[i];
        if(p->kind<3){text(o,s_src_input);number(o,1024*input++);text(o,s_dst_slot);number(o,1024*col(p->d));text(o,s_copy);}
        else if(p->kind==3){text(o,s_arg_a);number(o,1024*col(p->a));text(o,s_arg_b);number(o,1024*col(p->b));text(o,s_arg_c);number(o,1024*col(p->d));text(o,s_call);}
        else {text(o,s_src_slot);number(o,1024*col(p->d));text(o,s_dst_output);number(o,1024*output++);text(o,s_copy);}
    }
    text(o,s_amx);input=output=0;
    for(u32 i=0;i<t.no;i++){TensorOp *p=&t.ops[i];
        if(p->kind<3){text(o,s_amx_input);number(o,1024*input++);if(p->kind==2)text(o,s_pack_call);text(o,s_amx_load);number(o,col(p->d));text(o,s_amx_address);}
        else if(p->kind==3){text(o,s_zero);number(o,col(p->d));text(o,s_mma);number(o,col(p->d));text(o,s_tmm);number(o,col(p->a));text(o,s_tmm);number(o,col(p->b));text(o,s_nl);}
        else {text(o,s_amx_out);number(o,1024*output++);text(o,s_store);number(o,col(p->d));text(o,s_nl);}
    }
    text(o,s_end);return o->bad?0:o->n;
}
/* Relocatable instruction fragments are generated once at compiler build time. */
typedef struct { u32 base; i64 off; } Address;
static int fragment(u32 id,Address a,Address b,Address c){
    if(id>=sizeof(t_chunk)/sizeof(t_chunk[0]))return fail(0,"invalid instruction template",65);
    const TChunk *q=&t_chunk[id];if(q->len>MAX_CODE-t.cp||q->count>MAX_PATCH-t.nfix)return fail(0,"tensor machine-code bound",65);
    memcpy(t.code+t.cp,t_chunks+q->off,q->len);Address args[3]={a,b,c};
    for(u32 i=0;i<q->count;i++){const TFix *f=&t_patch[q->first+i];t.fix[t.nfix++]=(Fix){t.cp+f->a,args[f->b].base,args[f->b].off+f->c};}
    t.cp+=q->len;return 1;
}
#define AD(b,o) ((Address){(b),(i64)(o)})
#define F(id,a,b,c) do{if(!fragment((id),(a),(b),(c)))return 0;}while(0)
static void section(u8 *s,u64 va,u64 off,u64 len){w64(s+16,va);w64(s+24,off);w64(s+32,len);}
static void segment(u8 *s,u64 off,u64 va,u64 file,u64 mem){w64(s+8,off);w64(s+16,va);w64(s+24,va);w64(s+32,file);w64(s+40,mem);}
static int relocation(u8 *out,u64 base,u64 place,u64 target,i64 add){
    i64 d=(i64)target+add-(i64)place;if(d<INT32_MIN||d>INT32_MAX)return fail(0,"native displacement outside signed32",65);
    w32(out+(place-base),(u32)d);return 1;
}
int tensor_codegen(const Parser *p,u32 root,u8 *out,u32 cap,CodegenResult *r){
    if(!tensor_prepare(p,root,r))return 0;
    u32 input=0,output=0;Address z=AD(0,0);
    for(u32 i=0;i<t.no;i++){TensorOp *x=&t.ops[i];
        if(x->kind<3)F(T_VLOAD,AD(11,1024*input++),AD(12,1024*col(x->d)),z);
        else if(x->kind==3)F(T_VGEMM,AD(12,1024*col(x->a)),AD(12,1024*col(x->b)),AD(12,1024*col(x->d)));
        else F(T_VEMIT,AD(12,1024*col(x->d)),AD(13,1024*output++),z);
    }
    F(T_RET,z,z,z);t.vec_size=t.cp;
    F(T_AMXSTART,AD(T_NX_TILECFG_BASE,T_NX_TILECFG_OFF),z,z);input=output=0;
    for(u32 i=0;i<t.no;i++){TensorOp *x=&t.ops[i];
        if(x->kind==1)F(t_load_a[col(x->d)],AD(11,1024*input++),z,z);
        else if(x->kind==2)F(t_load_b[col(x->d)],AD(11,1024*input++),AD(14,0),AD(T_NX_PACK_B_BASE,T_NX_PACK_B_OFF));
        else if(x->kind==3)F(t_gemm[col(x->d)*64+col(x->a)*8+col(x->b)],z,z,z);
        else F(t_store[col(x->d)],AD(13,1024*output++),z,z);
    }
    F(T_AMXEND,z,z,z);
    const u64 base=0x400000;u64 a[19]={0};a[0]=base+4096;a[3]=a[0];a[4]=a[0]+t.vec_size;
    a[5]=up(a[0]+t.cp,T_M0_TA);a[6]=up(a[5]+T_M0_TEXT,T_M1_TA);u64 textlen=a[6]+T_M1_TEXT-a[0];
    u64 rooff=up(4096+textlen,4096);a[1]=base+rooff;a[15]=a[1];a[16]=a[1]+16+16*t.no;
    a[7]=up(a[16]+t.nv,T_M0_RA);a[8]=up(a[7]+T_M0_RO,T_M1_RA);u64 rolen=a[8]+T_M1_RO-a[1];
    a[2]=a[1]+rolen+4096;a[11]=a[2];a[12]=a[11]+t.ni*1024;a[13]=a[12]+8192;a[14]=a[13]+t.ne*1024;
    a[9]=up(a[14]+1024,T_M0_BA);a[10]=up(a[9]+T_M0_BSS,T_M1_BA);a[17]=a[2];a[18]=up(a[10]+T_M1_BSS,8);
    u64 bsslen=a[10]+T_M1_BSS-a[2],symoff=up(rooff+rolen,8),stroff=symoff+sizeof(t_symbols),shstroff=stroff+sizeof(t_strings),shoff=up(shstroff+sizeof(t_section_names),8),size=shoff+sizeof(t_sections);
    if(!out)return fail(root,"missing tensor output buffer",65);
    if(size>cap)return fail(root,"tensor output capacity exceeded",74);
    memset(out,0,(size_t)size);memcpy(out,t_header,sizeof(t_header));memcpy(out+4096,t.code,t.cp);
    memcpy(out+a[5]-base,t_module0_text,T_M0_TEXT);memcpy(out+a[6]-base,t_module1_text,T_M1_TEXT);
    u8 *plan=out+rooff;w32(plan,t.nv);w32(plan+4,t.no);w32(plan+8,t.ni);w32(plan+12,t.ne);memcpy(plan+16,t.ops,t.no*16);memcpy(plan+16+t.no*16,t.colors+8,t.nv);
    memcpy(out+a[7]-base,t_module0_rodata,T_M0_RO);memcpy(out+a[8]-base,t_module1_rodata,T_M1_RO);
    memcpy(out+symoff,t_symbols,sizeof(t_symbols));memcpy(out+stroff,t_strings,sizeof(t_strings));memcpy(out+shstroff,t_section_names,sizeof(t_section_names));memcpy(out+shoff,t_sections,sizeof(t_sections));
    for(u32 i=0;i<t.nfix;i++)if(!relocation(out,base,a[0]+t.fix[i].place,a[t.fix[i].base],t.fix[i].add))return 0;
    for(u32 i=0;i<sizeof(t_reloc)/sizeof(t_reloc[0]);i++){const TRel *x=&t_reloc[i];if(!relocation(out,base,a[x->a]+x->b,a[x->c],x->d))return 0;}
    for(u32 i=0;i<sizeof(t_symfix)/sizeof(t_symfix[0]);i++){const TFix *x=&t_symfix[i];w64(out+symoff+x->a,a[x->b]+x->c);}
    w64(out+24,a[T__START_BASE]+T__START_OFF);w64(out+40,shoff);
    segment(out+64+56,4096,a[0],textlen,textlen);segment(out+64+112,rooff,a[1],rolen,rolen);segment(out+64+168,a[2]&4095,a[2],0,bsslen);
    section(out+shoff+64,a[0],4096,textlen);section(out+shoff+128,a[1],rooff,rolen);section(out+shoff+192,a[2],symoff,bsslen);
    section(out+shoff+256,0,symoff,sizeof(t_symbols));section(out+shoff+320,0,stroff,sizeof(t_strings));section(out+shoff+384,0,shstroff,sizeof(t_section_names));
    r->size=(u32)size;r->exit_code=0;return 1;
}
