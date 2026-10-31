#include "tensor_normalize.h"
#include <string.h>
#include <niyah_unicode_nfkc.h>
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
enum { NORM_MAX_CP=1024, NORM_MAX_BYTES=512 };
static u32 combining(u32 cp) {
    u32 lo=0,hi=(u32)COUNT(norm_ccc);
    while(lo<hi){u32 m=lo+(hi-lo)/2;if(norm_ccc[m].cp<cp)lo=m+1;else hi=m;}
    return lo<COUNT(norm_ccc)&&norm_ccc[lo].cp==cp?norm_ccc[lo].cc:0;
}
static int is_mark(u32 cp) {
    u32 lo=0,hi=(u32)COUNT(norm_marks);
    while(lo<hi){u32 m=lo+(hi-lo)/2;if(norm_marks[m].hi<cp)lo=m+1;else hi=m;}
    return lo<COUNT(norm_marks)&&norm_marks[lo].lo<=cp;
}
static int in_ranges(u32 cp,const NormRange *ranges,u32 count) {
    u32 lo=0,hi=count;
    while(lo<hi){u32 m=lo+(hi-lo)/2;if(ranges[m].hi<cp)lo=m+1;else hi=m;}
    return lo<count&&ranges[lo].lo<=cp;
}
int tensor_arabic_letter(u32 cp){return in_ranges(cp,norm_arabic_letters,(u32)COUNT(norm_arabic_letters));}
int tensor_arabic_mark(u32 cp){return in_ranges(cp,norm_arabic_marks,(u32)COUNT(norm_arabic_marks));}
static u32 composition(u32 a,u32 b) {
    if(a>=0x1100&&a<0x1113&&b>=0x1161&&b<0x1176)
        return 0xAC00+((a-0x1100)*21+b-0x1161)*28;
    if(a>=0xAC00&&a<=0xD7A3&&(a-0xAC00)%28==0&&b>0x11A7&&b<0x11C3)
        return a+b-0x11A7;
    u64 key=((u64)a<<21)|b;u32 lo=0,hi=(u32)COUNT(norm_compose);
    while(lo<hi){u32 m=lo+(hi-lo)/2;if(norm_compose[m].key<key)lo=m+1;else hi=m;}
    return lo<COUNT(norm_compose)&&norm_compose[lo].key==key?norm_compose[lo].cp:0;
}
static int decode(const u8 *s,u32 len,u32 *at,u32 *cp) {
    if(*at>=len)return 0;
    u32 v=s[(*at)++],n=0,min=0;
    if(v<128){*cp=v;return 1;}
    if(v>=0xC2&&v<=0xDF){v&=31;n=1;min=128;}
    else if(v>=0xE0&&v<=0xEF){v&=15;n=2;min=2048;}
    else if(v>=0xF0&&v<=0xF4){v&=7;n=3;min=65536;}
    else return 0;
    if(n>len-*at)return 0;
    while(n--){u32 c=s[(*at)++];if((c&0xC0)!=0x80)return 0;v=(v<<6)|(c&63);}
    if(v<min||v>0x10FFFF||(v>=0xD800&&v<=0xDFFF))return 0;
    *cp=v;return 1;
}
static int append(u32 *buf,u32 *n,u32 cp) {
    if(*n==NORM_MAX_CP)return 0;
    u32 pos=(*n)++,cc=combining(cp);
    if(cc){while(pos&&combining(buf[pos-1])>cc){buf[pos]=buf[pos-1];pos--;}}
    buf[pos]=cp;return 1;
}
static int decompose(u32 *buf,u32 *n,u32 cp) {
    if(cp>=0xAC00&&cp<=0xD7A3){
        u32 s=cp-0xAC00;
        return append(buf,n,0x1100+s/588)&&append(buf,n,0x1161+(s%588)/28)&&
               (s%28==0||append(buf,n,0x11A7+s%28));
    }
    u32 lo=0,hi=(u32)COUNT(norm_decomp);
    while(lo<hi){u32 m=lo+(hi-lo)/2;if(norm_decomp[m].cp<cp)lo=m+1;else hi=m;}
    if(lo==COUNT(norm_decomp)||norm_decomp[lo].cp!=cp)return append(buf,n,cp);
    NormDecomp d=norm_decomp[lo];
    for(u32 i=0;i<d.len;i++)if(!append(buf,n,norm_data[d.off+i]))return 0;
    return 1;
}
const char *tensor_unicode_version(void){return NIYAH_UNICODE_VERSION;}
int tensor_normalize(const u8 *s,u32 length,u8 *out,u32 capacity,u32 *outlen) {
    if(!s||!out||!outlen||length>128)return 0;
    u32 buf[NORM_MAX_CP],n=0,at=0,cp;
    while(at<length){if(!decode(s,length,&at,&cp)||!decompose(buf,&n,cp))return 0;}
    /* Canonical composition, including Hangul and canonical blocking. */
    u32 end=0,starter=0,last_cc=0;int have_starter=0;
    for(u32 i=0;i<n;i++){
        cp=buf[i];u32 cc=combining(cp),composed=0;
        if(have_starter&&(last_cc<cc||last_cc==0))composed=composition(buf[starter],cp);
        if(composed){buf[starter]=composed;continue;}
        buf[end]=cp;
        if(!cc){starter=end;have_starter=1;}
        last_cc=cc;end++;
    }
    u8 tmp[NORM_MAX_BYTES];u32 bytes=0;
    if(capacity>NORM_MAX_BYTES)capacity=NORM_MAX_BYTES;
    for(u32 i=0;i<end;i++){
        cp=buf[i];
        if(cp==0x622||cp==0x623||cp==0x625||cp==0x671)cp=0x627;
        if(is_mark(cp))continue;
        u32 width=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
        if(width>capacity-bytes)return 0;
        if(width==1)tmp[bytes++]=(u8)cp;
        else {
            tmp[bytes++]=(u8)((width==2?0xC0:width==3?0xE0:0xF0)|(cp>>(6*(width-1))));
            for(u32 j=width-1;j;j--)tmp[bytes++]=(u8)(0x80|((cp>>(6*(j-1)))&63));
        }
    }
    memcpy(out,tmp,bytes);*outlen=bytes;return 1;
}
