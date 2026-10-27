#include "codegen.h"
#include "check.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(i64) == 8, "native emitter needs 64-bit i64");

enum { ELF_BASE = 0x400000, CODE_START = 256,
       RELOC_CALL, RELOC_TRAP, RELOC_GLOBAL,
       TRAP_OVERFLOW = 0, TRAP_DIVZERO, TRAP_SHIFT, TRAP_FUEL,
       TRAP_DEPTH, TRAP_UNINITIALIZED, TRAP_COUNT };
typedef struct { u32 node, address, argc, body; } Function;
typedef struct { Token name; u32 global; int32_t displacement; u8 kind; } Binding;
/* Binding kind: 0 local with flag, 1 parameter, 2 global with flag. */
typedef struct { u32 location, target, kind; } Relocation;
typedef struct {
    const Parser *p;
    CodegenResult *result;
    u8 *out;
    u32 cap, pos, current;
    int failed;
    Function functions[CODEGEN_MAX_FUNCTIONS];
    u32 function_count;
    Binding bindings[CODEGEN_MAX_SLOTS];
    u32 binding_count, global_count, locals_used;
    Relocation relocations[CODEGEN_MAX_RELOCS];
    u32 relocation_count, traps[TRAP_COUNT];
    u32 global_nodes[CODEGEN_MAX_SLOTS];
} Emitter;

static int token_same(Token a, Token b) {
    return a.start && b.start && a.len == b.len &&
           memcmp(a.start, b.start, a.len) == 0;
}
static int token_text(Token a, const char *b) {
    size_t n = strlen(b);
    return a.start && a.len == n && memcmp(a.start, b, n) == 0;
}
static const AstNode *node(const Emitter *e, u32 id) { return &e->p->nodes[id]; }
static u32 first(const Emitter *e, u32 id) { return node(e,id)->first; }
static u32 next(const Emitter *e, u32 id) { return node(e,id)->next; }
static int fail(Emitter *e, u32 id, const char *message) {
    if (!e->failed) {
        e->failed = 1;
        e->result->error = message;
        if (id < e->p->count) e->result->at = node(e,id)->tok;
    }
    return 0;
}
static int room(Emitter *e, u32 n) {
    if (e->failed) return 0;
    if (e->pos > e->cap || n > e->cap - e->pos)
        return fail(e,e->current,"ELF output capacity exceeded");
    return 1;
}
static void byte(Emitter *e, u8 value) { if (room(e,1)) e->out[e->pos++] = value; }
static void raw(Emitter *e, const void *p, u32 n) {
    if (room(e,n)) { memcpy(e->out + e->pos,p,n); e->pos += n; }
}
static void word32(Emitter *e, u32 value) {
    u32 i; for (i=0;i<4;i++) byte(e,(u8)(value >> (i*8)));
}
static void word64(Emitter *e, uint64_t value) {
    u32 i; for (i=0;i<8;i++) byte(e,(u8)(value >> (i*8)));
}
static void put16(u8 *p, uint16_t x) { p[0]=(u8)x; p[1]=(u8)(x>>8); }
static void put32(u8 *p, u32 x) { u32 i; for(i=0;i<4;i++)p[i]=(u8)(x>>(i*8)); }
static void put64(u8 *p, uint64_t x) { u32 i; for(i=0;i<8;i++)p[i]=(u8)(x>>(i*8)); }
static void patch_jump(Emitter *e,u32 at,u32 target) {
    if (!e->failed) {
        int64_t delta=(int64_t)target - ((int64_t)at + 4);
        if (at > e->pos || e->pos-at<4 || delta<INT32_MIN || delta>INT32_MAX) {
            fail(e,e->current,"relative address out of range"); return;
        }
        put32(e->out+at,(u32)(int32_t)delta);
    }
}
static void reloc(Emitter *e,u32 kind,u32 target) {
    u32 location=e->pos;
    if(e->relocation_count>=CODEGEN_MAX_RELOCS) {
        fail(e,e->current,"relocation capacity exceeded");return;
    }
    e->relocations[e->relocation_count++]=(Relocation){location,target,kind};
    if(kind==RELOC_GLOBAL)word64(e,0);else word32(e,0);
}
static u32 jump(Emitter *e,int condition) {
    u32 p;
    if(condition<0)byte(e,0xe9);
    else {byte(e,0x0f);byte(e,(u8)(0x80+condition));}
    p=e->pos;word32(e,0);return p;
}
static void trap_if(Emitter *e,int condition,u32 trap) {
    byte(e,0x0f);byte(e,(u8)(0x80+condition));reloc(e,RELOC_TRAP,trap);
}
static void mov_rax(Emitter *e,uint64_t value) {byte(e,0x48);byte(e,0xb8);word64(e,value);}
static void test_rax(Emitter *e) {raw(e,"\x48\x85\xc0",3);}
static void tick(Emitter *e) {
    raw(e,"\x4d\x85\xed",3);             /* test r13,r13 */
    trap_if(e,4,TRAP_FUEL);                 /* je */
    raw(e,"\x49\xff\xcd",3);            /* dec r13 */
}
static void global_address(Emitter *e,u32 slot) {
    byte(e,0x49);byte(e,0xbb);reloc(e,RELOC_GLOBAL,slot); /* movabs r11 */
}
static Binding *lookup(Emitter *e,Token name) {
    u32 i=e->binding_count;
    while(i--)if(token_same(e->bindings[i].name,name))return &e->bindings[i];
    return NULL;
}
static int bind(Emitter *e,Token name,u8 kind,int32_t disp,u32 global) {
    if(e->binding_count>=CODEGEN_MAX_SLOTS)
        return fail(e,e->current,"binding capacity exceeded");
    e->bindings[e->binding_count++]=(Binding){name,global,disp,kind};return 1;
}
static int function_index(Emitter *e,Token name,u32 *index) {
    u32 i;
    for(i=0;i<e->function_count;i++)
        if(token_same(node(e,e->functions[i].node)->tok,name)){*index=i;return 1;}
    return fail(e,e->current,"function not found during emission");
}
static void flag_local(Emitter *e,int32_t displacement,u8 value) {
    byte(e,0xc6);byte(e,0x85);word32(e,(u32)(displacement+8));byte(e,value);
}
static void load(Emitter *e,const Binding *b) {
    if(b->kind==2) {
        global_address(e,b->global);
        raw(e,"\x41\x80\x7b\x08\x00",5); /* cmp byte [r11+8],0 */
        trap_if(e,4,TRAP_UNINITIALIZED);
        raw(e,"\x49\x8b\x03",3);           /* mov rax,[r11] */
    } else {
        if(b->kind==0) {
            byte(e,0x80);byte(e,0xbd);word32(e,(u32)(b->displacement+8));byte(e,0);
            trap_if(e,4,TRAP_UNINITIALIZED);
        }
        byte(e,0x48);byte(e,0x8b);byte(e,0x85);word32(e,(u32)b->displacement);
    }
}
static void store(Emitter *e,const Binding *b) {
    if(b->kind==2) {
        global_address(e,b->global);
        raw(e,"\x49\x89\x03",3);           /* mov [r11],rax */
        raw(e,"\x41\xc6\x43\x08\x01",5);
    } else {
        byte(e,0x48);byte(e,0x89);byte(e,0x85);word32(e,(u32)b->displacement);
        if(b->kind==0)flag_local(e,b->displacement,1);
    }
}
static void epilogue(Emitter *e) {
    raw(e,"\x49\xff\xcc\xc9\xc3",5); /* dec r12; leave; ret */
}
static int expression(Emitter *e,u32 id,u32 depth);
static int binary(Emitter *e,u32 id,u32 depth) {
    const AstNode *n=node(e,id);
    u32 left=first(e,id),right=next(e,left),skip;
    TokKind op=n->tok.kind;
    if(!expression(e,left,depth+1))return 0;
    if(op==T_ANDAND || op==T_OROR) {
        test_rax(e);skip=jump(e,op==T_ANDAND?4:5);
        if(!expression(e,right,depth+1))return 0;
        patch_jump(e,skip,e->pos);return !e->failed;
    }
    byte(e,0x50);                          /* push rax: left operand */
    if(!expression(e,right,depth+1))return 0;
    raw(e,"\x48\x89\xc1\x58",4);      /* mov rcx,rax; pop rax */
    switch(op) {
    case T_PLUS:
        raw(e,"\x48\x01\xc8",3);trap_if(e,0,TRAP_OVERFLOW);break;
    case T_MINUS:
        raw(e,"\x48\x29\xc8",3);trap_if(e,0,TRAP_OVERFLOW);break;
    case T_STAR:
        raw(e,"\x48\x0f\xaf\xc1",4);trap_if(e,0,TRAP_OVERFLOW);break;
    case T_SLASH:case T_PERCENT:
        raw(e,"\x48\x85\xc9",3);trap_if(e,4,TRAP_DIVZERO);
        raw(e,"\x48\x83\xf9\xff",4);  /* cmp rcx,-1 */
        skip=jump(e,5);
        byte(e,0x48);byte(e,0xba);word64(e,UINT64_C(0x8000000000000000));
        raw(e,"\x48\x39\xd0",3);trap_if(e,4,TRAP_OVERFLOW);
        patch_jump(e,skip,e->pos);
        raw(e,"\x48\x99\x48\xf7\xf9",5); /* cqo; idiv rcx */
        if(op==T_PERCENT)raw(e,"\x48\x89\xd0",3);
        break;
    case T_SHL:case T_SHR:
        raw(e,"\x48\x83\xf9\x3f",4);trap_if(e,7,TRAP_SHIFT); /* unsigned >63 */
        if(op==T_SHR)raw(e,"\x48\xd3\xf8",3);
        else {
            raw(e,"\x48\x89\xc2\x48\xd3\xe0",6); /* rdx=old; shl rax,cl */
            raw(e,"\x49\x89\xc2\x49\xd3\xfa",6); /* r10=result; sar r10,cl */
            raw(e,"\x49\x39\xd2",3);trap_if(e,5,TRAP_OVERFLOW);
        }
        break;
    case T_AMP:raw(e,"\x48\x21\xc8",3);break;
    case T_PIPE:raw(e,"\x48\x09\xc8",3);break;
    case T_CARET:raw(e,"\x48\x31\xc8",3);break;
    case T_EQ:case T_NE:case T_LT:case T_LE:case T_GT:case T_GE: {
        u8 condition=op==T_EQ?4:op==T_NE?5:op==T_LT?12:op==T_LE?14:op==T_GT?15:13;
        raw(e,"\x48\x39\xc8\x0f",4);byte(e,(u8)(0x90+condition));byte(e,0xc0);
        raw(e,"\x48\x0f\xb6\xc0",4);break;
    }
    default:return fail(e,id,"binary operator not supported by native emitter");
    }
    return !e->failed;
}
static int expression(Emitter *e,u32 id,u32 depth) {
    const AstNode *n;
    Binding *b;
    u32 callee,arg,index,count=0;
    if(e->failed)return 0;
    if(!id || id>=e->p->count)return fail(e,0,"invalid expression node");
    e->current=id;
    if(depth>CHECK_MAX_DEPTH)return fail(e,id,"native expression nesting exceeded");
    n=node(e,id);
    switch(n->kind) {
    case A_INT:mov_rax(e,(uint64_t)n->tok.value);break;
    case A_NAME:
        b=lookup(e,n->tok);
        if(!b)return fail(e,id,"unbound variable during native emission");
        load(e,b);break;
    case A_BINARY:return binary(e,id,depth);
    case A_UNARY:
        if(!expression(e,first(e,id),depth+1))return 0;
        if(n->tok.kind==T_MINUS) {
            raw(e,"\x48\xf7\xd8",3);trap_if(e,0,TRAP_OVERFLOW);
        } else if(n->tok.kind==T_TILDE)raw(e,"\x48\xf7\xd0",3);
        else if(n->tok.kind==T_BANG)raw(e,"\x48\x83\xf0\x01",4);
        else return fail(e,id,"unsupported native unary operator");
        break;
    case A_CALL:
        callee=first(e,id);
        if(!function_index(e,node(e,callee)->tok,&index))return 0;
        arg=next(e,callee);
        while(arg) {
            if(count++>=CODEGEN_MAX_ARGS)return fail(e,id,"native argument limit exceeded");
            if(!expression(e,arg,depth+1))return 0;
            byte(e,0x50);
            arg=next(e,arg);
        }
        byte(e,0xe8);reloc(e,RELOC_CALL,index);
        if(count){raw(e,"\x48\x81\xc4",3);word32(e,count*8);}
        break;
    default:return fail(e,id,"expression not supported by native emitter");
    }
    return !e->failed;
}
static int statement(Emitter *e,u32 id,u32 depth);
static int block(Emitter *e,u32 id,u32 depth) {
    u32 mark=e->binding_count,child=first(e,id);
    if(depth>CHECK_MAX_DEPTH)return fail(e,id,"native block nesting exceeded");
    while(child){if(!statement(e,child,depth+1))return 0;child=next(e,child);}
    e->binding_count=mark;return !e->failed;
}
static int statement(Emitter *e,u32 id,u32 depth) {
    const AstNode *n=node(e,id);
    u32 a,b,c,branch,end,loop;
    Binding binding,*target;
    if(e->failed)return 0;
    e->current=id;
    if(depth>CHECK_MAX_DEPTH)return fail(e,id,"native statement nesting exceeded");
    tick(e);
    switch(n->kind) {
    case A_VAR:case A_CONST:
        if(e->locals_used>=CODEGEN_MAX_SLOTS)return fail(e,id,"native local slot limit exceeded");
        binding=(Binding){n->tok,0,-(int32_t)(16*(++e->locals_used)),0};
        flag_local(e,binding.displacement,0);
        a=first(e,id);b=next(e,a);
        /* Resolve the initializer in the outer scope, before adding its name. */
        if(b){if(!expression(e,b,0))return 0;store(e,&binding);}
        return bind(e,n->tok,0,binding.displacement,0);
    case A_ASSIGN:
        a=first(e,id);b=next(e,a);
        target=lookup(e,node(e,a)->tok);
        if(!target)return fail(e,id,"assignment binding missing");
        binding=*target;
        if(!expression(e,b,0))return 0;
        store(e,&binding);break;
    case A_EXPR_STMT:return expression(e,first(e,id),0);
    case A_RETURN:
        a=first(e,id);
        if(a){if(!expression(e,a,0))return 0;}else mov_rax(e,0);
        epilogue(e);break;
    case A_BLOCK:return block(e,id,depth+1);
    case A_IF:
        a=first(e,id);b=next(e,a);c=next(e,b);
        if(!expression(e,a,0))return 0;
        test_rax(e);branch=jump(e,4);
        if(!block(e,b,depth+1))return 0;
        if(c) {
            end=jump(e,-1);patch_jump(e,branch,e->pos);
            if(node(e,c)->kind==A_IF){if(!statement(e,c,depth+1))return 0;}
            else if(!block(e,c,depth+1))return 0;
            patch_jump(e,end,e->pos);
        } else patch_jump(e,branch,e->pos);
        break;
    case A_WHILE:
        a=first(e,id);b=next(e,a);loop=e->pos;tick(e);
        if(!expression(e,a,0))return 0;
        test_rax(e);branch=jump(e,4);
        if(!block(e,b,depth+1))return 0;
        end=jump(e,-1);patch_jump(e,end,loop);patch_jump(e,branch,e->pos);break;
    default:return fail(e,id,"statement not supported by native emitter");
    }
    return !e->failed;
}
static int count_locals(Emitter *e,u32 id,u32 depth,u32 *count) {
    u32 child;
    if(depth>CHECK_MAX_DEPTH)return fail(e,id,"native layout nesting exceeded");
    if(node(e,id)->kind==A_VAR || node(e,id)->kind==A_CONST) {
        if(++*count>CODEGEN_MAX_SLOTS)return fail(e,id,"native local slot limit exceeded");
    }
    for(child=first(e,id);child;child=next(e,child))
        if(!count_locals(e,child,depth+1,count))return 0;
    return 1;
}
static int collect(Emitter *e,u32 root,u32 *entry) {
    u32 id,part,argc;
    int found=0;
    for(id=first(e,root);id;id=next(e,id)) {
        if(node(e,id)->kind==A_FUNC) {
            if(e->function_count>=CODEGEN_MAX_FUNCTIONS)return fail(e,id,"native function limit exceeded");
            argc=0;part=first(e,id);
            while(part && node(e,part)->kind==A_PARAM){argc++;part=next(e,part);}
            if(argc>CODEGEN_MAX_ARGS)return fail(e,id,"native parameter limit exceeded");
            if(token_text(node(e,id)->tok,"رئيسية")) {
                if(argc || !part || node(e,part)->kind!=A_TYPE_NAME ||
                   !token_text(node(e,part)->tok,"صحيح"))
                    return fail(e,id,"entry must be رئيسية() -> صحيح");
                *entry=e->function_count;found=1;
            }
            if(part && node(e,part)->kind!=A_BLOCK)part=next(e,part);
            e->functions[e->function_count++]=(Function){id,0,argc,part};
        } else if(node(e,id)->kind==A_VAR || node(e,id)->kind==A_CONST) {
            if(e->global_count>=CODEGEN_MAX_SLOTS)return fail(e,id,"native global slot limit exceeded");
            e->global_nodes[e->global_count++]=id;
        } else return fail(e,id,"declaration not supported by native emitter");
    }
    return found || fail(e,root,"entry function رئيسية is missing");
}
static int emit_function(Emitter *e,u32 index) {
    Function *f=&e->functions[index];
    u32 locals=0,part=first(e,f->node),param=0;
    e->current=f->node;
    if(!count_locals(e,f->body,0,&locals))return 0;
    e->binding_count=e->global_count;e->locals_used=0;
    f->address=e->pos;
    raw(e,"\x49\xff\xc4\x49\x81\xfc",6);word32(e,CODEGEN_CALL_LIMIT);
    trap_if(e,7,TRAP_DEPTH);                /* inc r12; cmp r12,128; ja */
    raw(e,"\x55\x48\x89\xe5",4);
    if(locals){raw(e,"\x48\x81\xec",3);word32(e,locals*16);}
    while(part && node(e,part)->kind==A_PARAM) {
        int32_t displacement=(int32_t)(16+8*(f->argc-1-param));
        if(!bind(e,node(e,part)->tok,1,displacement,0))return 0;
        param++;part=next(e,part);
    }
    if(!block(e,f->body,0))return 0;
    /* Checker proves non-void paths return. This is the implicit void return. */
    mov_rax(e,0);epilogue(e);
    return !e->failed;
}
static void emit_traps(Emitter *e) {
    static const char *const messages[TRAP_COUNT]={
        "runtime: signed arithmetic overflow\n", "runtime: division by zero\n",
        "runtime: shift count outside 0..63\n", "runtime: execution budget exhausted\n",
        "runtime: call depth exceeded\n", "runtime: uninitialized variable\n"
    };
    u32 i,p;
    for(i=0;i<TRAP_COUNT;i++) {
        e->traps[i]=e->pos;
        byte(e,0xb8);word32(e,1);byte(e,0xbf);word32(e,2);
        raw(e,"\x48\x8d\x35",3);p=e->pos;word32(e,0);
        byte(e,0xba);word32(e,(u32)strlen(messages[i]));raw(e,"\x0f\x05",2);
        byte(e,0xbf);word32(e,120+i);byte(e,0xb8);word32(e,60);
        raw(e,"\x0f\x05\x0f\x0b",4);
        patch_jump(e,p,e->pos);raw(e,messages[i],(u32)strlen(messages[i]));
    }
}
static void program_header(u8 *p,u32 type,u32 flags,uint64_t offset,
                           uint64_t address,uint64_t filesz,uint64_t memsz,uint64_t align) {
    put32(p,type);put32(p+4,flags);put64(p+8,offset);put64(p+16,address);
    put64(p+24,address);put64(p+32,filesz);put64(p+40,memsz);put64(p+48,align);
}
static int finish_elf(Emitter *e) {
    u32 code_end=e->pos,data_offset,i,data_size=e->global_count*16;
    if(!data_size)data_size=16;
    data_offset=(code_end+4095U)&~4095U;
    while(e->pos<data_offset && !e->failed)byte(e,0);
    for(i=0;i<data_size && !e->failed;i++)byte(e,0);
    if(e->failed)return 0;
    for(i=0;i<e->relocation_count;i++) {
        Relocation r=e->relocations[i];
        if(r.kind==RELOC_GLOBAL) {
            put64(e->out+r.location,(uint64_t)ELF_BASE+data_offset+16*r.target);
        } else patch_jump(e,r.location,r.kind==RELOC_CALL ?
                          e->functions[r.target].address:e->traps[r.target]);
    }
    if(e->failed)return 0;
    memcpy(e->out,"\x7f" "ELF",4);
    e->out[4]=2;e->out[5]=1;e->out[6]=1;
    put16(e->out+16,2);put16(e->out+18,62);put32(e->out+20,1);
    put64(e->out+24,ELF_BASE+CODE_START);put64(e->out+32,64);
    put16(e->out+52,64);put16(e->out+54,56);put16(e->out+56,3);
    program_header(e->out+64,1,5,0,ELF_BASE,code_end,code_end,4096);
    program_header(e->out+120,1,6,data_offset,ELF_BASE+data_offset,data_size,data_size,4096);
    program_header(e->out+176,0x6474e551U,6,0,0,0,0,16);
    e->result->size=e->pos;return 1;
}
int codegen_elf64(const Parser *parser,u32 root,u8 *output,u32 capacity,CodegenResult *result) {
    Emitter *e;
    Checker *checker;
    u32 entry=0,i,id,init;
    int ok=0;
    if(!result)return 0;
    memset(result,0,sizeof(*result));
    if(!parser || !output || !parser->nodes || !root || root>=parser->count ||
       parser->count>CODEGEN_MAX_NODES || capacity<CODE_START || parser->nodes[root].kind!=A_PROGRAM) {
        result->error="invalid compiler input or capacity";return 0;
    }
    checker=(Checker *)malloc(sizeof(*checker));
    if(!checker){result->error="out of memory";return 0;}
    checker_init(checker,parser);
    if(!checker_run(checker,root)) {
        result->error=checker->message?checker->message:"semantic check failed";
        result->at=checker->at;result->semantic_error=1;free(checker);return 0;
    }
    free(checker);
    e=(Emitter *)calloc(1,sizeof(*e));
    if(!e){result->error="out of memory";return 0;}
    e->p=parser;e->out=output;e->cap=capacity;e->pos=CODE_START;e->result=result;e->current=root;
    memset(output,0,CODE_START);
    if(!collect(e,root,&entry))goto done;
    raw(e,"\x45\x31\xe4",3);              /* xor r12d,r12d */
    byte(e,0x41);byte(e,0xbd);word32(e,CODEGEN_FUEL);
    raw(e,"\x55\x48\x89\xe5",4);        /* frame for initializer expressions */
    for(i=0;i<e->global_count;i++) {
        Binding global;
        id=e->global_nodes[i];e->current=id;tick(e);
        init=next(e,first(e,id));
        if(init){if(!expression(e,init,0))goto done;}else mov_rax(e,0);
        global=(Binding){node(e,id)->tok,i,0,2};store(e,&global);
        if(!bind(e,global.name,2,0,i))goto done;
    }
    byte(e,0xe8);reloc(e,RELOC_CALL,entry);
    raw(e,"\x48\x89\xc7",3);byte(e,0xb8);word32(e,60);raw(e,"\x0f\x05\x0f\x0b",4);
    for(i=0;i<e->function_count;i++)if(!emit_function(e,i))goto done;
    emit_traps(e);ok=finish_elf(e);
done:
    free(e);return ok;
}
