/* SWRC v9 geometry viewer for PS Vita. No original game engine or gameplay.
 * Data: ux0:data/SWRV00001/scene.swv (generated from the user's own PC files).
 * Padded, 64-byte v9 header/chunk structures. Safe enough for diagnostics.
 */
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#define VIEW 35000.0f
#define DEG 0.0000958737992429f
typedef struct __attribute__((packed)) {
 char magic[4]; uint32_t version,header_size,vertex_stride,chunk_stride;
 uint32_t chunk_count,chunk_table_offset,reserved;
 float bounds[6]; uint32_t file_size,crc32;
} Header;
typedef struct __attribute__((packed)) {
 float bounds[6]; uint32_t vertex_count,index_count,vertex_offset,index_offset;
 uint32_t actor_count,bsp_triangles,mesh_triangles;
 int32_t cell_x,cell_y; uint32_t reserved;
} Chunk;
typedef struct __attribute__((packed)) {
 float xyz[3],n[3],uv[2]; uint8_t rgba[4];
} Vertex;
typedef struct {unsigned char *raw; size_t size; Header *h; Chunk *chunks; SceUID memblock;} Scene;
_Static_assert(sizeof(Header)==64,"header size");
_Static_assert(sizeof(Chunk)==64,"chunk size");
_Static_assert(sizeof(Vertex)==36,"vertex size");

static void logmsg(const char *msg) {
 FILE *f=fopen("ux0:data/SWRV00001-viewer.log","a");
 if(f){fprintf(f,"%s\n",msg);fclose(f);}
}
static uint32_t crc32(const unsigned char *p,size_t n) {
 uint32_t c=0xffffffffu;
 for(size_t i=0;i<n;i++){c^=p[i];for(int j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}
 return ~c;
}
static char scene_error[180];
static char scene_trace[640];
static void release(Scene *s) {
 if(s->memblock>=0) sceKernelFreeMemBlock(s->memblock);
 else free(s->raw);
 memset(s,0,sizeof(*s));s->memblock=-1;
}
static void trace_line(const char *s) {
 size_t n=strlen(scene_trace);
 if(n+strlen(s)+3<sizeof(scene_trace)) snprintf(scene_trace+n,sizeof(scene_trace)-n,"%s\n",s);
 logmsg(s);
}
static int load(Scene *s,const char *path) {
 memset(s,0,sizeof(*s));s->memblock=-1;
 SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
 if(fd<0){
   snprintf(scene_error,sizeof(scene_error),"OPEN %s ERROR %08X",path,(unsigned)fd);
   trace_line(scene_error);return 0;
 }
 SceOff len=sceIoLseek(fd,0,SCE_SEEK_END);
 if(len<64||len>96*1024*1024||sceIoLseek(fd,0,SCE_SEEK_SET)<0){
   snprintf(scene_error,sizeof(scene_error),"BAD SIZE %s %lld",path,(long long)len);
   sceIoClose(fd);trace_line(scene_error);return 0;
 }
 s->size=(size_t)len;
 s->raw=malloc(s->size);
 if(!s->raw){
   uint32_t aligned=(uint32_t)((s->size+4095)&~4095u);
   SceUID block=sceKernelAllocMemBlock("swrc_geometry",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,aligned,NULL);
   if(block>=0){
     void *base=NULL;
     if(sceKernelGetMemBlockBase(block,&base)==0&&base){
       s->memblock=block;s->raw=(unsigned char*)base;
     }else sceKernelFreeMemBlock(block);
   }
 }
 if(!s->raw){
   snprintf(scene_error,sizeof(scene_error),"OUT OF MEMORY %lu BYTES",(unsigned long)s->size);
   sceIoClose(fd);trace_line(scene_error);return 0;
 }
 size_t got=0;
 while(got<s->size){
   size_t want=s->size-got;
   if(want>65536)want=65536;
   int n=sceIoRead(fd,s->raw+got,(SceSize)want);
   if(n<=0)break;
   got+=(size_t)n;
 }
 sceIoClose(fd);
 if(got!=s->size){
   snprintf(scene_error,sizeof(scene_error),"READ ERROR %lu OF %lu",(unsigned long)got,(unsigned long)s->size);
   goto bad;
 }
 s->h=(Header*)s->raw;Header *h=s->h;
 if(memcmp(h->magic,"SWV9",4)||h->version!=9||h->header_size!=64||
    h->vertex_stride!=36||h->chunk_stride!=64||h->reserved!=0||
    h->chunk_table_offset!=64||h->chunk_count<1||h->chunk_count>8192||
    h->file_size!=s->size||64ull+h->chunk_count*64ull>s->size){
   snprintf(scene_error,sizeof(scene_error),"HEADER INVALID %s",path);goto bad;
 }
 uint32_t crc=crc32(s->raw+64,s->size-64);
 if(crc!=h->crc32){
   snprintf(scene_error,sizeof(scene_error),"CRC INVALID %08X EXPECT %08X",crc,h->crc32);goto bad;
 }
 s->chunks=(Chunk*)(s->raw+64);
 for(uint32_t i=0;i<h->chunk_count;i++){
  Chunk *c=&s->chunks[i];
  uint64_t endv=(uint64_t)c->vertex_offset+c->vertex_count*36ull;
  uint64_t endi=(uint64_t)c->index_offset+c->index_count*2ull;
  if(c->vertex_count<3||c->vertex_count>40000||c->index_count<3||
    c->index_count%3||c->index_count/3!=c->bsp_triangles+c->mesh_triangles||
    c->vertex_offset<64+h->chunk_count*64||endv>c->index_offset||
    endi>s->size||c->vertex_offset%4||c->index_offset%2){
    snprintf(scene_error,sizeof(scene_error),"CHUNK %u INVALID",i);goto bad;
  }
  for(int a=0;a<3;a++)if(!isfinite(c->bounds[a])||!isfinite(c->bounds[a+3])||
     c->bounds[a]>c->bounds[a+3]){
    snprintf(scene_error,sizeof(scene_error),"CHUNK %u INVALID BOUNDS",i);goto bad;
  }
 }
 snprintf(scene_error,sizeof(scene_error),"SCENE LOADED %u CHUNKS FROM %s",h->chunk_count,path);
 trace_line(scene_error);
 return 1;
bad:
 trace_line(scene_error);release(s);return 0;
}
static float axis(uint8_t b){
 float v=((float)b-128.0f)/127.0f;
 return fabsf(v)>0.13f?fmaxf(-1.0f,fminf(1.0f,v)):0.0f;
}
typedef struct {float x,y,z;} V3;
static V3 cross(V3 a,V3 b){return (V3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static V3 normalize(V3 a){float m=sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);if(m>1e-9f){a.x/=m;a.y/=m;a.z/=m;}return a;}
static void camera(float eye[3],float yaw,float pitch){
 V3 f=normalize((V3){cosf(pitch)*cosf(yaw),sinf(pitch),-cosf(pitch)*sinf(yaw)});
 V3 e={eye[0],eye[2],-eye[1]};
 V3 right=normalize(cross(f,(V3){0,1,0})),up=cross(right,f);
 float m[16]={right.x,up.x,-f.x,0,
              right.y,up.y,-f.y,0,
              right.z,up.z,-f.z,0,
              0,0,0,1};
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMultMatrixf(m);glTranslatef(-e.x,-e.y,-e.z);
}
static int visible(Chunk *c,float eye[3],float yaw,float pitch){
 float dx=(c->bounds[0]+c->bounds[3])*.5f-eye[0];
 float dy=(c->bounds[1]+c->bounds[4])*.5f-eye[1];
 float dz=(c->bounds[2]+c->bounds[5])*.5f-eye[2];
 float x=(c->bounds[3]-c->bounds[0])*.5f;
 float y=(c->bounds[4]-c->bounds[1])*.5f;
 float z=(c->bounds[5]-c->bounds[2])*.5f;
 float r=sqrtf(x*x+y*y+z*z);
 if(dx*dx+dy*dy+dz*dz>(VIEW+r)*(VIEW+r))return 0;
 return cosf(pitch)*(cosf(yaw)*dx+sinf(yaw)*dy)+sinf(pitch)*dz>=-r;
}
static void draw(Scene *s,float eye[3],float yaw,float pitch){
 camera(eye,yaw,pitch);
 float transform[16]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
 glMultMatrixf(transform);
 glEnableClientState(GL_VERTEX_ARRAY);
 glEnableClientState(GL_COLOR_ARRAY);
 for(uint32_t i=0;i<s->h->chunk_count;i++){
  Chunk *c=&s->chunks[i];
  if(!visible(c,eye,yaw,pitch))continue;
  Vertex *v=(Vertex*)(s->raw+c->vertex_offset);
  uint16_t *idx=(uint16_t*)(s->raw+c->index_offset);
  glVertexPointer(3,GL_FLOAT,sizeof(Vertex),v[0].xyz);
  glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),v[0].rgba);
  glDrawElements(GL_TRIANGLES,c->index_count,GL_UNSIGNED_SHORT,idx);
 }
 glDisableClientState(GL_COLOR_ARRAY);
 glDisableClientState(GL_VERTEX_ARRAY);
}
/* Fixed-width bitmap diagnostic drawn using the already-initialized vitaGL API.
 * No font file, no sprite atlas, and no external library required. */
static const uint8_t font5x7[36][7]={
 {14, 17, 19, 21, 25, 17, 14},
 {4, 12, 4, 4, 4, 4, 14},
 {14, 17, 1, 2, 4, 8, 31},
 {30, 1, 1, 14, 1, 1, 30},
 {2, 6, 10, 18, 31, 2, 2},
 {31, 16, 16, 30, 1, 1, 30},
 {14, 16, 16, 30, 17, 17, 14},
 {31, 1, 2, 4, 8, 8, 8},
 {14, 17, 17, 14, 17, 17, 14},
 {14, 17, 17, 15, 1, 1, 14},
 {14, 17, 17, 31, 17, 17, 17},
 {30, 17, 17, 30, 17, 17, 30},
 {14, 17, 16, 16, 16, 17, 14},
 {30, 17, 17, 17, 17, 17, 30},
 {31, 16, 16, 30, 16, 16, 31},
 {31, 16, 16, 30, 16, 16, 16},
 {14, 17, 16, 23, 17, 17, 15},
 {17, 17, 17, 31, 17, 17, 17},
 {31, 4, 4, 4, 4, 4, 31},
 {7, 2, 2, 2, 18, 18, 12},
 {17, 18, 20, 24, 20, 18, 17},
 {16, 16, 16, 16, 16, 16, 31},
 {17, 27, 21, 21, 17, 17, 17},
 {17, 25, 21, 19, 17, 17, 17},
 {14, 17, 17, 17, 17, 17, 14},
 {30, 17, 17, 30, 16, 16, 16},
 {14, 17, 17, 17, 21, 18, 13},
 {30, 17, 17, 30, 20, 18, 17},
 {15, 16, 16, 14, 1, 1, 30},
 {31, 4, 4, 4, 4, 4, 4},
 {17, 17, 17, 17, 17, 17, 14},
 {17, 17, 17, 17, 17, 10, 4},
 {17, 17, 17, 21, 21, 21, 10},
 {17, 17, 10, 4, 10, 17, 17},
 {17, 17, 10, 4, 4, 4, 4},
 {31, 1, 2, 4, 8, 16, 31}
};
static const uint8_t question_glyph[7]={14,17,2,4,4,0,4};
static const uint8_t *get_glyph(char ch){
 if(ch>='0'&&ch<='9')return font5x7[ch-'0'];
 if(ch>='a'&&ch<='z')ch=(char)(ch-'a'+'A');
 if(ch>='A'&&ch<='Z')return font5x7[10+ch-'A'];
 if(ch==' ')return NULL;
 return question_glyph;
}
static void show_error_text(const char *s,int line){
 static float tri[64*35*6*3];
 unsigned n=0;
 float unit_x=0.0061f,unit_y=0.012f;
 for(int ci=0;ci<47&&s[ci];ci++){
  const uint8_t *g=get_glyph(s[ci]);if(!g)continue;
  for(int y=0;y<7;y++)for(int x=0;x<5;x++){
   if(!(g[y]&(1u<<(4-x))))continue;
   float left=-0.93f+(ci*6+x)*unit_x,top=0.55f-(line*11+y)*unit_y;
   float right=left+unit_x,bottom=top-unit_y;
   if(n+18>sizeof(tri)/sizeof(tri[0]))break;
   tri[n++]=left;tri[n++]=top;tri[n++]=0;
   tri[n++]=right;tri[n++]=top;tri[n++]=0;
   tri[n++]=left;tri[n++]=bottom;tri[n++]=0;
   tri[n++]=right;tri[n++]=top;tri[n++]=0;
   tri[n++]=right;tri[n++]=bottom;tri[n++]=0;
   tri[n++]=left;tri[n++]=bottom;tri[n++]=0;
  }
 }
 glEnableClientState(GL_VERTEX_ARRAY);
 glDisableClientState(GL_COLOR_ARRAY);
 glColor3f(1.0f,1.0f,1.0f);
 glVertexPointer(3,GL_FLOAT,0,tri);
 glDrawArrays(GL_TRIANGLES,0,n/3);
 glDisableClientState(GL_VERTEX_ARRAY);
}
static void draw_load_error(void) {
 glDisable(GL_DEPTH_TEST);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 show_error_text("SWRC LEVEL NOT LOADED",0);
 show_error_text(scene_error,1);
 show_error_text("SEE UX0 DATA SWRV00001 VIEWER LOG",2);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
}
int main(void){
 FILE *clearlog=fopen("ux0:data/SWRV00001-viewer.log","w");
 if(clearlog){fputs("SWRC v12 native IO start\n",clearlog);fclose(clearlog);}
 scene_trace[0]=0;
 Scene scene;memset(&scene,0,sizeof(scene));scene.memblock=-1;
 const char *paths[]={
   "ux0:data/SWRV00001/scene.swv",
   "app0:scene.swv",
   "app0:/scene.swv",
   "ux0:app/SWRV00001/scene.swv",
   "app0:data/scene.swv",
   "app0:sce_sys/scene.swv"
 };
 int ok=0;
 for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0])&&!ok;i++){
   ok=load(&scene,paths[i]);
 }
 logmsg(ok?"SWV9 ready: rendering world geometry":"SWV9 unavailable: see SCENE LOAD FAILURE");
 (void)vglInit(0x800000);
 logmsg("VitaGL init call returned");
 vglWaitVblankStart(GL_TRUE);
 sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
 glViewport(0,0,960,544);
 glClearColor(0.045f,0.065f,0.105f,1);
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_CULL_FACE);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();
 gluPerspective(75.0,960.0/544.0,24.0,VIEW*2.0);
 const float spawn[3]={2101.9768f,1829.5629f,-1901.798f};
 float pos[3]={spawn[0],spawn[1],spawn[2]},yaw=-9815.0f*DEG,pitch=0;
 uint32_t prev=0;
 unsigned frame=0;
 for(;;){
  SceCtrlData pad;memset(&pad,0,sizeof(pad));pad.lx=pad.ly=pad.rx=pad.ry=128;
  if(sceCtrlPeekBufferPositive(0,&pad,1)<1){sceKernelDelayThread(16000);continue;}
  if(pad.buttons&SCE_CTRL_CIRCLE)break;
  if((pad.buttons&SCE_CTRL_START)&&!(prev&SCE_CTRL_START)){
    memcpy(pos,spawn,sizeof(pos));yaw=-9815.0f*DEG;pitch=0;
  }
  prev=pad.buttons;
  yaw+=axis(pad.rx)*.04f;
  pitch=fminf(1.5f,fmaxf(-1.5f,pitch-axis(pad.ry)*.027f));
  float forward=-axis(pad.ly),strafe=axis(pad.lx);
  float speed=(pad.buttons&SCE_CTRL_RTRIGGER)?170:45;
  pos[0]+=speed*(forward*cosf(yaw)-strafe*sinf(yaw));
  pos[1]+=speed*(forward*sinf(yaw)+strafe*cosf(yaw));
  if(pad.buttons&SCE_CTRL_UP)pos[2]+=speed;
  if(pad.buttons&SCE_CTRL_DOWN)pos[2]-=speed;
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  if(ok)draw(&scene,pos,yaw,pitch);else draw_load_error();
  vglSwapBuffers(GL_FALSE);
  if(++frame==1)logmsg("First frame submitted");
 }
 release(&scene);
 logmsg("Exited normally");
 return 0;
}