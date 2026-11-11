/* VLC Waveform, native VLC 3 Windows plugin.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * References: VLC visual.c (LGPL-2.1+), gestures.c (GPL-2.0+),
 * nurupo/vlc-pause-click-plugin by Maxim Biro (LGPL-2.1+).
 * Full source accompanies the installable binary.
 */
#define N_(s) (s)
#define MODULE_STRING "waveform"
#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#define VLC_MODULE_COPYRIGHT "VLC Waveform contributors, 2026"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* VLC 3 uses legacy msvcrt; Zig's MinGW runtime uses UCRT. VLC allocates
 * strings which public inline helpers free, so both sides MUST use the
 * same heap implementation. All plugin buffers use VLC's CRT allocator.
 * FILE handles remain private to this DLL's own UCRT stdio. */
static INIT_ONCE allocator_once=INIT_ONCE_STATIC_INIT;
static void *(__cdecl *legacy_alloc)(size_t);
static void *(__cdecl *legacy_resize)(void *,size_t);
static void *(__cdecl *legacy_zero)(size_t,size_t);
static void (__cdecl *legacy_release)(void *);
static BOOL CALLBACK allocator_init(PINIT_ONCE once,void *arg,void **ctx)
{
    (void)once;(void)arg;(void)ctx;
    HMODULE crt=GetModuleHandleW(L"msvcrt.dll");
    if(!crt)crt=LoadLibraryW(L"msvcrt.dll");
    legacy_alloc=(void *)GetProcAddress(crt,"malloc");
    legacy_resize=(void *)GetProcAddress(crt,"realloc");
    legacy_zero=(void *)GetProcAddress(crt,"calloc");
    legacy_release=(void *)GetProcAddress(crt,"free");
    return legacy_alloc && legacy_resize && legacy_zero && legacy_release;
}
static void *wf_malloc(size_t n) { InitOnceExecuteOnce(&allocator_once,allocator_init,NULL,NULL);return legacy_alloc(n); }
static void *wf_calloc(size_t n,size_t s) { InitOnceExecuteOnce(&allocator_once,allocator_init,NULL,NULL);return legacy_zero(n,s); }
static void *wf_realloc(void *p,size_t n) { InitOnceExecuteOnce(&allocator_once,allocator_init,NULL,NULL);return legacy_resize(p,n); }
static void wf_free(void *p) { InitOnceExecuteOnce(&allocator_once,allocator_init,NULL,NULL);legacy_release(p); }
static char *wf_strdup(const char *s) { size_t n=strlen(s)+1;char *p=wf_malloc(n);if(p)memcpy(p,s,n);return p; }
#define malloc(...) wf_malloc(__VA_ARGS__)
#define calloc wf_calloc
#define realloc wf_realloc
#define free wf_free
#define strdup wf_strdup
/* Source headers expect configure's poll compatibility declaration on Windows.
 * No plugin code calls poll; the inline VLC thread wrapper only needs its type. */
struct pollfd;
int poll(struct pollfd *, unsigned, int);
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <vlc_input_item.h>
#include <vlc_playlist.h>
#include <vlc_filter.h>
#include <vlc_aout.h>
#include <vlc_vout.h>
#include <vlc_url.h>
#include <vlc_stream.h>
#include <vlc_subpicture.h>

#define WIDTH 800
#define HEIGHT 240
#define LEFT 20
#define RIGHT (WIDTH-28)
#define MAX_BUCKETS 8640000u /* bounded to 24 hours at 10 ms */
#define NET_CACHE_LIMIT (16u*1024u*1024u)
#define TOOL_BUTTON_W 80
#define TOOL_STEP 86
#define TOOL_X (RIGHT-4*TOOL_STEP+6)
#define CLAMP(v,a,b) ((v)<(a)?(a):((v)>(b)?(b):(v)))
typedef struct { int16_t lo, hi; } peak_t;
typedef struct {
    CRITICAL_SECTION lock;
    HANDLE stop, control, analyzer, analysis_stop, job;
    intf_thread_t *intf;
    input_thread_t *input;
    unsigned generation;
    char *uri;
    wchar_t path[32768], ffmpeg[32768], cache_dir[32768], settings_path[32768];
    wchar_t title[1024], error[256];
    peak_t *peaks;
    unsigned count, capacity;
    int max_amplitude;
    bool complete, analyzing, local, seekable;
    float position;
    int state;
    int64_t length, time;
    bool pending_seek;
    float seek;
    unsigned seek_generation;
    int view_mode;
    bool enabled, settings_dirty, pending_disable;
    audio_output_t *menu_aout;
    LONG references;
} context_t;
static context_t *global;
static SRWLOCK global_lock=SRWLOCK_INIT;
typedef struct {
    CRITICAL_SECTION lock;
    LONG references;
    subpicture_region_t *pending;
    unsigned applied;
} canvas_t;
typedef struct {
    context_t *ctx;
    vout_thread_t *vout;
    HANDLE stop, thread;
    HDC dc, base_dc;
    HBITMAP bitmap, old_bitmap, base_bitmap, old_base_bitmap;
    HFONT font, small_font;
    uint8_t *pixels;
    filter_t *filter;
    peak_t columns[RIGHT-LEFT];
    unsigned drawn_count, drawn_generation;
    int64_t drawn_length, drawn_time;
    int drawn_max, drawn_state, drawn_x, drawn_mode;
    bool drawn_analyzing, drawn_enabled;
    wchar_t drawn_error[256];
    ULONGLONG rebuilt_at;
    unsigned frames, rebuilds;
    int spu_channel;
    canvas_t *canvas;
} visual_t;

static const char *view_keys[]={"waveform-standard","waveform-compact","waveform-contrast"};
static const wchar_t *view_labels[]={L"Standard",L"Kompakt",L"Kontrast"};
static const char *menu_labels[]={"Wellenform - Standard","Wellenform - Kompakt","Wellenform - Kontrast"};

static void release_canvas(canvas_t *canvas)
{
    if(InterlockedDecrement(&canvas->references)) return;
    subpicture_region_Delete(canvas->pending);
    DeleteCriticalSection(&canvas->lock);free(canvas);
}
static int ValidateCanvas(subpicture_t *sp,bool src_changed,const video_format_t *src,
                          bool dst_changed,const video_format_t *dst,mtime_t at)
{
    (void)src_changed;(void)src;(void)dst_changed;(void)dst;(void)at;
    canvas_t *canvas=(void *)sp->updater.p_sys;
    EnterCriticalSection(&canvas->lock);bool pending=canvas->pending!=NULL;LeaveCriticalSection(&canvas->lock);
    return pending?VLC_EGENERIC:VLC_SUCCESS;
}
static void UpdateCanvas(subpicture_t *sp,const video_format_t *src,const video_format_t *dst,mtime_t at)
{
    (void)src;(void)dst;(void)at;
    canvas_t *canvas=(void *)sp->updater.p_sys;
    /* VLC deletes the previous region before invoking this updater. */
    EnterCriticalSection(&canvas->lock);
    sp->p_region=canvas->pending;canvas->pending=NULL;canvas->applied++;
    LeaveCriticalSection(&canvas->lock);
}
static void DestroyCanvas(subpicture_t *sp)
{
    release_canvas((void *)sp->updater.p_sys);
}
static void replace_canvas(canvas_t *canvas,subpicture_region_t *region)
{
    EnterCriticalSection(&canvas->lock);
    subpicture_region_t *old=canvas->pending;canvas->pending=region;
    LeaveCriticalSection(&canvas->lock);
    subpicture_region_Delete(old);
}

static int OpenInterface(vlc_object_t *);
static void CloseInterface(vlc_object_t *);
static int OpenVisual(vlc_object_t *);
static void CloseVisual(vlc_object_t *);
static int OpenNetCache(vlc_object_t *);
static void CloseNetCache(vlc_object_t *);

vlc_module_begin()
    set_shortname(N_("Waveform"))
    set_description(N_("VLC Waveform - native waveform with three views"))
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_capability("interface", 0)
    add_shortcut("waveformintf")
    add_loadfile("waveform-ffmpeg", "", N_("FFmpeg executable"),
                 N_("Path to the bundled FFmpeg audio decoder"), true)
    add_loadfile("waveform-settings", "", N_("Waveform preferences"),
                 N_("Optional path for the plugin-owned preferences file"), true)
    set_callbacks(OpenInterface, CloseInterface)
    add_submodule()
    set_shortname(N_("Waveform"))
    set_description(N_("Full-file waveform with click seeking"))
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_VISUAL)
    set_capability("visualization", 0)
    add_shortcut("waveformvis")
    set_callbacks(OpenVisual, CloseVisual)
    add_submodule()
    set_description(N_("Bounded read-ahead for small NAS MP3 files"))
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_STREAM_FILTER)
    set_capability("stream_filter", 400)
    add_shortcut("waveformnetcache")
    set_callbacks(OpenNetCache, CloseNetCache)
vlc_module_end()

typedef struct {
    uint8_t *data;
    size_t size, position;
    bool failed;
} net_cache_t;

static bool net_cache_eligible(const char *url,uint64_t size,bool preparsing)
{
    if(preparsing || !url || !size || size>NET_CACHE_LIMIT) return false;
    char *path=vlc_uri2path(url);
    if(!path) return false;
    size_t n=strlen(path);
    bool unc=n>2 && ((path[0]=='\\' && path[1]=='\\') || (path[0]=='/' && path[1]=='/'));
    bool mp3=n>=4 && !_stricmp(path+n-4,".mp3");
    free(path);
    return unc && mp3;
}
static ssize_t NetCacheRead(stream_t *s,void *buffer,size_t length)
{
    net_cache_t *c=s->p_sys;
    if(length>c->size-c->position) length=c->size-c->position;
    if(buffer && length) memcpy(buffer,c->data+c->position,length);
    c->position+=length;
    return (ssize_t)length;
}
static int NetCacheSeek(stream_t *s,uint64_t offset)
{
    net_cache_t *c=s->p_sys;
    if(c->failed || offset>c->size) return VLC_EGENERIC;
    c->position=(size_t)offset;
    return VLC_SUCCESS;
}
static int NetCacheControl(stream_t *s,int query,va_list args)
{
    net_cache_t *c=s->p_sys;
    switch(query) {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
            *va_arg(args,bool *)=!c->failed;return VLC_SUCCESS;
        case STREAM_GET_SIZE:
            if(c->failed) return VLC_EGENERIC;
            *va_arg(args,uint64_t *)=c->size;return VLC_SUCCESS;
        default:
            return vlc_stream_vaControl(s->p_source,query,args);
    }
}
static int OpenNetCache(vlc_object_t *obj)
{
    stream_t *s=(stream_t *)obj;
    AcquireSRWLockShared(&global_lock);
    bool enabled=false;
    if(global && global->intf->obj.libvlc==obj->obj.libvlc) {
        EnterCriticalSection(&global->lock);enabled=global->enabled;LeaveCriticalSection(&global->lock);
    }
    ReleaseSRWLockShared(&global_lock);
    /* VLC 3 does not propagate b_preparsing into all stream wrappers.
       Preparsers are quiet; conservatively leave quiet playback alone too. */
    if(!enabled || !s->p_input || (s->obj.flags&OBJECT_FLAGS_QUIET) ||
       s->b_preparsing || !s->p_source) return VLC_EGENERIC;
    /* Auto filters are retried after each insertion. Never cache a stream
       already backed by this module, even through another wrapper. */
    for(stream_t *p=s->p_source;p;p=p->p_source)
        if(p->pf_read==NetCacheRead) return VLC_EGENERIC;
    uint64_t size=0;bool seekable=false;
    if(vlc_stream_Control(s->p_source,STREAM_CAN_SEEK,&seekable) || !seekable)
        return VLC_EGENERIC;
    if(vlc_stream_GetSize(s->p_source,&size) ||
       !net_cache_eligible(s->psz_url,size,s->b_preparsing) ||
       vlc_stream_Tell(s->p_source)!=0) return VLC_EGENERIC;
    net_cache_t *c=calloc(1,sizeof(*c));
    if(!c) return VLC_ENOMEM;
    c->data=malloc((size_t)size);
    if(!c->data) { free(c);return VLC_ENOMEM; }
    mtime_t start=mdate();
    ssize_t got=vlc_stream_Read(s->p_source,c->data,(size_t)size);
    uint64_t after=0;
    if(got!=(ssize_t)size || vlc_stream_GetSize(s->p_source,&after) || after!=size) {
        if(!vlc_stream_Seek(s->p_source,0)) {
            msg_Warn(s,"NAS MP3 read-ahead unavailable; original stream restored");
            free(c->data);free(c);return VLC_EGENERIC;
        }
        /* Rejecting a filter does not rewind its source. Keep an explicit
           failed/EOF filter rather than expose a consumed suffix to VLC. */
        free(c->data);c->data=NULL;c->failed=true;
        msg_Err(s,"NAS MP3 read-ahead failed; cannot restore original stream; refusing partial playback");
    } else {
        c->size=(size_t)size;
        msg_Info(s,"NAS MP3 read-ahead ready: %zu bytes in %lld ms; playback reads RAM",
                 c->size,(long long)((mdate()-start)/1000));
    }
    s->p_sys=c;
    s->pf_read=NetCacheRead;s->pf_seek=NetCacheSeek;s->pf_control=NetCacheControl;
    return VLC_SUCCESS;
}
static void CloseNetCache(vlc_object_t *obj)
{
    net_cache_t *c=((stream_t *)obj)->p_sys;
    free(c->data);free(c);
}

static void wide(const char *s, wchar_t *out, size_t n)
{
    out[0]=0;
    if (s && !MultiByteToWideChar(CP_UTF8,0,s,-1,out,(int)n)) out[0]=0;
}
static void reset_view(context_t *c)
{
    c->pending_seek=false;
}
static int view_index(const char *key)
{
    for(int i=0;key && i<3;i++) if(!strcmp(key,view_keys[i])) return i;
    return -1;
}
static void load_settings(context_t *c)
{
    c->enabled=true;c->view_mode=0;
    FILE *file=_wfopen(c->settings_path,L"rb");
    if(!file) return;
    int enabled,mode;
    if(fscanf(file,"VLCWaveform 1\nenabled=%d\nview=%d",&enabled,&mode)==2 &&
       (enabled==0 || enabled==1) && mode>=0 && mode<3) {
        c->enabled=enabled!=0;c->view_mode=mode;
    }
    fclose(file);
}
static void save_settings(context_t *c)
{
    EnterCriticalSection(&c->lock);
    bool dirty=c->settings_dirty,enabled=c->enabled;int mode=c->view_mode;
    c->settings_dirty=false;
    LeaveCriticalSection(&c->lock);
    if(!dirty) return;
    wchar_t tmp[32768];_snwprintf(tmp,32768,L"%s.%lu.tmp",c->settings_path,GetCurrentProcessId());
    FILE *file=_wfopen(tmp,L"wb");bool ok=false;
    if(file) {
        ok=fprintf(file,"VLCWaveform 1\nenabled=%d\nview=%d\n",enabled,mode)>0;
        if(fclose(file)) ok=false;
        if(ok) ok=MoveFileExW(tmp,c->settings_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    }
    if(!ok) {
        DeleteFileW(tmp);
        msg_Warn(c->intf,"Waveform preferences could not be saved");
    }
}
/* Called synchronously by the core before its visualization restart request.
   SETVALUE is deliberately callback-free: recursive var_SetString here would
   wait for this active callback. Never access val.psz_string after SETVALUE. */
static int AudioVisualChanged(vlc_object_t *obj,const char *name,vlc_value_t old,vlc_value_t val,void *data)
{
    (void)name;(void)old;
    context_t *c=data;
    char *key=var_GetString(obj,"visual");int mode=view_index(key);free(key);
    bool translate=val.psz_string && !strcmp(val.psz_string,"visual") && mode>=0;
    bool enabled=translate || (val.psz_string && !strcmp(val.psz_string,"waveformvis"));
    EnterCriticalSection(&c->lock);
    if(mode<0) mode=c->view_mode;
    if(c->enabled!=enabled || (enabled && c->view_mode!=mode)) c->settings_dirty=true;
    c->enabled=enabled;if(enabled)c->view_mode=mode;
    if(enabled)c->pending_disable=false;
    LeaveCriticalSection(&c->lock);
    if(translate) {
        vlc_value_t replacement={.psz_string="waveformvis"};
        var_Change(obj,"audio-visual",VLC_VAR_SETVALUE,&replacement,NULL);
    }
    return VLC_SUCCESS;
}
static void detach_menu(context_t *c)
{
    if(!c->menu_aout) return;
    /* No context/global lock: DelCallback waits for active callbacks. */
    var_DelCallback(c->menu_aout,"audio-visual",AudioVisualChanged,c);
    for(int i=0;i<3;i++) {
        vlc_value_t choice={.psz_string=(char *)view_keys[i]};
        var_Change(c->menu_aout,"visual",VLC_VAR_DELCHOICE,&choice,NULL);
    }
    vlc_object_release(c->menu_aout);c->menu_aout=NULL;
}
static void reconcile_menu(context_t *c)
{
    audio_output_t *aout=playlist_GetAout(pl_Get(c->intf));
    if(aout==c->menu_aout) { if(aout)vlc_object_release(aout);return; }
    detach_menu(c);c->menu_aout=aout;
    if(!aout) return;
    for(int i=0;i<3;i++) {
        vlc_value_t choice={.psz_string=(char *)view_keys[i]},label={.psz_string=(char *)menu_labels[i]};
        var_Change(aout,"visual",VLC_VAR_ADDCHOICE,&choice,&label);
    }
    var_AddCallback(aout,"audio-visual",AudioVisualChanged,c);
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;int mode=c->view_mode;LeaveCriticalSection(&c->lock);
    char *active=var_GetString(aout,"audio-visual");
    if(active && !strcmp(active,"waveformvis")) {
        vlc_value_t selection={.psz_string=(char *)(enabled?view_keys[mode]:"")};
        var_Change(aout,"visual",VLC_VAR_SETVALUE,&selection,NULL);
        if(!enabled) var_SetString(aout,"audio-visual","");
    }
    free(active);
}
static void sync_menu_selection(context_t *c)
{
    if(!c->menu_aout) return;
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;int mode=c->view_mode;LeaveCriticalSection(&c->lock);
    if(!enabled) return;
    char *active=var_GetString(c->menu_aout,"audio-visual");
    if(active && !strcmp(active,"waveformvis")) {
        vlc_value_t choice={.psz_string=(char *)view_keys[mode]};
        var_Change(c->menu_aout,"visual",VLC_VAR_SETVALUE,&choice,NULL);
    }
    free(active);
}
static uint64_t hash_data(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b=p;
    while(n--) { h^=*b++; h*=1099511628211ULL; }
    return h;
}
static bool cache_path(context_t *c, wchar_t *out, size_t n)
{
    WIN32_FILE_ATTRIBUTE_DATA st;
    if(!GetFileAttributesExW(c->path,GetFileExInfoStandard,&st) || (st.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    uint64_t h=hash_data(14695981039346656037ULL,c->path,wcslen(c->path)*sizeof(wchar_t));
    h=hash_data(h,&st.ftLastWriteTime,sizeof(st.ftLastWriteTime));
    h=hash_data(h,&st.nFileSizeHigh,sizeof(DWORD));
    h=hash_data(h,&st.nFileSizeLow,sizeof(DWORD));
    _snwprintf(out,n,L"%s\\%016llx.wfp",c->cache_dir,(unsigned long long)h);
    return true;
}
typedef struct { uint32_t magic, version, count, bucket_us; } cache_header_t;
static bool load_cache(context_t *c, const wchar_t *path)
{
    FILE *f=_wfopen(path,L"rb"); if(!f) return false;
    cache_header_t h;
    bool ok=fread(&h,sizeof(h),1,f)==1 && h.magic==0x31504657 && h.version==1 && h.bucket_us==10000 && h.count>0 && h.count<=MAX_BUCKETS;
    peak_t *p=ok?malloc((size_t)h.count*sizeof(*p)):NULL;
    if(!p) ok=false;
    if(ok) ok=fread(p,sizeof(*p),h.count,f)==h.count && fgetc(f)==EOF;
    fclose(f);
    if(ok) {
        EnterCriticalSection(&c->lock);
        free(c->peaks); c->peaks=p; c->count=c->capacity=h.count;
        c->max_amplitude=1;
        for(unsigned i=0;i<h.count;i++) {
            int a=abs(p[i].lo),b=abs(p[i].hi);
            if(a>c->max_amplitude)c->max_amplitude=a;
            if(b>c->max_amplitude)c->max_amplitude=b;
        }
        c->complete=true; c->analyzing=false;
        LeaveCriticalSection(&c->lock);
    } else free(p);
    return ok;
}
static void save_cache(context_t *c, const wchar_t *path)
{
    wchar_t tmp[32768]; _snwprintf(tmp,32768,L"%s.%lu.tmp",path,GetCurrentProcessId());
    FILE *f=_wfopen(tmp,L"wb"); if(!f) return;
    EnterCriticalSection(&c->lock);
    cache_header_t h={0x31504657,1,c->count,10000};
    bool ok=fwrite(&h,sizeof(h),1,f)==1 && fwrite(c->peaks,sizeof(peak_t),c->count,f)==c->count;
    LeaveCriticalSection(&c->lock);
    if(fclose(f)) ok=false;
    if(ok) MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    else DeleteFileW(tmp);
}
static bool append_peaks(context_t *c, const peak_t *p, unsigned count)
{
    bool ok=true;
    EnterCriticalSection(&c->lock);
    if(c->count+count>MAX_BUCKETS) ok=false;
    else if(c->count+count>c->capacity) {
        unsigned cap=c->capacity?c->capacity*2:16384;
        if(cap<c->count+count) cap=c->count+count;
        if(cap>MAX_BUCKETS) cap=MAX_BUCKETS;
        peak_t *q=realloc(c->peaks,(size_t)cap*sizeof(*q));
        if(!q) ok=false; else { c->peaks=q; c->capacity=cap; }
    }
    if(ok) {
        memcpy(c->peaks+c->count,p,count*sizeof(*p)); c->count+=count;
        for(unsigned i=0;i<count;i++) {
            int a=abs(p[i].lo),b=abs(p[i].hi);
            if(a>c->max_amplitude)c->max_amplitude=a;
            if(b>c->max_amplitude)c->max_amplitude=b;
        }
    }
    LeaveCriticalSection(&c->lock);
    return ok;
}
static DWORD WINAPI Analyze(void *data)
{
    context_t *c=data;
    SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
    wchar_t cache[32768];
    bool cached=cache_path(c,cache,32768);
    if(cached && load_cache(c,cache)) { msg_Dbg(c->intf,"waveform cache hit"); return 0; }
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    HANDLE rd=NULL,wr=NULL,nul=INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi={0}; bool success=false;
    if(!CreatePipe(&rd,&wr,&sa,1024*1024)) goto done;
    SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
    nul=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    wchar_t *cmd=malloc(131072*sizeof(wchar_t)); if(!cmd) goto done;
    _snwprintf(cmd,131072,L"\"%s\" -v error -nostdin -threads 1 -filter_threads 1 -i \"%s\" -map 0:a:0 -vn -ar 48000 -ac 2 -f s16le -acodec pcm_s16le -",c->ffmpeg,c->path);
    STARTUPINFOW si={0}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=nul; si.hStdError=nul; si.hStdOutput=wr;
    /* Suspended creation guarantees job membership before decoding starts. */
    BOOL created=CreateProcessW(c->ffmpeg,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|IDLE_PRIORITY_CLASS,NULL,NULL,&si,&pi);
    free(cmd); if(!created) goto done;
    if(!AssignProcessToJobObject(c->job,pi.hProcess)) { TerminateProcess(pi.hProcess,1); goto done; }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread); pi.hThread=NULL;
    CloseHandle(wr); wr=NULL;
    uint8_t buffer[65536]; bool have_low=false; uint8_t low=0;
    int sample_count=0; int16_t lo=32767,hi=-32768;
    peak_t peaks[128]; unsigned np=0;
    for(;;) {
        if(WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0) break;
        DWORD n=0;
        /* Dedicated, cancellable blocking reads avoid throttling decoding by
         * sleeping once per small pipe buffer (especially on long recordings). */
        if(!ReadFile(rd,buffer,sizeof(buffer),&n,NULL) || !n) {
            DWORD code=1; WaitForSingleObject(pi.hProcess,1000); GetExitCodeProcess(pi.hProcess,&code);
            success=code==0; break;
        }
        for(DWORD i=0;i<n;i++) {
            if(!have_low) { low=buffer[i]; have_low=true; continue; }
            int16_t v=(int16_t)((uint16_t)low|((uint16_t)buffer[i]<<8)); have_low=false;
            if(v<lo) lo=v; if(v>hi) hi=v;
            if(++sample_count==960) { /* 480 frames * stereo */
                peaks[np++]=(peak_t){lo,hi}; sample_count=0; lo=32767;hi=-32768;
                if(np==128) { if(!append_peaks(c,peaks,np)) goto done; np=0; }
            }
        }
    }
    if(np && !append_peaks(c,peaks,np)) success=false;
    if(success && sample_count) { peak_t last={lo,hi}; success=append_peaks(c,&last,1); }
done:
    if(pi.hProcess) { if(WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0) TerminateProcess(pi.hProcess,1); WaitForSingleObject(pi.hProcess,3000); CloseHandle(pi.hProcess); }
    if(pi.hThread) CloseHandle(pi.hThread);
    if(rd) CloseHandle(rd); if(wr) CloseHandle(wr); if(nul!=INVALID_HANDLE_VALUE) CloseHandle(nul);
    bool canceled=WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0;
    EnterCriticalSection(&c->lock);
    c->analyzing=false; c->complete=success && c->count>0;
    if(!canceled && !c->complete) wcscpy(c->error,L"Audioanalyse fehlgeschlagen - FFmpeg / Datei pruefen");
    bool complete=c->complete;
    LeaveCriticalSection(&c->lock);
    if(!canceled && complete && cached) save_cache(c,cache);
    msg_Dbg(c->intf,"waveform analysis finished success=%d canceled=%d",complete,canceled);
    return 0;
}
static void cancel_analysis(context_t *c)
{
    if(c->analyzer) {
        SetEvent(c->analysis_stop);
        /* Retry cancellation to cover the race between a stop check and the
         * next synchronous read, including a stalled network-file decoder. */
        do { CancelSynchronousIo(c->analyzer); }
        while(WaitForSingleObject(c->analyzer,50)==WAIT_TIMEOUT);
        CloseHandle(c->analyzer); c->analyzer=NULL;
    }
    ResetEvent(c->analysis_stop);
}
static void set_track(context_t *c, input_thread_t *input, const char *uri)
{
    /* Never replace shared path/peaks before the old worker has finished. */
    cancel_analysis(c);
    input_item_t *item=input?input_GetItem(input):NULL;
    char *name=item?input_item_GetName(item):NULL;
    char *path=uri?vlc_uri2path(uri):NULL;
    EnterCriticalSection(&c->lock);
    c->generation++; free(c->uri); c->uri=uri?strdup(uri):NULL;
    if(c->input) vlc_object_release(c->input);
    c->input=input; if(input) vlc_object_hold(input);
    c->local=path!=NULL; wide(path,c->path,32768); wide(name,c->title,1024);
    c->count=0; c->max_amplitude=1; c->complete=false; c->error[0]=0; c->analyzing=false;
    c->position=0;c->time=0;c->length=0;c->seekable=false; reset_view(c);
    if(input && !path) wcscpy(c->error,L"Stream - keine vollstaendige Datei-Wellenform");
    LeaveCriticalSection(&c->lock);
    free(name); free(path);
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;LeaveCriticalSection(&c->lock);
    if(input && enabled && c->local && var_CountChoices(input,"video-es")<=1) {
        EnterCriticalSection(&c->lock);c->analyzing=true;LeaveCriticalSection(&c->lock);
        c->analyzer=CreateThread(NULL,0,Analyze,c,0,NULL);
        if(!c->analyzer) { EnterCriticalSection(&c->lock); c->analyzing=false; wcscpy(c->error,L"Analyse konnte nicht gestartet werden"); LeaveCriticalSection(&c->lock); }
    }
}
static DWORD WINAPI Control(void *data)
{
    context_t *c=data;
    bool was_enabled=c->enabled;
    while(WaitForSingleObject(c->stop,50)==WAIT_TIMEOUT) {
        reconcile_menu(c);
        EnterCriticalSection(&c->lock);
        bool enabled=c->enabled,disable=c->pending_disable;c->pending_disable=false;
        LeaveCriticalSection(&c->lock);
        if(disable && c->menu_aout) var_SetString(c->menu_aout,"visual","");
        sync_menu_selection(c);
        if(!enabled && was_enabled) cancel_analysis(c);
        input_thread_t *input=playlist_CurrentInput(pl_Get(c->intf));
        char *uri=input?input_item_GetURI(input_GetItem(input)):NULL;
        EnterCriticalSection(&c->lock);
        bool changed=input!=c->input || (!!uri!=!!c->uri) || (uri && c->uri && strcmp(uri,c->uri));
        LeaveCriticalSection(&c->lock);
        if(changed || (enabled && !was_enabled)) set_track(c,input,uri);
        was_enabled=enabled;
        free(uri);
        if(input) {
            int state=var_GetInteger(input,"state");
            int64_t length=var_GetInteger(input,"length"),time=var_GetInteger(input,"time");
            float pos=var_GetFloat(input,"position"); bool can=var_GetBool(input,"can-seek");
            EnterCriticalSection(&c->lock);
            c->state=state;c->length=length;c->time=time;c->position=pos;c->seekable=can;
            bool seek=c->pending_seek && c->seek_generation==c->generation && can;
            float target=c->seek; c->pending_seek=false;
            LeaveCriticalSection(&c->lock);
            if(seek) { var_SetFloat(input,"position",target); msg_Dbg(c->intf,"waveform seek %.6f",target); }
            vlc_object_release(input);
        }
        save_settings(c);
    }
    detach_menu(c);
    cancel_analysis(c);
    save_settings(c);
    return 0;
}
static void release_context(context_t *c)
{
    if(InterlockedDecrement(&c->references)) return;
    if(c->input) vlc_object_release(c->input);
    free(c->peaks);free(c->uri);
    CloseHandle(c->stop);CloseHandle(c->analysis_stop);CloseHandle(c->job);
    DeleteCriticalSection(&c->lock);free(c);
}
static int OpenInterface(vlc_object_t *obj)
{
    AcquireSRWLockExclusive(&global_lock);
    if(global) { ReleaseSRWLockExclusive(&global_lock);return VLC_EGENERIC; }
    context_t *c=calloc(1,sizeof(*c));
    if(!c) { ReleaseSRWLockExclusive(&global_lock);return VLC_ENOMEM; }
    InitializeCriticalSection(&c->lock);c->references=1; c->intf=(intf_thread_t *)obj; reset_view(c);
    c->stop=CreateEventW(NULL,TRUE,FALSE,NULL); c->analysis_stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    c->job=CreateJobObjectW(NULL,NULL);
    if(!c->stop || !c->analysis_stop || !c->job) { release_context(c);ReleaseSRWLockExclusive(&global_lock); return VLC_EGENERIC; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ji={0};ji.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(c->job,JobObjectExtendedLimitInformation,&ji,sizeof(ji));
    char *ff=var_InheritString(obj,"waveform-ffmpeg");
    wide(ff,c->ffmpeg,32768);
    free(ff);
    if(!c->ffmpeg[0]) wcscpy(c->ffmpeg,L"ffmpeg.exe");
    SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,c->cache_dir);
    wcscat(c->cache_dir,L"\\VLCWaveform");CreateDirectoryW(c->cache_dir,NULL);
    _snwprintf(c->settings_path,32768,L"%s\\waveform-settings.txt",c->cache_dir);
    char *settings=var_InheritString(obj,"waveform-settings");
    if(settings && settings[0])wide(settings,c->settings_path,32768);
    free(settings);
    load_settings(c);
    wcscat(c->cache_dir,L"\\cache");CreateDirectoryW(c->cache_dir,NULL);
    ((intf_thread_t *)obj)->p_sys=(void *)c;global=c;
    c->control=CreateThread(NULL,0,Control,c,0,NULL);
    if(!c->control) { global=NULL;release_context(c);ReleaseSRWLockExclusive(&global_lock);return VLC_EGENERIC; }
    ReleaseSRWLockExclusive(&global_lock);
    msg_Info(obj,"Native waveform loaded - no browser / no HTTP / no Python");
    return VLC_SUCCESS;
}
static void CloseInterface(vlc_object_t *obj)
{
    context_t *c=(void *)((intf_thread_t *)obj)->p_sys;
    AcquireSRWLockExclusive(&global_lock);global=NULL;ReleaseSRWLockExclusive(&global_lock);
    SetEvent(c->stop);WaitForSingleObject(c->control,INFINITE);CloseHandle(c->control);
    release_context(c);
}

static double mouse_position(context_t *c, int x)
{
    (void)c;
    return CLAMP((double)(x-LEFT)/(RIGHT-LEFT),0,0.999999);
}
static void view_bounds(int mode,int *top,int *bottom)
{
    *top=mode==1?112:90;*bottom=mode==1?174:HEIGHT-36;
}
static int MouseButton(vlc_object_t *obj, const char *name, vlc_value_t old, vlc_value_t val, void *data)
{
    (void)name;
    context_t *c=((visual_t *)data)->ctx;
    int x,y;var_GetCoords(obj,"mouse-moved",&x,&y);
    EnterCriticalSection(&c->lock);
    bool pressed=(val.i_int&1) && !(old.i_int&1);
    int top,bottom;view_bounds(c->view_mode,&top,&bottom);
    if(pressed && y>=27 && y<53 && x>=TOOL_X) {
        int button=(x-TOOL_X)/TOOL_STEP;
        if(button<4 && (x-TOOL_X)%TOOL_STEP<TOOL_BUTTON_W) {
            if(button==3) { c->enabled=false;c->pending_disable=true; }
            else c->view_mode=button;
            c->settings_dirty=true;
        }
    } else if(pressed && c->enabled && x>=LEFT && x<=RIGHT && y>=top-4 && y<=bottom+3 && c->input && c->seekable && c->length>0) {
        c->seek=(float)mouse_position(c,x);c->seek_generation=c->generation;c->pending_seek=true;
    }
    LeaveCriticalSection(&c->lock);return VLC_SUCCESS;
}
static void line(HDC dc,int x,int y,int x2,int y2,COLORREF color,int width)
{
    HPEN pen=CreatePen(PS_SOLID,width,color),old=SelectObject(dc,pen);
    MoveToEx(dc,x,y,NULL);LineTo(dc,x2,y2);SelectObject(dc,old);DeleteObject(pen);
}
static void text(HDC dc,int x,int y,const wchar_t *str,COLORREF color)
{ SetTextColor(dc,color);TextOutW(dc,x,y,str,(int)wcslen(str)); }
static void stamp(int64_t us,wchar_t *out,size_t n)
{
    if(us<0) us=0;
    int64_t ms=us/1000;
    _snwprintf(out,n,L"%02lld:%02lld:%02lld.%03lld",ms/3600000,(ms/60000)%60,(ms/1000)%60,ms%1000);
}
static void draw_toolbar(HDC dc,int mode,bool enabled)
{
    for(int i=0;i<4;i++) {
        int x=TOOL_X+i*TOOL_STEP;bool selected=i<3 && i==mode;
        COLORREF ink=i==3?RGB(255,158,99):selected?RGB(142,238,230):RGB(173,186,202);
        HBRUSH brush=CreateSolidBrush(selected?RGB(33,69,75):RGB(32,39,49));
        HPEN pen=CreatePen(PS_SOLID,1,selected?RGB(69,163,168):RGB(61,74,90));
        HGDIOBJ oldbrush=SelectObject(dc,brush),oldpen=SelectObject(dc,pen);
        RoundRect(dc,x,27,x+TOOL_BUTTON_W,53,8,8);
        SelectObject(dc,oldbrush);SelectObject(dc,oldpen);DeleteObject(brush);DeleteObject(pen);
        int cx=x+12,cy=40;
        if(i==0) {
            for(int k=-2;k<=2;k++) { int h=k==0?8:abs(k)==1?4:2;line(dc,cx+k*3,cy-h,cx+k*3,cy+h,ink,2); }
        } else if(i==1) {
            line(dc,cx-6,cy-5,cx+7,cy-5,ink,2);line(dc,cx-6,cy+5,cx+7,cy+5,ink,2);
            line(dc,cx-3,cy-1,cx+4,cy-1,ink,2);
        } else if(i==2) {
            HBRUSH hollow=GetStockObject(HOLLOW_BRUSH);oldbrush=SelectObject(dc,hollow);
            pen=CreatePen(PS_SOLID,1,ink);oldpen=SelectObject(dc,pen);
            Ellipse(dc,cx-6,cy-7,cx+7,cy+7);
            SelectObject(dc,oldbrush);SelectObject(dc,oldpen);DeleteObject(pen);
            for(int k=-4;k<=0;k++)line(dc,cx+k,cy-4,cx+k,cy+5,ink,1);
        } else {
            pen=CreatePen(PS_SOLID,2,ink);oldpen=SelectObject(dc,pen);oldbrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Arc(dc,cx-6,cy-6,cx+7,cy+7,cx-3,cy-6,cx+3,cy-6);
            SelectObject(dc,oldbrush);SelectObject(dc,oldpen);DeleteObject(pen);
            line(dc,cx,cy-8,cx,cy,ink,2);
        }
        text(dc,x+23,32,i==3?L"Aus":view_labels[i],ink);
    }
}
static bool draw(visual_t *v)
{
    context_t *c=v->ctx;
    wchar_t title[1024],error[256];
    int64_t length,at; int state,maxabs,x,mode; bool analyzing,rebuild,enabled;
    ULONGLONG now=GetTickCount64();
    /* Snapshot only. GDI drawing and picture delivery must never hold the
       controller/analyzer lock. Reaggregate growing peaks at most twice/sec. */
    EnterCriticalSection(&c->lock);
    length=c->length;at=c->time;state=c->state;analyzing=c->analyzing;
    mode=c->view_mode;enabled=c->enabled;
    maxabs=c->max_amplitude>0?c->max_amplitude:1;
    x=LEFT+(int)(c->position*(RIGHT-LEFT));
    rebuild=!v->frames || v->drawn_mode!=mode || v->drawn_enabled!=enabled || v->drawn_generation!=c->generation || v->drawn_length!=length ||
        v->drawn_analyzing!=analyzing || wcscmp(v->drawn_error,c->error) ||
        (v->drawn_count!=c->count && now-v->rebuilt_at>=500);
    if(rebuild) {
        wcscpy(title,c->title);wcscpy(error,c->error);
        double total=(double)length/10000.0;
        for(unsigned col=0;col<RIGHT-LEFT;col++) {
            unsigned i=(unsigned)(col*total/(RIGHT-LEFT));
            unsigned end=(unsigned)((col+1)*total/(RIGHT-LEFT))+1;
            if(end>c->count)end=c->count;
            int lo=0,hi=0;
            for(;i<end;i++) { if(c->peaks[i].lo<lo)lo=c->peaks[i].lo;if(c->peaks[i].hi>hi)hi=c->peaks[i].hi; }
            v->columns[col]=(peak_t){lo,hi};
        }
        v->drawn_count=c->count;v->drawn_generation=c->generation;v->drawn_length=length;
        v->drawn_analyzing=analyzing;v->drawn_max=maxabs;wcscpy(v->drawn_error,error);
        v->drawn_mode=mode;v->drawn_enabled=enabled;
    }
    LeaveCriticalSection(&c->lock);
    if(!rebuild && v->drawn_x==x && v->drawn_time==at/250000 && v->drawn_state==state) return false;
    int top,bottom;view_bounds(mode,&top,&bottom);int mid=(top+bottom)/2;
    bool contrast=mode==2;
    if(rebuild) {
        HDC dc=v->base_dc;
        RECT all={0,0,WIDTH,HEIGHT};HBRUSH bg=CreateSolidBrush(contrast?RGB(0,0,0):RGB(19,23,29));FillRect(dc,&all,bg);DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);SelectObject(dc,v->font);
        text(dc,LEFT,5,title[0]?title:L"VLC Waveform",RGB(228,232,238));
        SelectObject(dc,v->small_font);
        draw_toolbar(dc,mode,enabled);
        int ticks=mode==1?3:5;
        for(int tick=0;enabled && tick<=ticks;tick++) {
            int px=LEFT+(RIGHT-LEFT)*tick/ticks;
            line(dc,px,top-4,px,bottom,contrast?RGB(93,93,93):RGB(35,42,52),1);
            wchar_t label[64];stamp(length*tick/ticks,label,64);label[wcslen(label)-4]=0;
            text(dc,CLAMP(px-24,LEFT,RIGHT-70),top-23,length<3600000000LL?label+3:label,contrast?RGB(255,255,255):RGB(126,140,158));
        }
        if(enabled)line(dc,LEFT,mid,RIGHT,mid,RGB(57,68,81),1);
        HPEN pen=CreatePen(PS_SOLID,contrast?2:1,contrast?RGB(220,255,61):mode==1?RGB(115,171,249):RGB(70,194,203)),old=SelectObject(dc,pen);
        double gain=(bottom-top)*0.46/v->drawn_max;
        for(int col=0;enabled && col<RIGHT-LEFT;col++) {
            peak_t p=v->columns[col];
            if(mode!=1 || col%3==0) {
                int hi=mode==1?(abs(p.lo)>p.hi?abs(p.lo):p.hi):p.hi;
                int lo=mode==1?-hi:p.lo;
                MoveToEx(dc,col+LEFT,mid-(int)(hi*gain),NULL);LineTo(dc,col+LEFT,mid-(int)(lo*gain)+1);
            }
        }
        SelectObject(dc,old);DeleteObject(pen);
        if(!enabled)text(dc,LEFT,112,L"Ausgeschaltet - wieder an: Audio > Visualisierungen > Wellenform",RGB(228,232,238));
        text(dc,LEFT,HEIGHT-22,error[0]?error:analyzing?L"Wellenform wird analysiert ...":L"Klick: Position setzen | Leertaste: abspielen / pausieren",error[0]?RGB(246,126,113):RGB(145,160,178));
        GdiFlush();v->rebuilt_at=now;v->rebuilds++;
    }
    BitBlt(v->dc,0,0,WIDTH,HEIGHT,v->base_dc,0,0,SRCCOPY);
    SetBkMode(v->dc,TRANSPARENT);SelectObject(v->dc,v->small_font);
    wchar_t a[64],b[64],info[256];stamp(at,a,64);stamp(length,b,64);
    _snwprintf(info,256,L"%s / %s   %s",a,b,state==PAUSE_S?L"PAUSE":state==PLAYING_S?L"WIEDERGABE":L"STOPP");
    text(v->dc,LEFT,30,info,contrast?RGB(255,255,255):RGB(255,169,63));
    if(enabled && x>=LEFT && x<=RIGHT)line(v->dc,x,top-4,x,bottom+3,contrast?RGB(255,255,255):RGB(255,164,54),contrast?3:2);
    GdiFlush();v->drawn_x=x;v->drawn_time=at/250000;v->drawn_state=state;
    return true;
}
static DWORD WINAPI Render(void *data)
{
    visual_t *v=data;
    bool dirty=false;
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    while(WaitForSingleObject(v->stop,125)==WAIT_TIMEOUT) {
        if(WaitForSingleObject(v->ctx->stop,0)==WAIT_OBJECT_0) break;
        if(draw(v))dirty=true;
        if(!dirty)continue;
        /* One native SPU for the whole visual lifetime, one pending bitmap.
           Coalesce replacements if the vout stalls: no command/frame backlog
           and no recurring decoder-pool wait. Retry allocations while dirty. */
        video_format_t fmt;video_format_Init(&fmt,VLC_CODEC_RGBA);
        fmt.i_width=fmt.i_visible_width=WIDTH;fmt.i_height=fmt.i_visible_height=HEIGHT;
        fmt.i_sar_num=fmt.i_sar_den=1;
        subpicture_region_t *region=subpicture_region_New(&fmt);
        video_format_Clean(&fmt);
        if(!region) continue;
        for(int y=0;y<HEIGHT;y++) {
            uint8_t *dest=region->p_picture->p[0].p_pixels+y*region->p_picture->p[0].i_pitch;
            const uint8_t *src=v->pixels+y*WIDTH*4;
            for(int x=0;x<WIDTH;x++) { dest[4*x]=src[4*x+2];dest[4*x+1]=src[4*x+1];dest[4*x+2]=src[4*x];dest[4*x+3]=255; }
        }
        replace_canvas(v->canvas,region);dirty=false;
        v->frames++;
    }
    return 0;
}
static block_t *Audio(filter_t *f,block_t *b) { (void)f;return b; }
static int OpenVisual(vlc_object_t *obj)
{
    filter_t *f=(filter_t *)obj;
    AcquireSRWLockShared(&global_lock);context_t *c=global;
    if(c && c->intf->obj.libvlc==obj->obj.libvlc)InterlockedIncrement(&c->references);else c=NULL;
    ReleaseSRWLockShared(&global_lock);
    if(!c) { msg_Err(obj,"Enable extraintf=waveformintf first");return VLC_EGENERIC; }
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;LeaveCriticalSection(&c->lock);
    if(!enabled) { release_context(c);return VLC_EGENERIC; }
    input_thread_t *input=playlist_CurrentInput(pl_Get(c->intf));
    bool video=input && var_CountChoices(input,"video-es")>1;
    if(input)vlc_object_release(input);
    if(video) {
        msg_Dbg(obj,"Video track present: leave VLC's video output unchanged");
        release_context(c);return VLC_EGENERIC;
    }
    visual_t *v=calloc(1,sizeof(*v));if(!v){release_context(c);return VLC_ENOMEM;}
    v->ctx=c;v->filter=f;
    v->canvas=calloc(1,sizeof(*v->canvas));
    if(!v->canvas)goto fail;
    InitializeCriticalSection(&v->canvas->lock);v->canvas->references=1;
    video_format_t fmt={0};video_format_Init(&fmt,VLC_CODEC_RGB32);
    fmt.i_width=fmt.i_visible_width=WIDTH;fmt.i_height=fmt.i_visible_height=HEIGHT;
    fmt.i_sar_num=fmt.i_sar_den=1;
    fmt.i_rmask=0x00ff0000;fmt.i_gmask=0x0000ff00;fmt.i_bmask=0x000000ff;
    v->vout=aout_filter_RequestVout(f,NULL,&fmt);
    if(!v->vout) goto fail;
    /* One seed on the newly requested visualization vout. Released waveform
       vouts are not recycled by aout_filter_RequestVout. Never acquire again. */
    picture_t *seed=vout_GetPicture(v->vout);
    if(!seed) goto fail;
    for(int y=0;y<HEIGHT;y++)memset(seed->p[0].p_pixels+y*seed->p[0].i_pitch,0,WIDTH*4);
    seed->b_progressive=true;seed->b_force=true;seed->date=mdate();
    vout_PutPicture(v->vout,seed);
    v->spu_channel=vout_RegisterSubpictureChannel(v->vout);
    subpicture_updater_t updater={.pf_validate=ValidateCanvas,.pf_update=UpdateCanvas,
        .pf_destroy=DestroyCanvas,.p_sys=(void *)v->canvas};
    subpicture_t *sp=subpicture_New(&updater);
    if(!sp)goto fail;
    InterlockedIncrement(&v->canvas->references);
    sp->i_channel=v->spu_channel;sp->i_start=mdate();sp->i_stop=0;sp->b_ephemer=true;
    sp->b_subtitle=false;sp->b_absolute=true;sp->i_alpha=255;
    sp->i_original_picture_width=WIDTH;sp->i_original_picture_height=HEIGHT;
    vout_PutSubpicture(v->vout,sp);
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=WIDTH;bi.bmiHeader.biHeight=-HEIGHT;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    v->dc=CreateCompatibleDC(NULL);
    v->base_dc=CreateCompatibleDC(NULL);
    v->bitmap=CreateDIBSection(v->dc,&bi,DIB_RGB_COLORS,(void **)&v->pixels,NULL,0);
    if(!v->dc || !v->base_dc || !v->bitmap)goto fail;
    v->old_bitmap=SelectObject(v->dc,v->bitmap);
    /* The memory DC initially contains a monochrome bitmap. Create the cached
       surface from the selected 32-bit DIB, not that default 1-bit surface. */
    v->base_bitmap=CreateCompatibleBitmap(v->dc,WIDTH,HEIGHT);
    if(!v->base_bitmap)goto fail;
    v->old_base_bitmap=SelectObject(v->base_dc,v->base_bitmap);
    v->font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    v->small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    v->stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!v->stop)goto fail;
    var_AddCallback(v->vout,"mouse-button-down",MouseButton,v);
    v->thread=CreateThread(NULL,0,Render,v,0,NULL);
    if(!v->thread) {
        var_DelCallback(v->vout,"mouse-button-down",MouseButton,v);goto fail;
    }
    f->p_sys=(void *)v;f->fmt_in.audio.i_format=VLC_CODEC_FL32;
    f->fmt_out.audio=f->fmt_in.audio;f->pf_audio_filter=Audio;
    msg_Info(obj,"Native waveform video output ready");return VLC_SUCCESS;
fail:
    if(v->stop)CloseHandle(v->stop);
    if(v->dc && v->old_bitmap)SelectObject(v->dc,v->old_bitmap);
    if(v->base_dc && v->old_base_bitmap)SelectObject(v->base_dc,v->old_base_bitmap);
    if(v->base_bitmap)DeleteObject(v->base_bitmap);if(v->base_dc)DeleteDC(v->base_dc);
    if(v->bitmap)DeleteObject(v->bitmap);if(v->dc)DeleteDC(v->dc);
    if(v->font)DeleteObject(v->font);if(v->small_font)DeleteObject(v->small_font);
    if(v->vout)aout_filter_RequestVout(f,v->vout,NULL);
    if(v->canvas)release_canvas(v->canvas);
    release_context(c);free(v);return VLC_EGENERIC;
}
static void CloseVisual(vlc_object_t *obj)
{
    filter_t *f=(filter_t *)obj;visual_t *v=(void *)f->p_sys;
    SetEvent(v->stop);
    var_DelCallback(v->vout,"mouse-button-down",MouseButton,v);
    WaitForSingleObject(v->thread,INFINITE);
    CloseHandle(v->thread);CloseHandle(v->stop);
    vout_FlushSubpictureChannel(v->vout,v->spu_channel);
    aout_filter_RequestVout(f,v->vout,NULL);
    /* RequestVout(...,NULL) closes (does not recycle) this vout synchronously,
       so no SPU callback can run after returning/unloading this filter. */
    msg_Dbg(f,"waveform render frames=%u static=%u applied=%u",v->frames,v->rebuilds,v->canvas->applied);
    release_canvas(v->canvas);
    SelectObject(v->base_dc,v->old_base_bitmap);DeleteObject(v->base_bitmap);DeleteDC(v->base_dc);
    SelectObject(v->dc,v->old_bitmap);DeleteObject(v->bitmap);DeleteDC(v->dc);
    DeleteObject(v->font);DeleteObject(v->small_font);
    release_context(v->ctx);free(v);
}
