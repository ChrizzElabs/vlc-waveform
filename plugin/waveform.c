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
#include <vlc_mouse.h>

#define WIDTH 800
#define HEIGHT 450
#define LEFT 20
#define RIGHT (WIDTH-28)
#define MAX_BUCKETS 8640000u /* bounded to 24 hours at 10 ms */
#define NET_CACHE_LIMIT (16u*1024u*1024u)
#define TOOL_COUNT 7
#define TOOL_BUTTON_W 100
#define TOOL_STEP 106
#define TOOL_X LEFT
#define OPTIONS_Y 69
#define TIME_CELL 11
#define TIME_FIELD_CELLS 16
#define TIME_GAP 24
#define TIME_TOTAL_X (RIGHT-TIME_FIELD_CELLS*TIME_CELL)
#define TIME_CURRENT_X (TIME_TOTAL_X-TIME_GAP-TIME_FIELD_CELLS*TIME_CELL)
#define TIME_PAUSE_X (TIME_CURRENT_X-16)
#define OPT_STEP 94
#define OPT_WIDTH 88
#define PEAK_GROUP 64u
#define RASTER_STEP 5u
#define SWATCH_STEP 27
#define SWATCH_X (TOOL_X+TOOL_COLOR*TOOL_STEP)
#define CLAP_GAP 12u /* 120 ms between separate transient markers */
#define FPS_COUNT (sizeof(frame_rates)/sizeof(frame_rates[0]))
#define DEFAULT_FPS 3u /* 25 fps; indexes in existing settings are one-based */
#define CLAMP(v,a,b) ((v)<(a)?(a):((v)>(b)?(b):(v)))
typedef struct { int16_t lo, hi; } peak_t;
enum { OPT_HH, OPT_MM, OPT_SS, OPT_MS, OPT_FF, OPT_FPS_DOWN, OPT_FPS_UP, OPT_LANGUAGE, OPT_COUNT };
enum { TIME_HH=1<<OPT_HH, TIME_MM=1<<OPT_MM, TIME_SS=1<<OPT_SS, TIME_MS=1<<OPT_MS, TIME_FF=1<<OPT_FF,
       TIME_BASE=TIME_MM|TIME_SS, TIME_ALL=TIME_BASE|TIME_HH|TIME_MS|TIME_FF };
/* Historical time_preset IDs and their equivalent bitsets. */
static const unsigned prior_time_presets[]={TIME_BASE,TIME_HH|TIME_BASE,TIME_BASE|TIME_MS,TIME_HH|TIME_BASE|TIME_FF};
enum { LANG_DE, LANG_EN };
enum { VIEW_CONTOUR, VIEW_PULSE, VIEW_GRID, VIEW_COUNT };
enum { TOOL_COLOR=VIEW_COUNT, TOOL_CLAP, TOOL_OPTIONS, TOOL_OFF };
enum { COLOR_MINT, COLOR_BLUE, COLOR_RED, COLOR_WHITE, COLOR_COUNT=8 };
enum { ERR_NONE, ERR_DECODE, ERR_STREAM, ERR_WORKER };
static const unsigned frame_rates[]={23976,24000,25000,29970,30000,48000,50000,59940,60000,120000};
typedef struct {
    CRITICAL_SECTION lock;
    HANDLE stop, control, analyzer, analysis_stop, job;
    intf_thread_t *intf;
    input_thread_t *input;
    unsigned generation;
    char *uri;
    wchar_t path[32768], ffmpeg[32768], cache_dir[32768], settings_path[32768];
    wchar_t title[1024];
    unsigned error;
    peak_t *peaks;
    unsigned *claps, clap_count;
    bool claps_ready;
    peak_t groups[(MAX_BUCKETS+PEAK_GROUP-1)/PEAK_GROUP];
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
    unsigned colors[VIEW_COUNT];
    bool show_claps;
    unsigned time_format, fps_index, language;
    ULONGLONG settings_retry;
    bool enabled, settings_dirty, pending_disable;
    audio_output_t *menu_aout;
    DWORD menu_restore_thread;
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
    bool clap_columns[RIGHT-LEFT],drawn_claps_ready;
    unsigned drawn_count, drawn_generation;
    int64_t drawn_length, drawn_time;
    int drawn_max, drawn_state, drawn_x, drawn_mode;
    unsigned drawn_format, drawn_fps, drawn_language, drawn_error;
    bool options, drawn_options;
    unsigned drawn_color;
    bool palette_open, drawn_palette, drawn_show_claps;
    bool drawn_analyzing, drawn_enabled;
    ULONGLONG rebuilt_at;
    unsigned frames, rebuilds;
    int spu_channel;
    canvas_t *canvas;
} visual_t;

static const char menu_key[]="waveform";
static const wchar_t *const view_labels[2][TOOL_COUNT]={
    {L"Kontur",L"Puls",L"Raster",L"Farbe",L"Klappe",L"Optionen",L"Aus"},
    {L"Contour",L"Pulse",L"Grid",L"Color",L"Slate",L"Options",L"Off"}
};
static const COLORREF palette[COLOR_COUNT]={
    RGB(76,216,196),RGB(133,174,253),RGB(246,87,110),RGB(239,242,247),
    RGB(245,199,83),RGB(177,226,104),RGB(197,151,255),RGB(255,146,207)
};
static const unsigned default_colors[VIEW_COUNT]={COLOR_MINT,COLOR_BLUE,COLOR_RED};
static const char *const color_keys[VIEW_COUNT]={"color_contour","color_pulse","color_grid"};
#define MOUSE_FILTER "waveformmouse"
#define MOUSE_OWNER "waveform-mouse-owner"

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
static int OpenMouseFilter(vlc_object_t *);

vlc_module_begin()
    set_shortname(N_("Waveform"))
    set_description(N_("VLC Waveform - native waveform with five views"))
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
    add_submodule()
    set_description(N_("Waveform mouse input"))
    set_capability("video filter", 0)
    add_shortcut(MOUSE_FILTER)
    set_callbacks(OpenMouseFilter, NULL)
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
    if(c->failed)return -1;
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
static unsigned effective_fps(unsigned index) { return index && index<=FPS_COUNT?index:DEFAULT_FPS; }
static int setting_value(const char *line,const char *key,int limit)
{
    size_t n=strlen(key);if(strncmp(line,key,n)||line[n]!='=')return -1;
    const char *s=line+n+1;int value=0;
    if(*s<'0'||*s>'9')return -1;
    do { value=value*10+(*s++-'0');if(value>=limit)return -1; }while(*s>='0'&&*s<='9');
    return *s=='\r'||*s=='\n'||!*s?value:-1;
}
static void load_settings(context_t *c)
{
    c->enabled=true;c->view_mode=VIEW_CONTOUR;c->show_claps=false;
    memcpy(c->colors,default_colors,sizeof(c->colors));
    c->time_format=TIME_BASE|TIME_MS;c->fps_index=DEFAULT_FPS;c->language=LANG_DE;
    FILE *file=_wfopen(c->settings_path,L"rb");
    if(!file) return;
    char line[96];int enabled,mode;
    if(fgets(line,sizeof(line),file)&&(!strcmp(line,"VLCWaveform 1\n")||!strcmp(line,"VLCWaveform 1\r\n"))&&
       fgets(line,sizeof(line),file)&&(enabled=setting_value(line,"enabled",2))>=0&&
       fgets(line,sizeof(line),file)&&(mode=setting_value(line,"view",3))>=0) {
        int form=-1,claps=-1,colors[VIEW_COUNT]={-1,-1,-1};
        int time_fields=-1,time_preset=-1,legacy_time=1,legacy_pad=0;
        c->enabled=enabled!=0;
        while(fgets(line,sizeof(line),file)) {
            int v=setting_value(line,"selected_view",5);if(v>=0)mode=v;
            v=setting_value(line,"form",VIEW_COUNT);if(v>=0)form=v;
            v=setting_value(line,"clap_overlay",2);if(v>=0)claps=v;
            for(int i=0;i<VIEW_COUNT;i++) {
                v=setting_value(line,color_keys[i],COLOR_COUNT);if(v>=0)colors[i]=v;
            }
            v=setting_value(line,"time_format",3);if(v>=0)legacy_time=v;
            v=setting_value(line,"time_preset",4);if(v>=0)time_preset=v;
            v=setting_value(line,"time_fields",TIME_ALL+1);if(v>0)time_fields=v;
            v=setting_value(line,"fps_index",FPS_COUNT+1);if(v>=0)c->fps_index=effective_fps((unsigned)v);
            v=setting_value(line,"language",2);if(v>=0)c->language=(unsigned)v;
            v=setting_value(line,"pad_time",2);if(v>=0)legacy_pad=v;
        }
        c->time_format=time_fields>0?(unsigned)time_fields:time_preset>=0?prior_time_presets[time_preset]:
                       TIME_BASE|(legacy_pad||legacy_time==2?TIME_HH:0)|(legacy_time==2?TIME_FF:legacy_time==1?TIME_MS:0);
        c->view_mode=form>=0?form:mode==1?VIEW_PULSE:mode==4?VIEW_GRID:VIEW_CONTOUR;
        c->show_claps=claps>=0?claps!=0:mode==3;
        if(mode==2)c->colors[VIEW_CONTOUR]=COLOR_WHITE;
        for(int i=0;i<VIEW_COUNT;i++)if(colors[i]>=0)c->colors[i]=(unsigned)colors[i];
    }
    fclose(file);
}
static void save_settings(context_t *c)
{
    EnterCriticalSection(&c->lock);
    ULONGLONG now=GetTickCount64();
    bool dirty=c->settings_dirty&&now>=c->settings_retry,enabled=c->enabled,claps=c->show_claps;
    int mode=c->view_mode;
    unsigned colors[VIEW_COUNT];memcpy(colors,c->colors,sizeof(colors));
    unsigned format=c->time_format,fps=c->fps_index,language=c->language;
    if(dirty)c->settings_dirty=false;
    LeaveCriticalSection(&c->lock);
    if(!dirty) return;
    wchar_t tmp[32768];_snwprintf(tmp,32768,L"%s.%lu.tmp",c->settings_path,GetCurrentProcessId());
    FILE *file=_wfopen(tmp,L"wb");bool ok=false;
    if(file) {
        int fallback=mode==VIEW_GRID?4:mode==VIEW_PULSE?1:claps?3:colors[VIEW_CONTOUR]==COLOR_WHITE?2:0;
        unsigned preset=format&TIME_FF?3u:format&TIME_MS?2u:format&TIME_HH?1u:0u;
        unsigned legacy_format=preset==3?2u:preset==2?1u:0u;
        ok=fprintf(file,"VLCWaveform 1\nenabled=%d\nview=%d\nselected_view=%d\ntime_format=%u\nfps_index=%u\nlanguage=%u\nform=%d\nclap_overlay=%d\npad_time=%d\ntime_preset=%u\ntime_fields=%u\n",
                   enabled,fallback<3?fallback:0,fallback,legacy_format,fps,language,mode,claps,(format&TIME_HH)!=0,preset,format)>0;
        for(int i=0;ok&&i<VIEW_COUNT;i++)ok=fprintf(file,"%s=%u\n",color_keys[i],colors[i])>0;
        if(fclose(file)) ok=false;
        if(ok) ok=MoveFileExW(tmp,c->settings_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    }
    if(!ok) {
        DeleteFileW(tmp);
        EnterCriticalSection(&c->lock);c->settings_dirty=true;c->settings_retry=now+2000;LeaveCriticalSection(&c->lock);
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
    char *key=var_GetString(obj,"visual");
    bool translate=val.psz_string && !strcmp(val.psz_string,"visual") && key && !strcmp(key,menu_key);
    free(key);
    bool enabled=translate || (val.psz_string && !strcmp(val.psz_string,"waveformvis"));
    EnterCriticalSection(&c->lock);
    bool reset=translate && c->menu_restore_thread!=GetCurrentThreadId();
    if(c->enabled!=enabled || (reset && c->view_mode!=VIEW_CONTOUR)) c->settings_dirty=true;
    c->enabled=enabled;if(reset)c->view_mode=VIEW_CONTOUR;
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
    vlc_value_t choice={.psz_string=(char *)menu_key};
    var_Change(c->menu_aout,"visual",VLC_VAR_DELCHOICE,&choice,NULL);
    vlc_object_release(c->menu_aout);c->menu_aout=NULL;
}
static void reconcile_menu(context_t *c)
{
    audio_output_t *aout=playlist_GetAout(pl_Get(c->intf));
    if(aout==c->menu_aout) { if(aout)vlc_object_release(aout);return; }
    detach_menu(c);c->menu_aout=aout;
    if(!aout) return;
    vlc_value_t choice={.psz_string=(char *)menu_key},label={.psz_string="Wellenform"};
    var_Change(aout,"visual",VLC_VAR_ADDCHOICE,&choice,&label);
    var_AddCallback(aout,"audio-visual",AudioVisualChanged,c);
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;LeaveCriticalSection(&c->lock);
    char *active=var_GetString(aout,"audio-visual");
    if(active && !strcmp(active,"waveformvis")) {
        vlc_value_t selection={.psz_string=(char *)(enabled?menu_key:"")};
        var_Change(aout,"visual",VLC_VAR_SETVALUE,&selection,NULL);
        if(!enabled) var_SetString(aout,"visual","");
    } else if(active && !*active && enabled) {
        /* A replacement aout may inherit an empty default. Restore the saved
           plugin choice, never replace a named visualizer or explicit none. */
        EnterCriticalSection(&c->lock);c->menu_restore_thread=GetCurrentThreadId();LeaveCriticalSection(&c->lock);
        var_SetString(aout,"visual",menu_key);
        EnterCriticalSection(&c->lock);c->menu_restore_thread=0;LeaveCriticalSection(&c->lock);
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
#define PEAK_CACHE_VERSION 2u /* older entries may come from incomplete reads */
static int amplitude(peak_t p) { return abs(p.lo)>p.hi?abs(p.lo):p.hi; }
/* Compare with room tone, not digital silence. Allow an occasional tiny bump,
   but reject sustained activity or another substantial peak on either side. */
static bool quiet_clap_context(const peak_t *p,unsigned start,unsigned end,int peak)
{
    unsigned n=end-start,active=0,loud=0;
    if(n<12)return false; /* a file boundary is not evidence of quiet */
    for(unsigned k=start;k<end;k++) {
        int a=amplitude(p[k]);
        if(a*4>peak && ++loud>1)return false;
        if(a*7>peak && ++active>n/6)return false;
    }
    return true;
}
/* Peaks are 10 ms apart; the window is independent of song length and pixels. */
static bool clap_candidate(const peak_t *p,unsigned count,unsigned i,int max)
{
    if(i<4 || i+8>=count)return false;
    int a=amplitude(p[i]);
    int head=a;
    for(unsigned j=1;j<=2;j++)if(amplitude(p[i+j])>head)head=amplitude(p[i+j]);
    if(head<240 || head*4<max || a*2<head)return false;
    for(unsigned j=1;j<=4;j++)if(amplitude(p[i-j])*5>head*2)return false;
    /* Allow a short multipulse clap, but require its energy to fall quickly. */
    unsigned quiet=0;
    for(unsigned j=3;j<=8;j++)if(amplitude(p[i+j])*5<=head*2)quiet++;
    if(quiet<4)return false;
    /* Look beyond short gaps between syllables; omit the impact and its tail. */
    unsigned before=i>28?i-28:0,after=i+33<count?i+33:count;
    return quiet_clap_context(p,before,i-4,head) && quiet_clap_context(p,i+9,after,head);
}
static void build_claps(context_t *c)
{
    /* Only the analyzer owns peak writes; track replacement joins this worker.
       Keep the potentially long scan outside the renderer/controller lock. */
    unsigned n=c->count,found=0;
    unsigned *list=malloc((n/CLAP_GAP+1)*sizeof(*list));
    if(!list)return;
    for(unsigned i=4,check=0;i+8<n;) {
        if(i>=check) {
            if(WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0){free(list);return;}
            check=i+4096;
        }
        if(!clap_candidate(c->peaks,n,i,c->max_amplitude)){i++;continue;}
        unsigned strongest=i;
        for(unsigned j=i+1;j<i+3 && j<n;j++)
            if(amplitude(c->peaks[j])>amplitude(c->peaks[strongest]))strongest=j;
        list[found++]=strongest;
        i+=CLAP_GAP;
    }
    EnterCriticalSection(&c->lock);
    free(c->claps);c->claps=list;c->clap_count=found;c->claps_ready=true;
    LeaveCriticalSection(&c->lock);
}
static bool load_cache(context_t *c, const wchar_t *path)
{
    FILE *f=_wfopen(path,L"rb"); if(!f) return false;
    cache_header_t h;
    bool ok=fread(&h,sizeof(h),1,f)==1 && h.magic==0x31504657 && h.version==PEAK_CACHE_VERSION && h.bucket_us==10000 && h.count>0 && h.count<=MAX_BUCKETS;
    peak_t *p=ok?malloc((size_t)h.count*sizeof(*p)):NULL;
    if(!p) ok=false;
    if(ok) ok=fread(p,sizeof(*p),h.count,f)==h.count && fgetc(f)==EOF;
    fclose(f);
    if(ok) {
        unsigned groups=(h.count+PEAK_GROUP-1)/PEAK_GROUP;
        peak_t *summary=calloc(groups,sizeof(*summary));
        if(!summary){free(p);return false;}
        int max=1;
        for(unsigned i=0;i<h.count;i++) {
            if(i%16384==0 && WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0){free(summary);free(p);return false;}
            int a=abs(p[i].lo),b=abs(p[i].hi);
            if(a>max)max=a;if(b>max)max=b;
            peak_t *g=&summary[i/PEAK_GROUP];if(p[i].lo<g->lo)g->lo=p[i].lo;if(p[i].hi>g->hi)g->hi=p[i].hi;
        }
        EnterCriticalSection(&c->lock);
        free(c->peaks); c->peaks=p; c->count=c->capacity=h.count;c->max_amplitude=max;
        memcpy(c->groups,summary,groups*sizeof(*summary));
        c->complete=true; c->analyzing=false;
        LeaveCriticalSection(&c->lock);
        free(summary);
    } else free(p);
    return ok;
}
static void save_cache(context_t *c, const wchar_t *path)
{
    wchar_t tmp[32768]; _snwprintf(tmp,32768,L"%s.%lu.tmp",path,GetCurrentProcessId());
    FILE *f=_wfopen(tmp,L"wb"); if(!f) return;
    /* Only the analysis worker writes peaks; track replacement joins it. */
    cache_header_t h={0x31504657,PEAK_CACHE_VERSION,c->count,10000};
    bool ok=fwrite(&h,sizeof(h),1,f)==1 && fwrite(c->peaks,sizeof(peak_t),c->count,f)==c->count;
    if(fclose(f)) ok=false;
    if(!ok || !MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))DeleteFileW(tmp);
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
        unsigned start=c->count;
        memcpy(c->peaks+start,p,count*sizeof(*p)); c->count+=count;
        for(unsigned i=0;i<count;i++) {
            unsigned j=start+i;peak_t *g=&c->groups[j/PEAK_GROUP];
            if(j%PEAK_GROUP==0)*g=(peak_t){0,0};
            if(p[i].lo<g->lo)g->lo=p[i].lo;if(p[i].hi>g->hi)g->hi=p[i].hi;
            int a=abs(p[i].lo),b=abs(p[i].hi);
            if(a>c->max_amplitude)c->max_amplitude=a;
            if(b>c->max_amplitude)c->max_amplitude=b;
        }
    }
    LeaveCriticalSection(&c->lock);
    return ok;
}
/* Exact extrema, using summaries only for complete, aligned groups. */
static peak_t peak_range(const context_t *c,unsigned i,unsigned end)
{
    peak_t out={0,0};
    while(i<end) {
        bool group=i%PEAK_GROUP==0 && end-i>=PEAK_GROUP;
        peak_t p=group?c->groups[i/PEAK_GROUP]:c->peaks[i];
        if(p.lo<out.lo)out.lo=p.lo;if(p.hi>out.hi)out.hi=p.hi;
        i+=group?PEAK_GROUP:1;
    }
    return out;
}
static bool decoder_completed(HANDLE stop,HANDLE process,DWORD read_error,DWORD timeout)
{
    if(read_error!=ERROR_BROKEN_PIPE)return false; /* anonymous pipe EOF */
    HANDLE events[]={stop,process}; /* cancellation wins if both are signaled */
    DWORD code;
    return WaitForMultipleObjects(2,events,FALSE,timeout)==WAIT_OBJECT_0+1 &&
           GetExitCodeProcess(process,&code) && code==0;
}
static DWORD WINAPI Analyze(void *data)
{
    context_t *c=data;
    SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
    wchar_t cache[32768];
    bool cached=cache_path(c,cache,32768);
    if(cached && load_cache(c,cache)) { build_claps(c);msg_Dbg(c->intf,"waveform cache hit"); return 0; }
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    HANDLE rd=NULL,wr=NULL,nul=INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi={0}; bool success=false;
    if(WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0)goto done;
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
    if(ResumeThread(pi.hThread)==(DWORD)-1)goto done;
    CloseHandle(pi.hThread); pi.hThread=NULL;
    CloseHandle(wr); wr=NULL;
    uint8_t buffer[65536]; bool have_low=false; uint8_t low=0;
    int sample_count=0; int16_t lo=32767,hi=-32768;
    peak_t peaks[128]; unsigned np=0;
    for(;;) {
        if(WaitForSingleObject(c->analysis_stop,0)==WAIT_OBJECT_0) break;
        DWORD n=0;
        /* Dedicated, cancellable blocking reads avoid throttling decoding by
         * sleeping once per small pipe buffer (especially on long recordings). */
        if(!ReadFile(rd,buffer,sizeof(buffer),&n,NULL)) {
            DWORD read_error=GetLastError();
            /* Do not publish a cache after non-EOF errors, a torn sample, or
               a child that closed stdout without completing successfully. */
            success=!have_low && decoder_completed(c->analysis_stop,pi.hProcess,read_error,5000);
            break;
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
    c->analyzing=false; c->complete=!canceled && success && c->count>0;
    if(!canceled && !c->complete) c->error=ERR_DECODE;
    bool complete=c->complete;
    LeaveCriticalSection(&c->lock);
    if(!canceled && complete) {
        build_claps(c);
        if(cached && WaitForSingleObject(c->analysis_stop,0)!=WAIT_OBJECT_0)save_cache(c,cache);
    }
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
    c->count=0; c->max_amplitude=1; c->complete=false; c->error=ERR_NONE; c->analyzing=false;
    free(c->claps);c->claps=NULL;c->clap_count=0;c->claps_ready=false;
    c->position=0;c->time=0;c->length=0;c->seekable=false; reset_view(c);
    if(input && !path) c->error=ERR_STREAM;
    LeaveCriticalSection(&c->lock);
    free(name); free(path);
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;LeaveCriticalSection(&c->lock);
    if(input && enabled && c->local && var_CountChoices(input,"video-es")<=1) {
        EnterCriticalSection(&c->lock);c->analyzing=true;LeaveCriticalSection(&c->lock);
        c->analyzer=CreateThread(NULL,0,Analyze,c,0,NULL);
        if(!c->analyzer) { EnterCriticalSection(&c->lock); c->analyzing=false; c->error=ERR_WORKER; LeaveCriticalSection(&c->lock); }
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
    free(c->peaks);free(c->claps);free(c->uri);
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
    if(!SetInformationJobObject(c->job,JobObjectExtendedLimitInformation,&ji,sizeof(ji))) {
        release_context(c);ReleaseSRWLockExclusive(&global_lock);return VLC_EGENERIC;
    }
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
    *top=mode==VIEW_PULSE?212:122;*bottom=HEIGHT-64;
}
static int button_at(int x,int y,int row,int count)
{
    x-=TOOL_X;
    return x>=0 && y>=row && y<row+26 && x/TOOL_STEP<count && x%TOOL_STEP<TOOL_BUTTON_W?x/TOOL_STEP:-1;
}
static int option_at(int x,int y)
{
    x-=TOOL_X;
    return x>=0 && y>=OPTIONS_Y && y<OPTIONS_Y+26 && x/OPT_STEP<OPT_COUNT && x%OPT_STEP<OPT_WIDTH?x/OPT_STEP:-1;
}
static int swatch_at(int x,int y)
{
    x-=SWATCH_X;
    return x>=0 && x<SWATCH_STEP*COLOR_COUNT && y>=OPTIONS_Y && y<OPTIONS_Y+26?x/SWATCH_STEP:-1;
}
static bool seek_area(context_t *c,int x,int y)
{
    int top,bottom;view_bounds(c->view_mode,&top,&bottom);
    return x>=LEFT && x<=RIGHT && y>=top-(c->show_claps?11:4) && y<=bottom+3;
}
/* Shared by regular VLC button events and Windows/Qt double-click messages. */
static void plugin_click(visual_t *v,int x,int y)
{
    context_t *c=v->ctx;
    if(!c->enabled)return;
    int button=button_at(x,y,27,TOOL_COUNT);
    if(button>=0) {
        if(button==TOOL_OPTIONS) { v->options=!v->options;v->palette_open=false; }
        else if(button==TOOL_COLOR) { v->palette_open=!v->palette_open;v->options=false; }
        else {
            if(button==TOOL_OFF) { c->enabled=false;c->pending_disable=true;v->palette_open=false; }
            else if(button==TOOL_CLAP)c->show_claps=!c->show_claps;
            else c->view_mode=button;
            c->settings_dirty=true;
        }
        return;
    }
    if(v->palette_open) {
        int color=swatch_at(x,y);
        v->palette_open=false;
        if(color>=0) { c->colors[c->view_mode]=(unsigned)color;c->settings_dirty=true;return; }
    }
    button=v->options?option_at(x,y):-1;
    if(button>=0) {
        unsigned fps=effective_fps(c->fps_index);
        if(button<=OPT_FF) {
            unsigned fields=c->time_format^(1u<<button);
            if(!fields)return;
            c->time_format=fields;
        }
        else if(button==OPT_FPS_DOWN)c->fps_index=fps<=1?FPS_COUNT:fps-1;
        else if(button==OPT_FPS_UP)c->fps_index=fps%FPS_COUNT+1;
        else if(button==OPT_LANGUAGE)c->language=c->language==LANG_DE?LANG_EN:LANG_DE;
        else return;
        c->settings_dirty=true;
    } else if(seek_area(c,x,y) && c->input && c->seekable && c->length>0) {
        c->seek=(float)mouse_position(c,x);c->seek_generation=c->generation;c->pending_seek=true;
    }
}
static int MouseButton(vlc_object_t *obj, const char *name, vlc_value_t old, vlc_value_t val, void *data)
{
    (void)name;
    if(!(val.i_int&1) || (old.i_int&1))return VLC_SUCCESS;
    visual_t *v=data;context_t *c=v->ctx;
    int x,y;var_GetCoords(obj,"mouse-moved",&x,&y);
    EnterCriticalSection(&c->lock);
    plugin_click(v,x,y);
    LeaveCriticalSection(&c->lock);return VLC_SUCCESS;
}
static void filter_mouse(visual_t *v,vlc_mouse_t *out,const vlc_mouse_t *old,const vlc_mouse_t *in)
{
    *out=*in;
    if(!in->b_double_click)return;
    context_t *c=v->ctx;
    EnterCriticalSection(&c->lock);
    int x=in->i_x,y=in->i_y;
    /* Reserve the swatch row even just after a pick closes it. */
    if(button_at(x,y,27,TOOL_COUNT)>=0 || swatch_at(x,y)>=0 ||
       (v->options && option_at(x,y)>=0) || seek_area(c,x,y)) {
        out->b_double_click=false;
        /* Qt may deliver the second click only as a double-click, without a
           left-button transition. Emulated double-clicks already carry one. */
        if(!vlc_mouse_IsLeftPressed(in) && !old->b_double_click)plugin_click(v,x,y);
    }
    LeaveCriticalSection(&c->lock);
}
static int MouseFilter(filter_t *f,vlc_mouse_t *out,const vlc_mouse_t *old,const vlc_mouse_t *in)
{
    filter_mouse((void *)f->p_sys,out,old,in);return VLC_SUCCESS;
}
static picture_t *MousePicture(filter_t *f,picture_t *pic) { (void)f;return pic; }
static int OpenMouseFilter(vlc_object_t *obj)
{
    filter_t *f=(filter_t *)obj;
    visual_t *v=var_GetAddress(obj->obj.parent,MOUSE_OWNER);
    if(!v || VLC_OBJECT(v->vout)!=obj->obj.parent)return VLC_EGENERIC;
    f->p_sys=(void *)v;f->pf_video_filter=MousePicture;f->pf_video_mouse=MouseFilter;
    return VLC_SUCCESS;
}
static bool attach_mouse_filter(visual_t *v)
{
    char *prior=var_GetString(v->vout,"video-filter");
    size_t len=prior?strlen(prior):0;
    char *chain=malloc(len+1+sizeof(MOUSE_FILTER));
    if(!chain){free(prior);return false;}
    if(len)memcpy(chain,prior,len);
    if(len && chain[len-1]!=':')chain[len++]=':';
    memcpy(chain+len,MOUSE_FILTER,sizeof(MOUSE_FILTER));free(prior);
    bool ok=var_Create(v->vout,MOUSE_OWNER,VLC_VAR_ADDRESS)==VLC_SUCCESS &&
        var_SetAddress(v->vout,MOUSE_OWNER,v)==VLC_SUCCESS &&
        var_SetString(v->vout,"video-filter",chain)==VLC_SUCCESS;
    free(chain);return ok;
}
static void line(HDC dc,int x,int y,int x2,int y2,COLORREF color,int width)
{
    HPEN pen=CreatePen(PS_SOLID,width,color),old=SelectObject(dc,pen);
    MoveToEx(dc,x,y,NULL);LineTo(dc,x2,y2);SelectObject(dc,old);DeleteObject(pen);
}
static void text(HDC dc,int x,int y,const wchar_t *str,COLORREF color)
{ SetTextColor(dc,color);TextOutW(dc,x,y,str,(int)wcslen(str)); }
static void stamp(int64_t us,unsigned fields,unsigned fps,wchar_t out[TIME_FIELD_CELLS+1])
{
    us=CLAMP(us,0,(int64_t)MAX_BUCKETS*10000);
    unsigned ms=(unsigned)(us/1000),hours=ms/3600000,minutes=ms/60000,seconds=(ms/1000)%60;
    for(int i=0;i<TIME_FIELD_CELLS;i++)out[i]=L' ';
    out[TIME_FIELD_CELLS]=0;
    wchar_t digits[9];
    if(fields&TIME_HH){_snwprintf(digits,9,L"%02u",hours);memcpy(out,digits,2*sizeof(*out));}
    if(fields&TIME_MM) {
        if(fields&TIME_HH)minutes%=60;
        _snwprintf(digits,9,L"%02u",minutes);
        size_t width=wcslen(digits); /* at most four digits at the 24-hour limit */
        memcpy(out+5-width,digits,width*sizeof(*out));
    }
    if(fields&TIME_SS){_snwprintf(digits,9,L"%02u",seconds);memcpy(out+6,digits,2*sizeof(*out));}
    if((fields&(TIME_HH|TIME_MM))==(TIME_HH|TIME_MM))out[2]=L':';
    if((fields&(TIME_MM|TIME_SS))==(TIME_MM|TIME_SS))out[5]=L':';
    if(fields&TIME_FF) {
        int64_t rate=frame_rates[effective_fps(fps)-1];
        unsigned frame=(unsigned)((us%1000000)*rate/1000000000);
        int width=rate>100000?3:2;
        _snwprintf(digits,9,L"%0*u",width,frame);
        out[8]=L':';memcpy(out+9,digits,width*sizeof(*out));
    }
    if(fields&TIME_MS) {
        _snwprintf(digits,9,L"%03u",ms%1000);
        out[12]=L'.';memcpy(out+13,digits,3*sizeof(*out));
    }
}
/* Every combination uses the same cells: HH:MM:SS:fff.mmm. */
static void draw_time_field(HDC dc,int left,const wchar_t *str,COLORREF color)
{
    int n=(int)wcslen(str),advance[TIME_FIELD_CELLS];
    if(n>TIME_FIELD_CELLS)return;
    for(int i=0;i<n;i++)advance[i]=TIME_CELL;
    SetTextColor(dc,color);
    ExtTextOutW(dc,left,HEIGHT-36,0,NULL,str,n,advance);
}
static void draw_times(HDC dc,int64_t at,int64_t length,unsigned format,unsigned fps,bool paused)
{
    wchar_t a[TIME_FIELD_CELLS+1],b[TIME_FIELD_CELLS+1];stamp(at,format,fps,a);stamp(length,format,fps,b);
    draw_time_field(dc,TIME_CURRENT_X,a,RGB(242,244,249));
    draw_time_field(dc,TIME_TOTAL_X,b,RGB(185,195,207));
    if(paused) {
        int x=TIME_PAUSE_X;
        line(dc,x,HEIGHT-30,x,HEIGHT-16,RGB(145,160,178),2);
        line(dc,x+5,HEIGHT-30,x+5,HEIGHT-16,RGB(145,160,178),2);
    }
}
static void draw_button_sized(HDC dc,int x,int y,int width,const wchar_t *label,bool selected)
{
    HBRUSH brush=CreateSolidBrush(selected?RGB(33,55,63):RGB(21,24,29));
    HPEN pen=CreatePen(PS_SOLID,selected?2:1,selected?RGB(160,221,226):RGB(81,89,103));
    HGDIOBJ ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
    RoundRect(dc,x,y,x+width,y+26,8,8);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
    SIZE extent={0};GetTextExtentPoint32W(dc,label,(int)wcslen(label),&extent);
    text(dc,x+(width-extent.cx)/2,y+(26-extent.cy)/2,label,RGB(236,240,246));
}
static void draw_option(HDC dc,int slot,const wchar_t *label,bool selected)
{
    draw_button_sized(dc,TOOL_X+slot*OPT_STEP,OPTIONS_Y,OPT_WIDTH,label,selected);
}
static void draw_swatch(HDC dc,int x,int y,COLORREF color,bool selected)
{
    RECT r={x,y,x+22,y+22};
    FillRect(dc,&r,GetStockObject(selected?WHITE_BRUSH:DKGRAY_BRUSH));
    InflateRect(&r,-3,-3);
    HBRUSH brush=CreateSolidBrush(color);
    if(brush){FillRect(dc,&r,brush);DeleteObject(brush);}
}
static void draw_toolbar(HDC dc,int mode,bool enabled,bool options,bool show_palette,bool show_claps,
                         unsigned color,unsigned format,unsigned fps,unsigned language)
{
    for(int i=0;i<TOOL_COUNT;i++)
        draw_button_sized(dc,TOOL_X+i*TOOL_STEP,27,TOOL_BUTTON_W,view_labels[language][i],
                    i<VIEW_COUNT?enabled&&i==mode:i==TOOL_COLOR?show_palette:
                    i==TOOL_CLAP?enabled&&show_claps:i==TOOL_OPTIONS?options:!enabled);
    draw_swatch(dc,SWATCH_X+TOOL_BUTTON_W-26,29,palette[color],false);
    if(show_palette)for(unsigned i=0;i<COLOR_COUNT;i++)
        draw_swatch(dc,SWATCH_X+i*SWATCH_STEP+2,OPTIONS_Y+2,palette[i],i==color);
    if(options) {
        static const wchar_t *const labels[]={L"HH",L"MM",L"SS",L"mmm",L"FF"};
        for(unsigned i=0;i<=OPT_FF;i++)draw_option(dc,i,labels[i],(format&(1u<<i))!=0);
        draw_option(dc,OPT_FPS_DOWN,L"\u2212 FPS",false);
        wchar_t label[50];
        unsigned rate=frame_rates[effective_fps(fps)-1];
        if(rate%1000)_snwprintf(label,50,L"+ %u%c%03u fps",rate/1000,language==LANG_DE?L',':L'.',rate%1000);
        else _snwprintf(label,50,L"+ %u fps",rate/1000);
        draw_option(dc,OPT_FPS_UP,label,false);
        draw_option(dc,OPT_LANGUAGE,language==LANG_DE?L"Sprache: DE":L"Language: EN",false);
    }
}
static COLORREF shade(COLORREF color,int percent)
{
    return RGB(GetRValue(color)*percent/100,GetGValue(color)*percent/100,GetBValue(color)*percent/100);
}
static bool has_clap(const context_t *c,unsigned start,unsigned end)
{
    unsigned lo=0,hi=c->clap_count;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        if(c->claps[mid]<start)lo=mid+1;else hi=mid;
    }
    return lo<c->clap_count && c->claps[lo]<end;
}
static void draw_grid(HDC dc,const visual_t *v,int top,int bottom,COLORREF color)
{
    enum { LEVELS=24, CELL=6 };
    int base=(top+bottom+LEVELS*CELL)/2;
    line(dc,LEFT,base+1,RIGHT,base+1,shade(color,35),1);
    HBRUSH colors[LEVELS]={0};
    for(int i=0;i<LEVELS;i++)
        colors[i]=CreateSolidBrush(shade(color,55+i*45/(LEVELS-1)));
    for(unsigned col=0;col<RIGHT-LEFT;col+=RASTER_STEP) {
        int a=amplitude(v->columns[col]);
        if(!a)continue;
        int extent=(a*LEVELS*CELL+v->drawn_max-1)/v->drawn_max;
        for(int level=0;level<LEVELS && level*CELL<extent;level++) {
            int y=base-level*CELL;
            int h=CLAMP(extent-level*CELL,1,CELL-1);
            RECT cell={LEFT+(int)col,y-h,CLAMP(LEFT+(int)(col+RASTER_STEP)-1,LEFT,RIGHT),y};
            if(colors[level])FillRect(dc,&cell,colors[level]);
        }
    }
    for(int i=0;i<LEVELS;i++)if(colors[i])DeleteObject(colors[i]);
}
static void draw_wave(HDC dc,const visual_t *v,int mode,int top,int bottom,COLORREF color)
{
    enum { N=RIGHT-LEFT };
    int mid=(top+bottom)/2;
    double gain=(bottom-top)*.46/v->drawn_max;
    if(mode!=VIEW_GRID) {
        bool pulse=mode==VIEW_PULSE;
        int baseline=pulse?bottom-4:mid;
        line(dc,LEFT,baseline,RIGHT,baseline,RGB(52,62,73),1);
        HPEN pen=CreatePen(PS_SOLID,pulse?2:1,color);
        HGDIOBJ old=SelectObject(dc,pen);
        for(int i=0;i<N;i+=pulse?4:1) {
            int h=pulse?(int)(amplitude(v->columns[i])*gain*1.89):(int)(v->columns[i].hi*gain);
            MoveToEx(dc,LEFT+i,baseline,NULL);LineTo(dc,LEFT+i,baseline-h);
        }
        SelectObject(dc,old);DeleteObject(pen);
        if(!pulse) {
            pen=CreatePen(PS_SOLID,1,shade(color,65));old=SelectObject(dc,pen);
            for(int i=0;i<N;i++){MoveToEx(dc,LEFT+i,baseline,NULL);LineTo(dc,LEFT+i,baseline-(int)(v->columns[i].lo*gain));}
            SelectObject(dc,old);DeleteObject(pen);
        }
    } else draw_grid(dc,v,top,bottom,color);
}
static void draw_claps(HDC dc,const visual_t *v,int top)
{
    HBRUSH dot=CreateSolidBrush(RGB(255,192,84));
    if(!dot)return;
    for(int i=0;i<RIGHT-LEFT;i++)if(v->clap_columns[i]) {
        int x=LEFT+i,y=top-11;
        RECT flag={x-1,y,x+2,y+8};
        FillRect(dc,&flag,dot);
    }
    DeleteObject(dot);
}
static bool draw(visual_t *v)
{
    context_t *c=v->ctx;
    int64_t length,at; int state,maxabs,x,mode; bool analyzing,rebuild,enabled,options,show_palette,show_claps;
    unsigned color,format,fps,language,error;
    ULONGLONG now=GetTickCount64();
    /* Snapshot only. GDI drawing and picture delivery must never hold the
       controller/analyzer lock. Reaggregate growing peaks at most twice/sec. */
    EnterCriticalSection(&c->lock);
    length=CLAMP(c->length,0,((int64_t)MAX_BUCKETS)*10000);at=c->time;state=c->state;analyzing=c->analyzing;
    mode=c->view_mode;enabled=c->enabled;color=c->colors[mode];show_claps=c->show_claps;show_palette=v->palette_open;
    options=v->options;format=c->time_format;fps=c->fps_index;language=c->language;error=c->error;
    maxabs=c->max_amplitude>0?c->max_amplitude:1;
    x=LEFT+(int)((isfinite(c->position)?CLAMP(c->position,0,1):0)*(RIGHT-LEFT));
    rebuild=!v->frames || v->drawn_mode!=mode || v->drawn_enabled!=enabled || v->drawn_generation!=c->generation || v->drawn_length!=length ||
        v->drawn_color!=color || v->drawn_palette!=show_palette || v->drawn_show_claps!=show_claps ||
        (show_claps && v->drawn_claps_ready!=c->claps_ready) ||
        v->drawn_analyzing!=analyzing || v->drawn_options!=options || v->drawn_format!=format || v->drawn_fps!=fps || v->drawn_language!=language || v->drawn_error!=error ||
        (v->drawn_count!=c->count && now-v->rebuilt_at>=500);
    if(rebuild) {
        uint64_t total=length?(uint64_t)length/10000:c->count;
        for(unsigned col=0;col<RIGHT-LEFT;col++) {
            if(mode!=VIEW_GRID || col%RASTER_STEP==0) {
                unsigned i=(unsigned)(col*total/(RIGHT-LEFT));
                unsigned end=(unsigned)((col+1)*total/(RIGHT-LEFT))+1;
                if(mode==VIEW_GRID) {
                    unsigned next=CLAMP(col+RASTER_STEP,0,RIGHT-LEFT);
                    end=(unsigned)((next*total+(RIGHT-LEFT)-1)/(RIGHT-LEFT));
                    i=(unsigned)((col*total+(RIGHT-LEFT)-1)/(RIGHT-LEFT));
                }
                if(end>c->count)end=c->count;
                v->columns[col]=peak_range(c,i,end);
            }
            if(show_claps && c->claps_ready) {
                unsigned a=(unsigned)((col*total+(RIGHT-LEFT)-1)/(RIGHT-LEFT));
                unsigned b=(unsigned)(((col+1)*total+(RIGHT-LEFT)-1)/(RIGHT-LEFT));
                v->clap_columns[col]=has_clap(c,a,b);
            } else v->clap_columns[col]=false;
        }
        v->drawn_claps_ready=c->claps_ready;
        v->drawn_count=c->count;v->drawn_generation=c->generation;v->drawn_length=length;
        v->drawn_analyzing=analyzing;v->drawn_max=maxabs;v->drawn_error=error;
        v->drawn_mode=mode;v->drawn_enabled=enabled;
        v->drawn_color=color;v->drawn_palette=show_palette;v->drawn_show_claps=show_claps;
        v->drawn_options=options;v->drawn_format=format;v->drawn_fps=fps;v->drawn_language=language;
    }
    LeaveCriticalSection(&c->lock);
    if(!rebuild && v->drawn_time==at/250000 && v->drawn_state==state &&
       (state==PLAYING_S || v->drawn_x==x)) return false;
    int top,bottom;view_bounds(mode,&top,&bottom);
    if(rebuild) {
        HDC dc=v->base_dc;
        PatBlt(dc,0,0,WIDTH,HEIGHT,BLACKNESS);
        SetBkMode(dc,TRANSPARENT);
        SelectObject(dc,v->small_font);
        draw_toolbar(dc,mode,enabled,options,show_palette,show_claps,color,format,fps,language);
        if(enabled) {
            draw_wave(dc,v,mode,top,bottom,palette[color]);
            if(show_claps && v->drawn_claps_ready)draw_claps(dc,v,top);
        }
        static const wchar_t *const status[2][4]={
            {L"Analyse \u2026",L"Analyse fehlgeschlagen",L"Keine Datei-Wellenform",L"Analyse nicht gestartet"},
            {L"Analyzing \u2026",L"Analysis failed",L"No file waveform",L"Analysis could not start"}
        };
        if(error||analyzing)text(dc,LEFT,HEIGHT-28,status[language][error],error?RGB(246,126,113):RGB(145,160,178));
        GdiFlush();v->rebuilt_at=now;v->rebuilds++;
    }
    BitBlt(v->dc,0,0,WIDTH,HEIGHT,v->base_dc,0,0,SRCCOPY);
    SetBkMode(v->dc,TRANSPARENT);SelectObject(v->dc,v->font);
    SetTextAlign(v->dc,TA_LEFT|TA_TOP);
    draw_times(v->dc,at,length,format,fps,state==PAUSE_S);
    if(enabled && x>=LEFT && x<=RIGHT)line(v->dc,x,top-4,x,bottom+3,RGB(246,249,250),2);
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
        picture_t *picture=region->p_picture;
        if(!picture || picture->i_planes<1 || !picture->p[0].p_pixels ||
           picture->p[0].i_pitch<WIDTH*4 || picture->p[0].i_lines<HEIGHT) {
            subpicture_region_Delete(region);break;
        }
        for(int y=0;y<HEIGHT;y++) {
            uint8_t *dest=picture->p[0].p_pixels+y*picture->p[0].i_pitch;
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
    intf_thread_t *intf=NULL;
    if(c && c->intf->obj.libvlc==obj->obj.libvlc) {
        InterlockedIncrement(&c->references);intf=vlc_object_hold(c->intf);
    }else c=NULL;
    ReleaseSRWLockShared(&global_lock);
    if(!c) { msg_Err(obj,"Enable extraintf=waveformintf first");return VLC_EGENERIC; }
    EnterCriticalSection(&c->lock);bool enabled=c->enabled;LeaveCriticalSection(&c->lock);
    if(!enabled) { vlc_object_release(intf);release_context(c);return VLC_EGENERIC; }
    input_thread_t *input=playlist_CurrentInput(pl_Get(intf));
    vlc_object_release(intf);
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
    if(!attach_mouse_filter(v))msg_Warn(obj,"Waveform mouse filter could not be attached");
    /* One seed on the newly requested visualization vout. Released waveform
       vouts are not recycled by aout_filter_RequestVout. Never acquire again. */
    picture_t *seed=vout_GetPicture(v->vout);
    if(!seed) goto fail;
    if(seed->i_planes<1 || !seed->p[0].p_pixels || seed->p[0].i_pitch<WIDTH*4 || seed->p[0].i_lines<HEIGHT) {
        picture_Release(seed);goto fail;
    }
    for(int y=0;y<HEIGHT;y++)memset(seed->p[0].p_pixels+y*seed->p[0].i_pitch,0,WIDTH*4);
    seed->b_progressive=true;seed->b_force=true;seed->date=mdate();
    vout_PutPicture(v->vout,seed);
    v->spu_channel=vout_RegisterSubpictureChannel(v->vout);
    if(v->spu_channel==VOUT_SPU_CHANNEL_INVALID)goto fail;
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
    v->font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Bahnschrift");
    v->small_font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    if(!v->font||!v->small_font)goto fail;
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
