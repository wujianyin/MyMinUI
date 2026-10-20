#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>

/* Passive ioctl timing tracer: LD_PRELOAD / ld.so.preload shim, active only in minarch.elf.
   Records (request, start, end) of every ioctl made on /dev/disp or /dev/fb* style fds that
   matter for frame presentation, and writes per-request statistics. No behaviour change. */
#define MAXREC 400000
typedef struct { uint32_t req; uint32_t pad; uint64_t t0, t1; } rec_t;
static rec_t *recs; static size_t nrec; static int enabled; static uint64_t first_ns;
static int (*real_ioctl)(int, unsigned long, ...);
static char outpath[256];
extern char *program_invocation_short_name;

static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec; }
static int cmpd(const void*a,const void*b){ double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y; }
static double pct(double *v,size_t n,double p){ if(!n) return 0; return v[(size_t)((n-1)*p)]; }

static void dump(void){
    if(!enabled||nrec<10) return;
    FILE *f=fopen(outpath,"w"); if(!f) return;
    uint32_t reqs[32]; int nr=0;
    for(size_t i=0;i<nrec;i++){ int k; for(k=0;k<nr;k++) if(reqs[k]==recs[i].req) break; if(k==nr&&nr<32) reqs[nr++]=recs[i].req; }
    uint64_t skip_until = first_ns + 3000000000ull; /* ignore first 3s */
    fprintf(f,"# pantrace %s records=%zu span=%.1fs (first 3s ignored)\n", program_invocation_short_name, nrec,(recs[nrec-1].t1-first_ns)/1e9);
    for(int k=0;k<nr;k++){
        double *iv=malloc(sizeof(double)*nrec), *du=malloc(sizeof(double)*nrec); size_t ni=0,nd=0; uint64_t prev=0; size_t calls=0;
        for(size_t i=0;i<nrec;i++){ if(recs[i].req!=reqs[k]||recs[i].t0<skip_until) continue; calls++;
            if(prev) iv[ni++]=(recs[i].t0-prev)/1e6; prev=recs[i].t0; du[nd++]=(recs[i].t1-recs[i].t0)/1e6; }
        if(ni<20){ free(iv);free(du); continue; }
        qsort(iv,ni,sizeof(double),cmpd); qsort(du,nd,sizeof(double),cmpd);
        double s=0,s2=0; for(size_t i=0;i<ni;i++){s+=iv[i];s2+=iv[i]*iv[i];}
        double m=s/ni, sd=sqrt(fmax(0,s2/ni-m*m)); size_t late=0, early=0, dbl=0;
        for(size_t i=0;i<ni;i++){ if(iv[i]>20.0) late++; if(iv[i]<12.0) early++; if(iv[i]>30.0) dbl++; }
        double ds=0; for(size_t i=0;i<nd;i++) ds+=du[i];
        fprintf(f,"req=0x%x calls=%zu rate=%.2f/s | interval ms: mean=%.2f sd=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f | >20ms=%zu(%.1f%%) >30ms=%zu <12ms=%zu | duration ms: mean=%.3f p99=%.3f max=%.3f\n",
          reqs[k],calls,calls/((recs[nrec-1].t1-skip_until)/1e9),m,sd,pct(iv,ni,.5),pct(iv,ni,.95),pct(iv,ni,.99),iv[ni-1],late,100.0*late/ni,dbl,early,ds/nd,pct(du,nd,.99),du[nd-1]);
        free(iv);free(du);
    }
    fclose(f);
}

__attribute__((constructor)) static void init(void){
    if(strcmp(program_invocation_short_name,"minarch.elf")!=0) return;
    real_ioctl = dlsym(RTLD_NEXT,"ioctl");
    recs = malloc(sizeof(rec_t)*MAXREC); if(!recs||!real_ioctl) return;
    snprintf(outpath,sizeof(outpath),"/mnt/SDCARD/.userdata/m21/logs/pantrace_%lu.txt",(unsigned long)time(NULL));
    first_ns = now_ns(); enabled = 1;
}
__attribute__((destructor)) static void fini(void){ dump(); }

int ioctl(int fd, unsigned long req, ...){
    va_list ap; va_start(ap,req); void *arg = va_arg(ap,void*); va_end(ap);
    if(!real_ioctl) real_ioctl = dlsym(RTLD_NEXT,"ioctl");
    if(!enabled) return real_ioctl(fd,req,arg);
    uint64_t t0=now_ns(); int r=real_ioctl(fd,req,arg); uint64_t t1=now_ns();
    /* keep only display-related ioctls: fb (0x46xx) and disp (small request numbers used with /dev/disp) */
    if((req>>8)==0x46 || req<0x200){
        if(nrec<MAXREC){ recs[nrec].req=(uint32_t)req; recs[nrec].t0=t0; recs[nrec].t1=t1; nrec++; if((nrec&0x1fff)==0) dump(); }
    }
    return r;
}
