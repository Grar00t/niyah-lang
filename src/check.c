/* M2 slice: strict scalar types and lexical bindings; no libc. */
#include "check.h"
static int eq(Token t, const char *s) {
    u32 i=0;
    if (!t.start) return 0;
    while (s[i]) { if (i>=t.len || t.start[i]!=(u8)s[i]) return 0; i++; }
    return i==t.len;
}
static int same(Token a, Token b) {
    u32 i;
    if (a.len!=b.len || !a.start || !b.start) return 0;
    for(i=0;i<a.len;i++) if(a.start[i]!=b.start[i]) return 0;
    return 1;
}
const char *checker_type_name(CheckType t) {
    switch(t) {case TY_I64:return "صحيح";case TY_BYTE:return "بايت";
    case TY_BOOL:return "منطقي";case TY_VOID:return "void";default:return "error";}
}
/* Reset only live metadata. Unused symbol slots are never read (count=0).
 * A whole-struct zero init can emit a libc memset on freestanding targets. */
void checker_init(Checker *c,const Parser *p){
    c->parser=p;
    c->count=0;
    c->scope=0;
    c->errors=0;
    c->checked_nodes=0;
    c->message=0;
    c->at.kind=T_EOF;
    c->at.line=0;
    c->at.col=0;
    c->at.start=0;
    c->at.len=0;
    c->at.value=0;
}
static CheckType error(Checker *c,u32 id,const char *why) {
    if(!c->errors){c->message=why;c->at=c->parser->nodes[id].tok;}
    c->errors++;
    return TY_ERROR;
}
static u32 first(Checker *c,u32 id){return c->parser->nodes[id].first;}
static u32 next(Checker *c,u32 id){return c->parser->nodes[id].next;}
static const AstNode *at(Checker *c,u32 id){return &c->parser->nodes[id];}
static CheckType declared(Checker *c,u32 node,u32 depth){
    const AstNode *a;
    if (!node || depth>CHECK_MAX_DEPTH) return error(c,node?node:1,"type nesting exceeded");
    a=at(c,node);
    if(a->kind==A_TYPE_NAME) {
        if(eq(a->tok,"صحيح"))return TY_I64;
        if(eq(a->tok,"بايت"))return TY_BYTE;
        if(eq(a->tok,"منطقي"))return TY_BOOL;
        return error(c,node,"unknown or unsupported type");
    }
    return error(c,node,"pointer, array and struct types require a later M2 stage");
}
static int insert(Checker *c,u32 id,CheckType type,u8 is_const,u8 is_function){
    u32 i;
    Token name=at(c,id)->tok;
    for(i=c->count;i>0;i--){
        CheckSymbol *s=&c->symbols[i-1];
        if(s->scope<c->scope)break;
        if(same(s->name,name)){error(c,id,"duplicate symbol in scope");return 0;}
    }
    if(c->count==CHECK_MAX_SYMBOLS){error(c,id,"symbol capacity exceeded");return 0;}
    c->symbols[c->count++]=(CheckSymbol){name,type,c->scope,is_const,is_function};
    return 1;
}
static CheckSymbol *find(Checker *c,Token name){
    u32 i=c->count;
    while(i--)if(same(c->symbols[i].name,name))return &c->symbols[i];
    return 0;
}
static CheckType expr(Checker *c,u32 id,u32 depth);
static CheckType binary(Checker *c,u32 id,u32 depth) {
    const AstNode *n=at(c,id);
    u32 a=first(c,id),b=a?next(c,a):0;
    CheckType x=a?expr(c,a,depth+1):TY_ERROR;
    CheckType y=b?expr(c,b,depth+1):TY_ERROR;
    if(x==TY_ERROR||y==TY_ERROR)return TY_ERROR;
    switch(n->tok.kind){
        case T_PLUS:case T_MINUS:case T_STAR:case T_SLASH:case T_PERCENT:
        case T_SHL:case T_SHR:case T_PIPE:case T_CARET:case T_AMP:
            if(x!=TY_I64||y!=TY_I64)return error(c,id,"integer operands required");
            return TY_I64;
        case T_LT:case T_LE:case T_GT:case T_GE:
            if(x!=TY_I64||y!=TY_I64)return error(c,id,"integer comparison required");
            return TY_BOOL;
        case T_EQ:case T_NE:
            if(x!=y)return error(c,id,"comparison type mismatch");
            return TY_BOOL;
        case T_ANDAND:case T_OROR:
            if(x!=TY_BOOL||y!=TY_BOOL)return error(c,id,"boolean operands required");
            return TY_BOOL;
        default:return error(c,id,"unsupported binary operator");
    }
}
static CheckType expr(Checker *c,u32 id,u32 depth){
    const AstNode *n;
    u32 a;
    CheckSymbol *symbol;
    if(!id)return TY_ERROR;
    if(depth>CHECK_MAX_DEPTH)return error(c,id,"expression nesting exceeded");
    c->checked_nodes++;
    n=at(c,id);
    switch(n->kind){
        case A_INT:return TY_I64;
        case A_NAME:
            symbol=find(c,n->tok);
            if(!symbol)return error(c,id,"undefined identifier");
            if(symbol->is_function)return error(c,id,"function value requires call typing");
            return symbol->type;
        case A_BINARY:return binary(c,id,depth);
        case A_UNARY:
            a=first(c,id);
            { CheckType t=expr(c,a,depth+1);
              if(t==TY_ERROR)return t;
              if(n->tok.kind==T_BANG){
                  if(t!=TY_BOOL)return error(c,id,"boolean operand required");
                  return TY_BOOL;
              }
              if(n->tok.kind==T_MINUS||n->tok.kind==T_TILDE){
                  if(t!=TY_I64)return error(c,id,"integer operand required");
                  return TY_I64;
              }
              return error(c,id,"pointer unary operators not implemented"); }
        case A_STRING:return error(c,id,"string type not implemented");
        default:return error(c,id,"expression form not implemented in M2 slice");
    }
}
static void block(Checker *c,u32 id,u32 depth,CheckType result_type);
static void statement(Checker *c,u32 id,u32 depth,CheckType result_type){
    const AstNode *n=at(c,id);
    u32 a,b;
    CheckType declared_type,actual;
    CheckSymbol *symbol;
    if(depth>CHECK_MAX_DEPTH){error(c,id,"statement nesting exceeded");return;}
    switch(n->kind){
        case A_VAR:case A_CONST:
            a=first(c,id);b=a?next(c,a):0;
            declared_type=declared(c,a,0);
            if(b){actual=expr(c,b,0);if(declared_type!=TY_ERROR && actual!=TY_ERROR && actual!=declared_type)error(c,id,"initializer type mismatch");}
            if(declared_type!=TY_ERROR)insert(c,id,declared_type,n->kind==A_CONST,0);
            return;
        case A_ASSIGN:
            a=first(c,id);b=a?next(c,a):0;
            if(!a||at(c,a)->kind!=A_NAME){error(c,id,"invalid assignment target");return;}
            symbol=find(c,at(c,a)->tok);
            if(!symbol){error(c,a,"undefined identifier");return;}
            if(symbol->is_function){error(c,id,"cannot assign to function");return;}
            if(symbol->is_const){error(c,id,"cannot assign to constant");return;}
            actual=expr(c,b,0);
            if(actual!=TY_ERROR && actual!=symbol->type)error(c,id,"assignment type mismatch");
            return;
        case A_EXPR_STMT:expr(c,first(c,id),0);return;
        case A_RETURN:
            a=first(c,id);
            actual=a?expr(c,a,0):TY_VOID;
            if(actual!=TY_ERROR && actual!=result_type)error(c,id,"return type mismatch");
            return;
        case A_BLOCK:block(c,id,depth+1,result_type);return;
        case A_IF:
            a=first(c,id);
            if(expr(c,a,0)!=TY_BOOL)error(c,id,"if condition must be boolean");
            b=next(c,a);
            if(b)block(c,b,depth+1,result_type);
            if(b&&(a=next(c,b))) {
                if(at(c,a)->kind==A_IF)statement(c,a,depth+1,result_type);
                else block(c,a,depth+1,result_type);
            }
            return;
        case A_WHILE:
            a=first(c,id);
            if(expr(c,a,0)!=TY_BOOL)error(c,id,"while condition must be boolean");
            if((b=next(c,a)))block(c,b,depth+1,result_type);
            return;
        default:error(c,id,"statement form not supported");return;
    }
}
static void block(Checker *c,u32 id,u32 depth,CheckType result_type){
    u32 mark=c->count, a;
    if(depth>CHECK_MAX_DEPTH){error(c,id,"block nesting exceeded");return;}
    c->scope++;
    a=first(c,id);
    while(a){statement(c,a,depth,result_type);a=next(c,a);}
    c->count=mark;
    c->scope--;
}
int checker_run(Checker *c,u32 root){
    u32 id,part,body,p;
    CheckType ret;
    if(!c||!c->parser||!root||root>=c->parser->count || at(c,root)->kind!=A_PROGRAM)return 0;
    /* Reserve top-level function names in the same namespace as globals.
     * Function bodies are checked later; call typing remains unsupported. */
    id=first(c,root);
    while(id){
        const AstNode *n=at(c,id);
        if(n->kind==A_VAR || n->kind==A_CONST)statement(c,id,0,TY_VOID);
        else if(n->kind==A_FUNC)insert(c,id,TY_VOID,1,1);
        else error(c,id,"struct checking not implemented in M2 slice");
        id=next(c,id);
    }
    id=first(c,root);
    while(id){
        if(at(c,id)->kind==A_FUNC){
            u32 mark=c->count;
            c->scope++;
            part=first(c,id);
            while(part&&at(c,part)->kind==A_PARAM){
                p=first(c,part);
                ret=declared(c,p,0);
                if(ret!=TY_ERROR)insert(c,part,ret,0,0);
                part=next(c,part);
            }
            ret=TY_VOID;
            if(part&&at(c,part)->kind!=A_BLOCK) {ret=declared(c,part,0);part=next(c,part);}
            body=part;
            if(body&&at(c,body)->kind==A_BLOCK)block(c,body,0,ret);
            else error(c,id,"function body missing");
            c->count=mark;
            c->scope--;
        }
        id=next(c,id);
    }
    return c->errors==0;
}
