/*
 * vxltest.exe - CPU self-test that replays the Red Alert 2 / Yuri's Revenge
 * voxel span renderer (gamemd.exe 0x7DF9C0) on synthetic data and checks
 * the result against a plain C reference implementation.
 *
 * The crash under investigation shows the renderer's row counter (CH) and
 * row index disagreeing in a way the instruction stream cannot produce.
 * This program runs the identical instruction sequence millions of times,
 * pinned to each logical CPU in turn and then on all CPUs at once, and
 * reports any run whose result differs from the reference.
 *
 * Build (x86 only!):  cl /O2 /MT /W3 vxltest.c /link /SUBSYSTEM:CONSOLE
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <intrin.h>

#pragma pack(push, 1)
typedef struct VxlDraw {
    int*           spanStart;   /* +00 */
    int*           spanEnd;     /* +04 (unused by this variant) */
    unsigned char* spanData;    /* +08 */
    int            idx;         /* +0C current span index */
    int            colStep;     /* +10 */
    int            rowStep;     /* +14 */
    unsigned int   pos;         /* +18 packed 8.8 fixed point: lo word X, hi word Y */
    unsigned short pad1C;       /* +1C */
    unsigned int   colDelta;    /* +1E */
    unsigned short pad22;       /* +22 */
    unsigned int   rowDelta;    /* +24 */
    unsigned short pad28;       /* +28 */
    short          zStepX;      /* +2A  (also read as dword zStep at +2A) */
    short          zStepY;      /* +2C */
    unsigned short pad2E;       /* +2E */
    unsigned char  cx;          /* +30 */
    unsigned char  cy;          /* +31 */
    unsigned char  cz;          /* +32 */
    unsigned char  pad33;       /* +33 */
    unsigned int   pad34;       /* +34 */
} VxlDraw;
#pragma pack(pop)

/* Per-thread stand-in for the game's static tables. Offsets are fixed and
 * used by the asm below: ztab @0, nodes @1024, remap @1280, pixels @66816. */
typedef struct Ctx {
    unsigned int  ztab[256];        /* per-z accumulated step, built by the prologue */
    unsigned char nodes[256];       /* normal -> shade row            (offset 1024) */
    unsigned char remap[65536];     /* (shade<<8|color) -> pixel      (offset 1280) */
    unsigned char pixels[65536];    /* 256x256 render buffer          (offset 66816) */
} Ctx;

/*
 * The engine's routine, instruction-for-instruction equivalent to gamemd
 * 0x7DF9C0 (the variant that crashed), with the absolute table addresses
 * replaced by ctx-relative ones. Stack layout at entry to the row loop:
 *   [ebp+8]=s  [ebp+12]=ctx  [ebp-4]=rowStartIdx [ebp-8]=rowStartPos [ebp-12]=pos
 */
__declspec(naked) static void __stdcall vxl_draw_asm(VxlDraw* s, Ctx* c)
{
    __asm {
        push ebp
        mov  ebp, esp
        add  esp, -12
        push esi
        push edi
        push ebx
        push ecx
        push edx
        mov  esi, [ebp+8]
        push ebp
        movsx ebp, word ptr [esi+2Ah]
        movsx edx, word ptr [esi+2Ch]
        xor  ecx, ecx
        mov  cl, [esi+32h]
        mov  edi, [esp]              ; saved ebp -> frame
        mov  edi, [edi+12]           ; ctx (ztab is at offset 0)
        xor  eax, eax
        xor  ebx, ebx
        mov  esi, 4
    L1: mov  [edi], ax
        add  edi, 2
        add  eax, ebp
        mov  [edi], bx
        add  edi, 2
        add  ebx, edx
        dec  ecx
        jnz  L1
        pop  ebp
        mov  esi, [ebp+8]
        xor  eax, eax
        mov  eax, [esi+18h]
        mov  [ebp-12], eax
        mov  ch, [esi+31h]
    LROW:
        mov  eax, [esi+0Ch]
        mov  [ebp-4], eax
        mov  eax, [ebp-12]
        mov  [ebp-8], eax
        mov  cl, [esi+30h]
    LCOL:
        mov  eax, [ebp-12]
        mov  [esi+18h], eax
        xor  edi, edi
        mov  eax, [esi+0Ch]
        mov  ebx, [esi]
        or   edi, [ebx+eax*4]
        jns  LSPAN
    LNEXT:
        mov  eax, [esi+1Eh]
        add  [ebp-12], eax
        mov  eax, [esi+10h]
        add  [esi+0Ch], eax
        dec  cl
        jnz  LCOL
        mov  eax, [ebp-8]
        add  eax, [esi+24h]
        mov  [ebp-12], eax
        mov  eax, [ebp-4]
        add  eax, [esi+14h]
        mov  [esi+0Ch], eax
        dec  ch
        jnz  LROW
        pop  edx
        pop  ecx
        pop  ebx
        pop  edi
        pop  esi
        leave
        ret  8
    LSPAN:
        push esi
        push ecx
        push ebp
        xor  ecx, ecx
        add  edi, [esi+8]
        mov  cl, [esi+32h]
        mov  bx, [esi+1Ah]
        shl  ebx, 10h
        mov  ebp, [esi+2Ah]
        mov  bx, [esi+18h]
    LS1:
        xor  eax, eax
        cmp  ecx, 0
        jz   LSEND
        mov  al, [edi]
        inc  edi
        sub  cl, al
        push esi
        mov  esi, [esp+4]            ; saved ebp (frame)
        mov  esi, [esi+12]           ; ctx
        add  ebx, [esi+eax*4]        ; ctx->ztab[skip]
        pop  esi
        mov  ch, [edi]
        inc  edi
        test ch, ch
        jz   LS3
    LS2:
        mov  eax, ebx
        dec  cl
        shr  eax, 10h
        xor  edx, edx
        mov  dl, [edi+1]
        mov  al, bh
        push esi
        mov  esi, [esp+4]            ; saved ebp (frame)
        mov  esi, [esi+12]           ; ctx
        mov  dh, [esi+edx+1024]      ; ctx->nodes[normal]
        mov  dl, [edi]
        add  ebx, ebp
        add  edi, 2
        dec  ch
        mov  dl, [esi+edx+1280]      ; ctx->remap[edx]
        mov  [esi+eax+66816], dl     ; ctx->pixels[eax]
        pop  esi
        jnz  LS2
    LS3:
        inc  edi
        jmp  LS1
    LSEND:
        pop  ebp
        pop  ecx
        pop  esi
        jmp  LNEXT
    }
}

/* Plain C model of the same routine (the intended semantics). */
static void vxl_draw_ref(VxlDraw* s, Ctx* c)
{
    unsigned int ztab[256];
    int z, row, col;
    unsigned short ax = 0, bx = 0;
    for (z = 0; z < s->cz; z++) {
        ztab[z] = (unsigned int)ax | ((unsigned int)bx << 16);
        ax = (unsigned short)(ax + s->zStepX);
        bx = (unsigned short)(bx + s->zStepY);
    }
    unsigned int rowPos = s->pos;   /* engine reads +18 once, then only writes it per column */
    for (row = 0; row < s->cy; row++) {
        int rowIdx = s->idx;
        unsigned int pos = rowPos;
        for (col = 0; col < s->cx; col++) {
            int off = s->spanStart[s->idx];
            s->pos = pos;
            if (off >= 0) {
                const unsigned char* p = s->spanData + off;
                unsigned int ebx = pos;
                unsigned int zstep = (unsigned short)s->zStepX | ((unsigned int)(unsigned short)s->zStepY << 16);
                unsigned char cl = s->cz;
                for (;;) {
                    unsigned char skip, count;
                    if (cl == 0) break;
                    skip = *p++;
                    cl = (unsigned char)(cl - skip);
                    ebx += ztab[skip];
                    count = *p++;
                    while (count) {
                        unsigned int pix = ((ebx >> 16) & 0xFF00u) | ((ebx >> 8) & 0xFFu);
                        unsigned int normal = p[1];
                        unsigned int color = p[0];
                        unsigned int rm = ((unsigned int)c->nodes[normal] << 8) | color;
                        c->pixels[pix] = c->remap[rm];
                        ebx += zstep;
                        p += 2;
                        cl--;
                        count--;
                    }
                    p++;
                }
            }
            pos += s->colDelta;
            s->idx += s->colStep;
        }
        rowPos += s->rowDelta;
        s->idx = rowIdx + s->rowStep;
    }
}

/* ---- synthetic voxel section ------------------------------------------ */
typedef struct Section {
    int cx, cy, cz;
    int* startAlloc; int* start;    /* with -1 guard rows before/after */
    unsigned char* data; int dataLen;
    int nSpans;
} Section;

#define PADROWS 300

/* face table from gamemd 0x8468C0: {startA, startB, colStep, rowStepSign} */
static const int faceTab[8][4] = {
    {1,1,-1,-1}, {1,0,-1,1}, {0,0,1,1}, {0,1,1,-1},
    {1,1,-1,-1}, {1,0,-1,1}, {0,0,1,1}, {0,1,1,-1},
};

static unsigned int g_seed = 0x12345678u;
static unsigned int rnd(unsigned int* s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static void build_section(Section* sec, int cx, int cy, int cz, unsigned int* seed)
{
    int n = cx * cy, i;
    int cap = n * (cz * 3 + 8);
    int pos = 0;
    sec->cx = cx; sec->cy = cy; sec->cz = cz; sec->nSpans = n;
    sec->startAlloc = (int*)malloc(sizeof(int) * (n + 2 * PADROWS * cx));
    for (i = 0; i < n + 2 * PADROWS * cx; i++) sec->startAlloc[i] = -1;
    sec->start = sec->startAlloc + PADROWS * cx;
    sec->data = (unsigned char*)malloc(cap);
    for (i = 0; i < n; i++) {
        int zleft = cz;
        if ((rnd(seed) % 100) < 7) { sec->start[i] = -1; continue; }   /* empty column */
        sec->start[i] = pos;
        while (zleft > 0) {
            int skip = (int)(rnd(seed) % (zleft + 1));
            int count, k;
            zleft -= skip;
            count = zleft ? 1 + (int)(rnd(seed) % zleft) : 0;
            sec->data[pos++] = (unsigned char)skip;
            sec->data[pos++] = (unsigned char)count;
            for (k = 0; k < count; k++) {
                sec->data[pos++] = (unsigned char)(rnd(seed) & 0xFF);  /* color */
                sec->data[pos++] = (unsigned char)(rnd(seed) & 0xFF);  /* normal */
            }
            sec->data[pos++] = (unsigned char)count;
            zleft -= count;
        }
    }
    sec->dataLen = pos;
}

static void setup_draw(VxlDraw* d, Section* sec, int face, unsigned int* seed)
{
    const int* f = faceTab[face & 7];
    memset(d, 0, sizeof(*d));
    d->spanStart = sec->start;
    d->spanEnd = sec->start;
    d->spanData = sec->data;
    d->cx = (unsigned char)sec->cx; d->cy = (unsigned char)sec->cy; d->cz = (unsigned char)sec->cz;
    d->idx = (sec->cx - 1) * f[0] + sec->cx * (sec->cy - 1) * f[1];
    d->colStep = f[2];
    d->rowStep = f[3] * sec->cx;
    d->pos = ((rnd(seed) % 0x6000 + 0x2000) << 16) | (rnd(seed) % 0x6000 + 0x2000);
    d->colDelta = ((rnd(seed) % 0x300 - 0x180) << 16) | ((rnd(seed) % 0x300 - 0x180) & 0xFFFF);
    d->rowDelta = ((rnd(seed) % 0x300 - 0x180) << 16) | ((rnd(seed) % 0x300 - 0x180) & 0xFFFF);
    d->zStepX = (short)(rnd(seed) % 0x200 - 0x100);
    d->zStepY = (short)(rnd(seed) % 0x200 - 0x100);
}

/* ---- test driver ------------------------------------------------------- */
typedef struct Result {
    unsigned long long iters, bad, badIdx, badPix, badPos;
    char detail[256];
} Result;

/* section sizes taken from the five crash reports */
static const int dims[5][3] = { {28,18,9}, {80,115,21}, {19,19,4}, {57,26,15}, {16,19,16} };
static Section g_sections[5];

static void init_ctx(Ctx* c, unsigned int* seed)
{
    int i;
    for (i = 0; i < 256; i++) c->nodes[i] = (unsigned char)(rnd(seed) & 0x3F);
    for (i = 0; i < 65536; i++) c->remap[i] = (unsigned char)(rnd(seed) & 0xFF);
}

static void run_for(DWORD ms, int cpu, Result* r)
{
    Ctx* ca = (Ctx*)malloc(sizeof(Ctx));
    Ctx* cb = (Ctx*)malloc(sizeof(Ctx));
    unsigned int seed = g_seed ^ ((unsigned)(cpu + 1) * 2654435761u) ^ GetCurrentThreadId();
    DWORD t0 = GetTickCount();
    VxlDraw da, db;
    int which = 0;
    init_ctx(ca, &seed);
    memcpy(cb, ca, sizeof(Ctx));
    while (GetTickCount() - t0 < ms) {
        Section* sec = &g_sections[which % 5];
        int face = (int)(rnd(&seed) & 7);
        unsigned int s2;
        which++;
        s2 = seed;
        setup_draw(&da, sec, face, &s2);
        s2 = seed;
        setup_draw(&db, sec, face, &s2);
        seed = s2;
        memset(ca->pixels, 0, 65536);
        memset(cb->pixels, 0, 65536);
        vxl_draw_asm(&da, ca);
        vxl_draw_ref(&db, cb);
        r->iters++;
        if (da.idx != db.idx || da.pos != db.pos || memcmp(ca->pixels, cb->pixels, 65536) != 0) {
            int pixdiff = memcmp(ca->pixels, cb->pixels, 65536) != 0;
            r->bad++;
            if (da.idx != db.idx) r->badIdx++;
            if (da.pos != db.pos) r->badPos++;
            if (pixdiff) r->badPix++;
            if (r->bad == 1) {
                int rows = (da.idx - db.idx) / (db.rowStep ? db.rowStep : 1);
                sprintf(r->detail, "section %dx%dx%d face %d: asm idx=%d ref idx=%d (%+d rows), pos asm=%08X ref=%08X, pixels %s",
                    sec->cx, sec->cy, sec->cz, face, da.idx, db.idx, rows, da.pos, db.pos,
                    pixdiff ? "DIFFER" : "same");
            }
        }
    }
    free(ca); free(cb);
}

typedef struct ThreadArg { int cpu; DWORD ms; Result res; } ThreadArg;

static DWORD WINAPI worker(LPVOID p)
{
    ThreadArg* a = (ThreadArg*)p;
    if (a->cpu >= 0) SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << a->cpu);
    run_for(a->ms, a->cpu, &a->res);
    return 0;
}

static FILE* g_log;
static void out(const char* fmt, ...)
{
    va_list ap; char buf[1024];
    va_start(ap, fmt); vsprintf(buf, fmt, ap); va_end(ap);
    fputs(buf, stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
}

static void cpu_info(void)
{
    int r[4]; char brand[49] = {0}; int i;
    __cpuid(r, 0x80000000);
    if ((unsigned)r[0] >= 0x80000004) {
        for (i = 0; i < 3; i++) { __cpuid(r, 0x80000002 + i); memcpy(brand + 16 * i, r, 16); }
    }
    __cpuid(r, 1);
    out("CPU: %s\n", brand[0] ? brand : "(unknown)");
    out("CPUID.1 EAX=%08X  family %d model %d stepping %d\n", r[0],
        ((r[0] >> 8) & 0xF) + ((r[0] >> 20) & 0xFF), ((r[0] >> 4) & 0xF) | ((r[0] >> 12) & 0xF0), r[0] & 0xF);
}

int main(int argc, char** argv)
{
    SYSTEM_INFO si; int ncpu, i; unsigned int seed = g_seed;
    DWORD perCpuMs = 4000, allMs = 20000;
    ThreadArg* args; HANDLE* hs;
    unsigned long long totalIters = 0, totalBad = 0;
    char logpath[MAX_PATH];

    if (argc > 1) perCpuMs = (DWORD)atoi(argv[1]) * 1000;
    if (argc > 2) allMs = (DWORD)atoi(argv[2]) * 1000;

    GetModuleFileNameA(NULL, logpath, MAX_PATH);
    { char* p = strrchr(logpath, '\\'); if (p) strcpy(p + 1, "vxltest_log.txt"); else strcpy(logpath, "vxltest_log.txt"); }
    g_log = fopen(logpath, "w");

    out("vxltest 1.0 - RA2/YR voxel renderer CPU self-test\n");
    cpu_info();
    GetSystemInfo(&si);
    ncpu = (int)si.dwNumberOfProcessors; if (ncpu > 32) ncpu = 32;
    out("Logical CPUs: %d   per-CPU phase: %lu s each, all-CPU phase: %lu s\n", ncpu, perCpuMs / 1000, allMs / 1000);
    out("Log: %s\n\n", logpath);

    for (i = 0; i < 5; i++) build_section(&g_sections[i], dims[i][0], dims[i][1], dims[i][2], &seed);

    /* self-check: asm and reference must agree, otherwise the test itself is broken */
    {
        Result r; memset(&r, 0, sizeof r);
        run_for(300, -1, &r);
        out("Warm-up: %llu runs, %llu mismatches\n", r.iters, r.bad);
        if (r.bad && r.bad * 4 > r.iters * 3)
            out("!! asm and reference disagree on almost every run. On a known-good PC this means the test is broken.\n   %s\n", r.detail);
    }

    out("\nPhase 1: one CPU at a time\n");
    args = (ThreadArg*)calloc(ncpu, sizeof(ThreadArg));
    hs = (HANDLE*)calloc(ncpu, sizeof(HANDLE));
    for (i = 0; i < ncpu; i++) {
        memset(&args[i], 0, sizeof(ThreadArg));
        args[i].cpu = i; args[i].ms = perCpuMs;
        hs[i] = CreateThread(NULL, 0, worker, &args[i], 0, NULL);
        WaitForSingleObject(hs[i], INFINITE); CloseHandle(hs[i]);
        out("  CPU %2d: %10llu runs, %llu mismatches%s%s\n", i, args[i].res.iters, args[i].res.bad,
            args[i].res.bad ? "  <<< FAIL  " : "", args[i].res.bad ? args[i].res.detail : "");
        totalIters += args[i].res.iters; totalBad += args[i].res.bad;
    }

    out("\nPhase 2: all CPUs at once (%lu s)\n", allMs / 1000);
    for (i = 0; i < ncpu; i++) {
        memset(&args[i], 0, sizeof(ThreadArg));
        args[i].cpu = i; args[i].ms = allMs;
        hs[i] = CreateThread(NULL, 0, worker, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(ncpu, hs, TRUE, INFINITE);
    for (i = 0; i < ncpu; i++) {
        CloseHandle(hs[i]);
        out("  CPU %2d: %10llu runs, %llu mismatches%s%s\n", i, args[i].res.iters, args[i].res.bad,
            args[i].res.bad ? "  <<< FAIL  " : "", args[i].res.bad ? args[i].res.detail : "");
        totalIters += args[i].res.iters; totalBad += args[i].res.bad;
    }

    out("\n==== RESULT: %llu runs, %llu mismatches ====\n", totalIters, totalBad);
    if (totalBad == 0)
        out("No errors. The renderer loop executed correctly on every CPU in this run.\n");
    else
        out("ERRORS FOUND. The CPU produced different results for identical input.\nThis points at hardware or firmware instability (CPU, RAM, power/clock settings), not at the game.\n");
    if (g_log) fclose(g_log);
    if (argc <= 1) { printf("\nPress Enter to close..."); getchar(); }
    return totalBad ? 1 : 0;
}
