/* Independent scalar C oracle. Test-only. Signed INT8 -> INT32; no SIMD kernel reuse. */
#include <stdint.h>
#include <string.h>
extern long niyah_sys6(long,long,long,long,long,long,long);
static int exact(void *p,unsigned n){unsigned char *s=p;while(n){long v=niyah_sys6(0,0,(long)s,n,0,0,0);if(v==-4)continue;if(v<=0)return 0;s+=v;n-=(unsigned)v;}return 1;}
static union { signed char b[1024];int32_t c[256]; } values[64];
static uint32_t plan[128][4],header[4],types[64];
static unsigned char input[64*1024];static int32_t output[128][256];
int main(void){
    if(!exact(header,16))return 65;
    uint32_t nv=header[0],no=header[1],ni=header[2],ne=header[3];
    if(!nv||nv>64||!no||no>128||ni>64||!ne||ne>128)return 65;
    if(!exact(plan,no*16)||!exact(input,ni*1024))return 65;
    unsigned char extra;if(niyah_sys6(0,0,(long)&extra,1,0,0,0)!=0)return 65;
    unsigned in=0,out=0,defined=0;
    for(unsigned op=0;op<no;op++){
        unsigned k=plan[op][0],d=plan[op][1],a=plan[op][2],b=plan[op][3];
        if(d>=nv)return 65;
        if(k==1||k==2){if(d!=defined++||in>=ni)return 65;memcpy(values[d].b,input+1024*in++,1024);types[d]=k;}
        else if(k==3){
            if(d!=defined++||a>=d||b>=d||types[a]!=1||types[b]!=2)return 65;types[d]=3;
            for(unsigned i=0;i<16;i++)for(unsigned j=0;j<16;j++){
                int64_t sum=0;for(unsigned k2=0;k2<64;k2++)sum+=(int64_t)values[a].b[i*64+k2]*(int64_t)values[b].b[k2*16+j];
                if(sum<INT32_MIN||sum>INT32_MAX)return 65;
                values[d].c[i*16+j]=(int32_t)sum;
            }
        }else if(k==4){if(d>=defined||types[d]!=3||out>=ne)return 65;memcpy(output[out++],values[d].c,1024);}
        else return 65;
    }
    if(in!=ni||out!=ne||defined!=nv)return 65;
    const unsigned char *s=(const unsigned char *)output;unsigned remain=out*1024;
    while(remain){long n=niyah_sys6(1,1,(long)s,remain,0,0,0);if(n==-4)continue;if(n<=0)return 74;s+=n;remain-=(unsigned)n;}
    return 0;
}
