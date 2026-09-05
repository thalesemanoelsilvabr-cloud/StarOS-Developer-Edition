/* kprintf.c — printf bare-metal simples e estável */
#include <kernel/types.h>
#include <drivers/terminal.h>

typedef void (*putc_fn)(char);

static void emit_pad(putc_fn out, char pch, int n){
    while(n-- > 0) out(pch);
}

static int count_digits(u32 v, int base){
    int n=1;
    while(v >= (u32)base){ v/=(u32)base; n++; }
    return n;
}

static void emit_uint(putc_fn out, u32 v, int base, int pad, char pch,
                      int upper, int left){
    char buf[32]; int i=0;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if(base < 2 || base > 16) base = 10;
    if(v==0){ buf[i++]='0'; }
    else { while(v){ buf[i++]=dig[v%(u32)base]; v/=(u32)base; } }
    int n = i;
    if(!left) emit_pad(out, pch, pad - n);
    while(i-->0) out(buf[i]);
    if(left) emit_pad(out, ' ', pad - n);
}

static void emit_str(putc_fn out, const char* s, int pad, int left){
    int n=0; while(s[n]) n++;
    if(!left) emit_pad(out, ' ', pad - n);
    while(*s) out(*s++);
    if(left) emit_pad(out, ' ', pad - n);
}

static void kvformat(putc_fn out, const char* fmt, __builtin_va_list ap){
    for(const char* p=fmt; *p; p++){
        if(*p!='%'){ out(*p); continue; }
        p++;
        int left=0, pad=0; char pch=' ';
        if(*p=='-'){ left=1; p++; }
        if(*p=='0'){ pch='0'; p++; }
        while(*p>='0'&&*p<='9'){ pad=pad*10+(*p++)-'0'; }
        switch(*p){
            case 'd':{
                int v=__builtin_va_arg(ap,int);
                int neg = v<0;
                u32 uv = neg ? (u32)(-(v+1))+1u : (u32)v;
                if(!neg){ emit_uint(out,uv,10,pad,pch,0,left); break; }
                if(pch=='0' && !left){
                    out('-');
                    emit_uint(out,uv,10,pad-1,pch,0,left);
                } else {
                    if(!left) emit_pad(out,' ',pad - 1 - count_digits(uv,10));
                    out('-');
                    emit_uint(out,uv,10,0,pch,0,0);
                    if(left) emit_pad(out,' ',pad - 1 - count_digits(uv,10));
                }
                break;
            }
            case 'u': emit_uint(out,__builtin_va_arg(ap,u32),10,pad,pch,0,left); break;
            case 'x': emit_uint(out,__builtin_va_arg(ap,u32),16,pad,pch,0,left); break;
            case 'X': emit_uint(out,__builtin_va_arg(ap,u32),16,pad,pch,1,left); break;
            case 'p':
                out('0'); out('x');
                emit_uint(out,__builtin_va_arg(ap,u32),16,8,'0',0,0);
                break;
            case 'c': out((char)__builtin_va_arg(ap,int)); break;
            case 's':{
                const char* s=__builtin_va_arg(ap,const char*);
                if(!s) s="(null)";
                emit_str(out,s,pad,left);
                break;
            }
            case '%': out('%'); break;
            case 0:   return;
            default:  out('?'); break;
        }
    }
}

static void term_putc(char c){ term_write_char(c); }

void kprintf(const char* fmt, ...){
    __builtin_va_list ap;
    __builtin_va_start(ap,fmt);
    kvformat(term_putc, fmt, ap);
    __builtin_va_end(ap);
}

/* ksnprintf — escreve em buffer */
static u32   buf_pos;
static char* ksnbuf;
static u32   ksnmax;

static void snbuf_putc(char c){ if(buf_pos + 1 < ksnmax) ksnbuf[buf_pos++]=c; }

void ksnprintf(char* buf, u32 maxlen, const char* fmt, ...){
    if(!buf || !maxlen) return;
    ksnbuf=buf; ksnmax=maxlen; buf_pos=0;
    __builtin_va_list ap;
    __builtin_va_start(ap,fmt);
    kvformat(snbuf_putc, fmt, ap);
    __builtin_va_end(ap);
    ksnbuf[buf_pos]=0;
}
