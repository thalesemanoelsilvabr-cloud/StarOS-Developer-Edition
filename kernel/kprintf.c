/* kprintf.c — printf bare-metal simples e estável */
#include <kernel/types.h>
#include <drivers/terminal.h>

static void print_uint(u32 v, int base, int pad, char pch, int upper){
    char buf[32]; int i=0;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if(v==0){ buf[i++]='0'; }
    else { while(v){ buf[i++]=dig[v%base]; v/=base; } }
    while(i<pad) buf[i++]=pch;
    while(i-->0) term_write_char(buf[i]);
}

void kprintf(const char* fmt, ...){
    __builtin_va_list ap;
    __builtin_va_start(ap,fmt);
    for(const char* p=fmt; *p; p++){
        if(*p!='%'){ term_write_char(*p); continue; }
        p++;
        int pad=0; char pch=' ';
        if(*p=='0'){ pch='0'; p++; }
        while(*p>='0'&&*p<='9'){ pad=pad*10+(*p++)-'0'; }
        switch(*p){
            case 'd':{int v=__builtin_va_arg(ap,int);if(v<0){term_write_char('-');v=-v;}print_uint((u32)v,10,pad,pch,0);break;}
            case 'u': print_uint(__builtin_va_arg(ap,u32),10,pad,pch,0); break;
            case 'x': print_uint(__builtin_va_arg(ap,u32),16,pad,pch,0); break;
            case 'X': print_uint(__builtin_va_arg(ap,u32),16,pad,pch,1); break;
            case 'p': term_write_char('0'); term_write_char('x');
                       print_uint(__builtin_va_arg(ap,u32),16,8,'0',0); break;
            case 'c': term_write_char((char)__builtin_va_arg(ap,int)); break;
            case 's':{const char* s=__builtin_va_arg(ap,const char*); if(!s) s="(null)"; while(*s) term_write_char(*s++); break;}
            case '%': term_write_char('%'); break;
            default:   term_write_char('?'); break;
        }
    }
    __builtin_va_end(ap);
}

/* ksnprintf — escreve em buffer */
static int buf_pos;
static char* ksnbuf;
static u32   ksnmax;
static void snbuf_putc(char c){ if((u32)buf_pos<ksnmax-1) ksnbuf[buf_pos++]=c; }

void ksnprintf(char* buf, u32 maxlen, const char* fmt, ...){
    ksnbuf=buf; ksnmax=maxlen; buf_pos=0;
    __builtin_va_list ap;
    __builtin_va_start(ap,fmt);
    for(const char* p=fmt;*p;p++){
        if(*p!='%'){ snbuf_putc(*p); continue; }
        p++;
        int pad=0; char pch=' ';
        if(*p=='0'){ pch='0'; p++; }
        while(*p>='0'&&*p<='9'){ pad=pad*10+(*p++)-'0'; }
        switch(*p){
            case 'd':{int v=__builtin_va_arg(ap,int);
                char tmp[16]; int i=0,neg=0;
                if(v<0){neg=1;v=-v;} if(v==0) tmp[i++]='0';
                while(v){tmp[i++]='0'+v%10;v/=10;}
                if(neg) tmp[i++]='-';
                while(i<pad) {snbuf_putc(pch);} /* pad simplificado */
                while(i-->0) snbuf_putc(tmp[i]); break;}
            case 'u':{u32 v=__builtin_va_arg(ap,u32);
                char tmp[16]; int i=0;
                if(v==0) tmp[i++]='0'; while(v){tmp[i++]='0'+v%10;v/=10;}
                while(i-->0) snbuf_putc(tmp[i]); break;}
            case 'x':{u32 v=__builtin_va_arg(ap,u32);
                char tmp[16]; int i=0; const char* h="0123456789abcdef";
                if(v==0) tmp[i++]='0'; while(v){tmp[i++]=h[v&0xF];v>>=4;}
                while(i<pad){snbuf_putc(pch);pad--;}
                while(i-->0) snbuf_putc(tmp[i]); break;}
            case 's':{const char* s=__builtin_va_arg(ap,const char*);
                if(!s)s="(null)"; while(*s) snbuf_putc(*s++); break;}
            case 'c': snbuf_putc((char)__builtin_va_arg(ap,int)); break;
            case '%': snbuf_putc('%'); break;
        }
    }
    __builtin_va_end(ap);
    ksnbuf[buf_pos]=0;
}
