#include "eval.h"
static int same(Token t,const char *s){u32 i=0;while(s[i]){if(i>=t.len||t.start[i]!=(u8)s[i])return 0;i++;}return i==t.len;}
static int err(EvalResult *r,const AstNode *n,const char *s){r->error=s;r->at=n->tok;return 0;}
static int expression(const Parser *p,u32 id,u32 depth,i64 *v,EvalResult *r){
    const AstNode *n,*l; i64 a,b,z;u32 x,y;
    if(!id||id>=p->count)return 0;
    n=&p->nodes[id];
    if(depth>128)return err(r,n,"expression nesting exceeded");
    if(n->kind==A_INT){*v=n->tok.value;return 1;}
    if(n->kind==A_UNARY){
        x=n->first;
        if(!x||p->nodes[x].next||!expression(p,x,depth+1,&a,r))return 0;
        if(n->tok.kind==T_MINUS){
            if(__builtin_sub_overflow((i64)0,a,&z))return err(r,n,"integer overflow");
            *v=z;return 1;
        }
        if(n->tok.kind==T_TILDE){*v=~a;return 1;}
        return err(r,n,"unsupported unary operator");
    }
    if(n->kind!=A_BINARY)return err(r,n,"only constant integer expressions can execute");
    x=n->first;
    if(!x||!(y=p->nodes[x].next)||p->nodes[y].next)return err(r,n,"invalid binary AST");
    l=&p->nodes[x];
    if(!expression(p,x,depth+1,&a,r)||!expression(p,y,depth+1,&b,r))return 0;
    (void)l;
    switch(n->tok.kind){
        case T_PLUS: if(__builtin_add_overflow(a,b,&z))return err(r,n,"integer overflow");break;
        case T_MINUS: if(__builtin_sub_overflow(a,b,&z))return err(r,n,"integer overflow");break;
        case T_STAR: if(__builtin_mul_overflow(a,b,&z))return err(r,n,"integer overflow");break;
        case T_SLASH:
            if(!b)return err(r,n,"division by zero");
            if(a==(-9223372036854775807L-1)&&b==-1)return err(r,n,"integer overflow");
            z=a/b;break;
        case T_PERCENT:
            if(!b)return err(r,n,"modulo by zero");
            if(a==(-9223372036854775807L-1)&&b==-1)return err(r,n,"integer overflow");
            z=a%b;break;
        default:return err(r,n,"unsupported operator in constant execution");
    }
    *v=z;return 1;
}
int eval_constant_main(const Parser *p,u32 root,EvalResult *r){
    u32 fn,type,body,ret,ex; const AstNode *n;
    if(!r)return 0;
    *r=(EvalResult){0};
    if(!p||!root||root>=p->count)return 0;
    n=&p->nodes[root];if(n->kind!=A_PROGRAM)return err(r,n,"invalid program");
    fn=n->first;
    if(!fn||p->nodes[fn].next)return err(r,n,"expected exactly one function");
    n=&p->nodes[fn];if(n->kind!=A_FUNC||!same(n->tok,"رئيسية"))return err(r,n,"expected function رئيسية");
    type=n->first;
    if(!type)return err(r,n,"expected return type and body");
    if(p->nodes[type].kind!=A_TYPE_NAME||!same(p->nodes[type].tok,"صحيح"))
        return err(r,&p->nodes[type],"main must return صحيح and have no parameters");
    body=p->nodes[type].next;
    if(!body||p->nodes[body].next||p->nodes[body].kind!=A_BLOCK)
        return err(r,n,"expected main block");
    ret=p->nodes[body].first;
    if(!ret||p->nodes[ret].next||p->nodes[ret].kind!=A_RETURN)
        return err(r,&p->nodes[body],"only one return statement is supported");
    ex=p->nodes[ret].first;
    if(!ex||p->nodes[ex].next)return err(r,&p->nodes[ret],"expected one return expression");
    return expression(p,ex,0,&r->value,r);
}
