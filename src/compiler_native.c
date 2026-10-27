/* Private freestanding support for the existing C emitter. No linked libc. */
#include <stddef.h>
#include <stdint.h>
long niyah_sys6(long n,long a,long b,long c,long d,long e,long f){
    register long r10 __asm__("r10")=d;register long r8 __asm__("r8")=e;register long r9 __asm__("r9")=f;long v;
    __asm__ volatile("syscall":"=a"(v):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory","cc");return v;
}
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];return 0;}
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
void *malloc(size_t n){
    if(n>SIZE_MAX-16)return 0;
    size_t bytes=n+16;
    long v=niyah_sys6(9,0,(long)bytes,3,34,-1,0);if(v<0)return 0;
    size_t *p=(size_t *)(uintptr_t)v;p[0]=bytes;p[1]=0;return (unsigned char *)p+16;
}
void *calloc(size_t count,size_t size){if(size && count>SIZE_MAX/size)return 0;return malloc(count*size);}
void free(void *p){if(p){size_t *base=(size_t *)((unsigned char *)p-16);niyah_sys6(11,(long)base,(long)base[0],0,0,0,0);}}
