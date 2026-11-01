/* SWRC v9 geometry viewer for PS Vita. No original game engine or gameplay.
 * Data: ux0:data/SWRV00006/scene.swv (generated from the user's own PC files).
 * Padded, 64-byte v9 header/chunk structures. Safe enough for diagnostics.
 */
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>

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
 FILE *f=fopen("ux0:data/SWRV00006-viewer.log","a");
 if(f){fprintf(f,"%s\n",msg);fclose(f);}
}
static uint32_t crc32(const unsigned char *p,size_t n) {
 uint32_t c=0xffffffffu;
 for(size_t i=0;i<n;i++){c^=p[i];for(int j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}
 return ~c;
}
/* Installer diagnostic: actual visible files under the app0: mount. */
static void log_app_mount(void){
 SceUID d=sceIoDopen("app0:");
 if(d<0){
  char msg[100];snprintf(msg,sizeof(msg),"app0: DIRECTORY OPEN ERROR %08X",(unsigned)d);logmsg(msg);return;
 }
 logmsg("app0: directory listing begins");
 SceIoDirent ent;
 for(int i=0;i<32;i++){
   memset(&ent,0,sizeof(ent));
   int read=sceIoDread(d,&ent);
   if(read<=0)break;
   char msg[150];
   snprintf(msg,sizeof(msg),"app0: FILE %.80s bytes %llu",ent.d_name,(unsigned long long)ent.d_stat.st_size);
   logmsg(msg);
 }
 sceIoDclose(d);
 logmsg("app0: directory listing ends");
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

/* SWRC v16 standalone texture pack. All pixels derived from original user's UTX,
 * stored in a separate file and never committed to public GitHub.
 * Format: 'SWTX' + version,count,filelen,crc32; then count times:
 * uint16 width,height; uint32 rgba_bytes; RGBA8 pixel bytes.
 */
#define MAX_SWRC_TEXTURES 64
static GLuint swrc_textures[MAX_SWRC_TEXTURES+1];
static unsigned swrc_texture_count=0;
static unsigned char *load_tex_bytes(const char *path,size_t *size){
 SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
 if(fd<0){char m[180];snprintf(m,sizeof(m),"TEXTURE OPEN %s %08X",path,(unsigned)fd);logmsg(m);return NULL;}
 SceOff end=sceIoLseek(fd,0,SCE_SEEK_END);
 if(end<20||end>32*1024*1024||sceIoLseek(fd,0,SCE_SEEK_SET)<0){
  logmsg("TEXTURE INVALID SIZE");sceIoClose(fd);return NULL;
 }
 unsigned char *buffer=(unsigned char*)malloc((size_t)end);
 if(!buffer){logmsg("TEXTURE OUT OF RAM");sceIoClose(fd);return NULL;}
 size_t off=0;
 while(off<(size_t)end){
  int n=sceIoRead(fd,buffer+off,(SceSize)(((size_t)end-off)>65536?65536:(size_t)end-off));
  if(n<=0)break;
  off+=(size_t)n;
 }
 sceIoClose(fd);
 if(off!=(size_t)end){logmsg("TEXTURE IO SHORT READ");free(buffer);return NULL;}
 *size=(size_t)end;return buffer;
}
static uint16_t swrc_u16(const unsigned char *p){return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t swrc_u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int load_textures(const char *path){
 size_t size=0;unsigned char *buf=load_tex_bytes(path,&size);
 if(!buf)return 0;
 if(memcmp(buf,"SWTX",4)||swrc_u32(buf+4)!=1||swrc_u32(buf+12)!=size||crc32(buf+20,size-20)!=swrc_u32(buf+16)){
  logmsg("TEXTURE PACK HEADER OR CRC INVALID");free(buf);return 0;
 }
 uint32_t n=swrc_u32(buf+8);
 if(n<1||n>MAX_SWRC_TEXTURES){logmsg("TEXTURE COUNT INVALID");free(buf);return 0;}
 size_t pos=20;
 for(uint32_t i=0;i<n;i++){
  if(pos+8>size){logmsg("TEXTURE HEADER TRUNCATED");free(buf);return 0;}
  uint32_t w=swrc_u16(buf+pos),h=swrc_u16(buf+pos+2),bytes=swrc_u32(buf+pos+4);
  pos+=8;
  if(w<4||h<4||w>1024||h>1024||bytes!=w*h*4||bytes>size-pos){
   logmsg("TEXTURE ENTRY INVALID");free(buf);return 0;
  }
  pos+=bytes;
 }
 if(pos!=size){logmsg("TEXTURE TRAILING DATA");free(buf);return 0;}
 memset(swrc_textures,0,sizeof(swrc_textures));
 glGenTextures(n,swrc_textures+1);
 pos=20;
 for(uint32_t i=0;i<n;i++){
  uint32_t w=swrc_u16(buf+pos),h=swrc_u16(buf+pos+2),bytes=swrc_u32(buf+pos+4);
  pos+=8;
  glBindTexture(GL_TEXTURE_2D,swrc_textures[i+1]);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,buf+pos);
  pos+=bytes;
 }
 glBindTexture(GL_TEXTURE_2D,0);
 swrc_texture_count=n;
 char msg[180];snprintf(msg,sizeof(msg),"SWRC TEXTURES LOADED %u FROM %s",n,path);logmsg(msg);
 free(buf);return 1;
}

/* v19 scene excludes an unsupported procedural DemonRays/SunBeam effect.
 * The sky is an original-texture approximation, not the original UE2 sky engine.
 * L toggles background to compare screenshot geometry against sky.
 */
#define SWRC_SKY_SEGMENTS 32
#define SWRC_SKY_PANEL 24
#define SWRC_SKY_TOP 25
/* Sky modes: 0=screen-space approximation; 1=previous cylinder; 2=off.
 * This comparison does not reconstruct the original Unreal sky renderer. */
static int swrc_sky_mode=0;
static int swrc_sky_enabled=1;

static void render_sky_legacy(float yaw,float pitch){
 if(!swrc_sky_enabled||swrc_texture_count<SWRC_SKY_TOP||
    !swrc_textures[SWRC_SKY_PANEL]||!swrc_textures[SWRC_SKY_TOP])return;
 float wallxyz[(SWRC_SKY_SEGMENTS+1)*6];
 float walluv[(SWRC_SKY_SEGMENTS+1)*4];
 float capxyz[(SWRC_SKY_SEGMENTS+2)*3];
 float capuv[(SWRC_SKY_SEGMENTS+2)*2];
 const float radius=29000.0f;
 const float top=22000.0f,bottom=-18000.0f;
 for(int j=0;j<=SWRC_SKY_SEGMENTS;j++){
  float a=6.283185307179586f*j/SWRC_SKY_SEGMENTS;
  float x=radius*cosf(a),y=radius*sinf(a);
  wallxyz[6*j+0]=x;wallxyz[6*j+1]=y;wallxyz[6*j+2]=top;
  wallxyz[6*j+3]=x;wallxyz[6*j+4]=y;wallxyz[6*j+5]=bottom;
  walluv[4*j+0]=(float)j/SWRC_SKY_SEGMENTS;walluv[4*j+1]=0;
  walluv[4*j+2]=(float)j/SWRC_SKY_SEGMENTS;walluv[4*j+3]=1;
  capxyz[3*(j+1)+0]=x;
  capxyz[3*(j+1)+1]=y;
  capxyz[3*(j+1)+2]=top;
  capuv[2*(j+1)]=0.5f+0.5f*cosf(a);
  capuv[2*(j+1)+1]=0.5f+0.5f*sinf(a);
 }
 capxyz[0]=0;capxyz[1]=0;capxyz[2]=top;
 capuv[0]=0.5f;capuv[1]=0.5f;
 glBindBuffer(GL_ARRAY_BUFFER,0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 glDisable(GL_DEPTH_TEST);
 glDepthMask(GL_FALSE);
 glDisableClientState(GL_COLOR_ARRAY);
 glColor4f(1.0f,1.0f,1.0f,1.0f);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();
 float origin[3]={0,0,0};
 camera(origin,yaw,pitch);
 const float world_to_eye[16]={1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
 glMultMatrixf(world_to_eye);
 glEnable(GL_TEXTURE_2D);
 glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 glEnableClientState(GL_VERTEX_ARRAY);
 glEnableClientState(GL_TEXTURE_COORD_ARRAY);
 glBindTexture(GL_TEXTURE_2D,swrc_textures[SWRC_SKY_PANEL]);
 glVertexPointer(3,GL_FLOAT,0,wallxyz);
 glTexCoordPointer(2,GL_FLOAT,0,walluv);
 glDrawArrays(GL_TRIANGLE_STRIP,0,(SWRC_SKY_SEGMENTS+1)*2);
 glBindTexture(GL_TEXTURE_2D,swrc_textures[SWRC_SKY_TOP]);
 glVertexPointer(3,GL_FLOAT,0,capxyz);
 glTexCoordPointer(2,GL_FLOAT,0,capuv);
 glDrawArrays(GL_TRIANGLE_FAN,0,SWRC_SKY_SEGMENTS+2);
 glDisableClientState(GL_TEXTURE_COORD_ARRAY);
 glDisableClientState(GL_VERTEX_ARRAY);
 glDisable(GL_TEXTURE_2D);
 glPopMatrix();
 glDepthMask(GL_TRUE);
 glEnable(GL_DEPTH_TEST);
}

/* v21: 2D panoramic backdrop. Using a screen-space quad avoids the distant
 * cylinder's extreme perspective interpolation and lets us compare sky quality
 * independently of world geometry. Still an approximation, not a UE2 skybox.
 */
static void render_sky_screen(float yaw,float pitch){
 if(swrc_texture_count<SWRC_SKY_PANEL||!swrc_textures[SWRC_SKY_PANEL])return;
 const float PI=3.14159265358979323846f;
 const float ucenter=-yaw/(2*PI);
 const float uhalf=0.112f;   /* 80 degrees across the 960x544 viewport */
 float vcenter=0.48f-pitch/PI;
 float vtop=fmaxf(0.0f,fminf(1.0f,vcenter-0.135f));
 float vbottom=fmaxf(0.0f,fminf(1.0f,vcenter+0.135f));
 const float xyz[12]={-1.0f, 1.0f,0.0f,  1.0f, 1.0f,0.0f,
                      -1.0f,-1.0f,0.0f,  1.0f,-1.0f,0.0f};
 const float uv[8]={ucenter-uhalf,vtop, ucenter+uhalf,vtop,
                    ucenter-uhalf,vbottom, ucenter+uhalf,vbottom};
 glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 glDisableClientState(GL_COLOR_ARRAY);
 glEnable(GL_TEXTURE_2D);
 glBindTexture(GL_TEXTURE_2D,swrc_textures[SWRC_SKY_PANEL]);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 glColor4f(1.0f,1.0f,1.0f,1.0f);
 glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
 glVertexPointer(3,GL_FLOAT,0,xyz);
 glTexCoordPointer(2,GL_FLOAT,0,uv);
 glDrawArrays(GL_TRIANGLE_STRIP,0,4);
 glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
 glDisable(GL_TEXTURE_2D);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
 glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);
}

static void render_sky(float yaw,float pitch){
 if(swrc_sky_mode==0)render_sky_screen(yaw,pitch);
 else if(swrc_sky_mode==1)render_sky_legacy(yaw,pitch);
}

/* v18 performance path: submit static geometry from two persistent GPU buffers
 * instead of re-uploading client-side vertex/index arrays on every frame.
 * SELECT toggles the existing v17 path for an A/B performance comparison.
 */
typedef struct {uint32_t vbytes,ibytes;} CacheOffset;
static CacheOffset swrc_offsets[8192];
static GLuint swrc_bufs[2]={0,0};
static int swrc_gpu_cache=0,swrc_use_cache=0;
static int swrc_volume_enabled=0; /* optional translucent light-volume preview, off by default */
static uint32_t swrc_last_calls=0,swrc_last_tris=0;
static int swrc_last_textures=0;

static void free_geometry_buffers(void){
 if(swrc_bufs[0]||swrc_bufs[1]){
  glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
  glDeleteBuffers(2,swrc_bufs);swrc_bufs[0]=swrc_bufs[1]=0;
 }
 swrc_gpu_cache=0;swrc_use_cache=0;
}

static int make_geometry_buffers(Scene *scene){
 if(!scene->raw||scene->h->chunk_count>8192)return 0;
 uint64_t totalv=0,totali=0;
 for(uint32_t i=0;i<scene->h->chunk_count;i++){
  Chunk *c=&scene->chunks[i];
  uint32_t vb=c->vertex_count*sizeof(Vertex),ib=c->index_count*sizeof(uint16_t);
  if(totalv+vb>0x3fffffff||totali+ib>0x3fffffff)return 0;
  swrc_offsets[i].vbytes=(uint32_t)totalv;
  swrc_offsets[i].ibytes=(uint32_t)totali;
  totalv+=vb;totali+=ib;
 }
 char msg[150];
 snprintf(msg,sizeof(msg),"GEOMETRY GPU CACHE REQUEST %lu VERTEX BYTES %lu INDEX BYTES",(unsigned long)totalv,(unsigned long)totali);
 logmsg(msg);
 glGenBuffers(2,swrc_bufs);
 if(!swrc_bufs[0]||!swrc_bufs[1]){
  logmsg("GEOMETRY BUFFER GENERATION FAILED");free_geometry_buffers();return 0;
 }
 glBindBuffer(GL_ARRAY_BUFFER,swrc_bufs[0]);
 glBufferData(GL_ARRAY_BUFFER,(GLsizei)totalv,NULL,GL_STATIC_DRAW);
 GLenum err=glGetError();
 if(err!=GL_NO_ERROR){
  snprintf(msg,sizeof(msg),"GEOMETRY VBO ALLOCATION ERROR %04X",(unsigned)err);
  logmsg(msg);free_geometry_buffers();return 0;
 }
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,swrc_bufs[1]);
 glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizei)totali,NULL,GL_STATIC_DRAW);
 err=glGetError();
 if(err!=GL_NO_ERROR){
  snprintf(msg,sizeof(msg),"GEOMETRY IBO ALLOCATION ERROR %04X",(unsigned)err);
  logmsg(msg);free_geometry_buffers();return 0;
 }
 for(uint32_t i=0;i<scene->h->chunk_count;i++){
  Chunk *c=&scene->chunks[i];
  glBufferSubData(GL_ARRAY_BUFFER,swrc_offsets[i].vbytes,
     c->vertex_count*sizeof(Vertex),scene->raw+c->vertex_offset);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,swrc_offsets[i].ibytes,
     c->index_count*sizeof(uint16_t),scene->raw+c->index_offset);
  err=glGetError();
  if(err!=GL_NO_ERROR){
   snprintf(msg,sizeof(msg),"GEOMETRY CACHE UPLOAD ERROR CHUNK %u GL %04X",i,(unsigned)err);
   logmsg(msg);free_geometry_buffers();return 0;
  }
 }
 glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 swrc_gpu_cache=swrc_use_cache=1;
 logmsg("GEOMETRY GPU CACHE UPLOADED SUCCESSFULLY (VBO DEFAULT)");
 return 1;
}
static void draw(Scene *s,float eye[3],float yaw,float pitch,int draw_layer){
 camera(eye,yaw,pitch);
 const float transform[16]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
 glMultMatrixf(transform);
 int use_buffers=swrc_use_cache && swrc_gpu_cache;
 glBindBuffer(GL_ARRAY_BUFFER,use_buffers?swrc_bufs[0]:0);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,use_buffers?swrc_bufs[1]:0);
 glEnableClientState(GL_VERTEX_ARRAY);
 glEnableClientState(GL_COLOR_ARRAY);
 uint32_t calls=0,tris=0;
 int texture_on=0;
 GLuint current_texture=0;
 int textured_chunks=0;
 for(uint32_t i=0;i<s->h->chunk_count;i++){
  Chunk *c=&s->chunks[i];
  /* Arena_GM VolumetricLight chunks are deliberately opt-in: need procedural shader. */
  if(c->reserved==4 && !swrc_volume_enabled)continue;
  if(draw_layer!=0 && c->reserved!=0 && c->reserved!=(uint32_t)draw_layer)continue;
  if(!visible(c,eye,yaw,pitch))continue;
  Vertex *v=(Vertex*)(s->raw+c->vertex_offset);
  uint16_t *idx=(uint16_t*)(s->raw+c->index_offset);
  uint32_t vb=swrc_offsets[i].vbytes,ib=swrc_offsets[i].ibytes;
  unsigned tid=(unsigned)c->cell_y;
  GLuint texture=(tid>0&&tid<=swrc_texture_count)?swrc_textures[tid]:0;
  if(texture){
   if(!texture_on){glEnable(GL_TEXTURE_2D);glEnableClientState(GL_TEXTURE_COORD_ARRAY);texture_on=1;}
   if(texture!=current_texture){glBindTexture(GL_TEXTURE_2D,texture);current_texture=texture;}
   glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
   glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),
     use_buffers?(const void*)(uintptr_t)(vb+offsetof(Vertex,uv)):(const void*)v[0].uv);
   textured_chunks++;
  }else if(texture_on){
   glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisable(GL_TEXTURE_2D);
   texture_on=0;current_texture=0;
  }
  glVertexPointer(3,GL_FLOAT,sizeof(Vertex),
    use_buffers?(const void*)(uintptr_t)vb:(const void*)v[0].xyz);
  glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),
    use_buffers?(const void*)(uintptr_t)(vb+offsetof(Vertex,rgba)):(const void*)v[0].rgba);
  /* Simple additive preview for the optional translucent volume meshes. */
  if(c->reserved==4){
   glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glDepthMask(GL_FALSE);
  }
  glDrawElements(GL_TRIANGLES,c->index_count,GL_UNSIGNED_SHORT,
    use_buffers?(const void*)(uintptr_t)ib:(const void*)idx);
  if(c->reserved==4){glDepthMask(GL_TRUE);glDisable(GL_BLEND);}
  calls++;tris+=c->index_count/3;
 }
 glDisableClientState(GL_TEXTURE_COORD_ARRAY);
 glDisable(GL_TEXTURE_2D);
 glDisableClientState(GL_COLOR_ARRAY);
 glDisableClientState(GL_VERTEX_ARRAY);
 glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 swrc_last_calls=calls;swrc_last_tris=tris;swrc_last_textures=textured_chunks;
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
static int measured_fps=0;
static void draw_scene_hud(int layer, int overview){
 const char *label=layer==0?"MESH AND BSP":layer==1?"MESH ONLY":"BSP ONLY";
 char timing_label[60];
 glDisable(GL_DEPTH_TEST);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 show_error_text("SWRC V21 SKY COMPARE",0);
 show_error_text(label,1);
 snprintf(timing_label,sizeof(timing_label),"FPS %u CALLS %u TRIS %u",measured_fps,swrc_last_calls,swrc_last_tris);
 show_error_text(timing_label,2);
 show_error_text(swrc_use_cache?"VBO ON  SELECT TO COMPARE":"VBO OFF SELECT TO COMPARE",3);
 show_error_text(swrc_sky_mode==0?"SKY SCREEN  L TO COMPARE":swrc_sky_mode==1?"SKY CYLINDER  L TO COMPARE":"SKY OFF  L TO COMPARE",4);
 show_error_text(swrc_volume_enabled?"VOLUME FX ON  CROSS TO TOGGLE":"VOLUME FX OFF CROSS TO TOGGLE",5);
 if(overview)show_error_text("OVERVIEW",6);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
 glEnable(GL_DEPTH_TEST);
}
static void draw_load_error(void) {
 glDisable(GL_DEPTH_TEST);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 show_error_text("SWRC LEVEL NOT LOADED",0);
 show_error_text(scene_error,1);
 show_error_text("SEE UX0 DATA SWRV00006 VIEWER LOG",2);
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
}
int main(void){
 FILE *clearlog=fopen("ux0:data/SWRV00006-viewer.log","w");
 if(clearlog){fputs("SWRC v21 two sky projection modes and walker neutral placeholder\n",clearlog);fclose(clearlog);}
 scene_trace[0]=0;
 log_app_mount();
 Scene scene;memset(&scene,0,sizeof(scene));scene.memblock=-1;
 const char *paths[]={
   "ux0:data/SWRV00006/scene.swv",
   "app0:scene.swv",
   "app0:/scene.swv",
   "ux0:app/SWRV00006/scene.swv",
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
 const char *texpaths[]={
   "app0:textures.swtx","app0:/textures.swtx",
   "app0:data/textures.swtx","ux0:app/SWRV00006/textures.swtx",
   "ux0:data/SWRV00006/textures.swtx"
 };
 int texture_loaded=0;
 for(unsigned i=0;i<sizeof(texpaths)/sizeof(texpaths[0])&&!texture_loaded;i++)
   texture_loaded=load_textures(texpaths[i]);
 if(!texture_loaded)logmsg("TEXTURES NOT AVAILABLE: GEOMETRY WILL RENDER UNTEXTURED");
 if(swrc_texture_count>=25)logmsg("SKY PANEL AND SKY TOP ORIGINAL TEXELS READY; L TOGGLE");
 if(ok && !make_geometry_buffers(&scene))logmsg("VBO UNAVAILABLE: USING V17 CLIENT ARRAY PATH");

 vglWaitVblankStart(GL_TRUE);
 sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
 glViewport(0,0,960,544);
 glClearColor(0.045f,0.065f,0.105f,1);
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_CULL_FACE);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();
 gluPerspective(75.0,960.0/544.0,24.0,VIEW*2.0);
 const float spawn[3]={2101.9768f,1829.5629f,-1901.798f};
 float pos[3]={spawn[0],spawn[1],spawn[2]},yaw=-9815.0f*DEG,pitch=0;
 int draw_layer=0,overview=0;
 uint32_t prev=0;
 unsigned frame=0;
 int64_t bench_start=sceKernelGetSystemTimeWide();
 uint64_t draw_us=0,swap_us=0;
 unsigned bench_frames=0;

 for(;;){
  SceCtrlData pad;memset(&pad,0,sizeof(pad));pad.lx=pad.ly=pad.rx=pad.ry=128;
  if(sceCtrlPeekBufferPositive(0,&pad,1)<1){sceKernelDelayThread(16000);continue;}
  if(pad.buttons&SCE_CTRL_CIRCLE)break;
  if((pad.buttons&SCE_CTRL_START)&&!(prev&SCE_CTRL_START)){
    memcpy(pos,spawn,sizeof(pos));yaw=-9815.0f*DEG;pitch=0;overview=0;
    logmsg("camera reset to PlayerStart");
  }
  if((pad.buttons&SCE_CTRL_TRIANGLE)&&!(prev&SCE_CTRL_TRIANGLE)){
    overview=!overview;
    if(overview){pos[0]=2500;pos[1]=-5500;pos[2]=9500;yaw=-1.5f;pitch=-1.22f;
      logmsg("camera overhead arena preset");}
    else{memcpy(pos,spawn,sizeof(pos));yaw=-9815.0f*DEG;pitch=0;
      logmsg("camera PlayerStart preset");}
  }
  if((pad.buttons&SCE_CTRL_LTRIGGER)&&!(prev&SCE_CTRL_LTRIGGER)){
    swrc_sky_mode=(swrc_sky_mode+1)%3;
    swrc_sky_enabled=(swrc_sky_mode!=2);
    logmsg(swrc_sky_mode==0?"SKY SCREEN PANORAMA":swrc_sky_mode==1?"SKY OLD CYLINDER":"SKY OFF (BACKGROUND CLEAR COLOR)");
  }
  if((pad.buttons&SCE_CTRL_CROSS)&&!(prev&SCE_CTRL_CROSS)){
    swrc_volume_enabled=!swrc_volume_enabled;
    logmsg(swrc_volume_enabled?"ARENA GM VOLUMETRIC LIGHT PREVIEW ON":"ARENA GM VOLUMETRIC LIGHT PREVIEW OFF");
  }
  if((pad.buttons&SCE_CTRL_SELECT)&&!(prev&SCE_CTRL_SELECT)&&swrc_gpu_cache){
    swrc_use_cache=!swrc_use_cache;
    logmsg(swrc_use_cache?"PERF MODE VBO ENABLED":"PERF MODE VBO DISABLED (V17 LEGACY PATH)");
  }
  if((pad.buttons&SCE_CTRL_SQUARE)&&!(prev&SCE_CTRL_SQUARE)){
    draw_layer=(draw_layer+1)%3;
    logmsg(draw_layer==0?"layer BOTH":draw_layer==1?"layer STATIC MESHES ONLY":"layer BSP ONLY");
  }
  prev=pad.buttons;
  /* Moving the right stick right must rotate toward camera-right in world XY. */
  yaw-=axis(pad.rx)*.04f;
  pitch=fminf(1.5f,fmaxf(-1.5f,pitch-axis(pad.ry)*.027f));
  float forward=-axis(pad.ly),strafe=axis(pad.lx);
  float speed=(pad.buttons&SCE_CTRL_RTRIGGER)?170:45;
  /* The renderer uses world (X,Y,Z) -> eye (X,Z,-Y). Camera-right in
     world XY is (+sin(yaw), -cos(yaw)), not (-sin(yaw),+cos(yaw). */
  pos[0]+=speed*(forward*cosf(yaw)+strafe*sinf(yaw));
  pos[1]+=speed*(forward*sinf(yaw)-strafe*cosf(yaw));
  if(pad.buttons&SCE_CTRL_UP)pos[2]+=speed;
  if(pad.buttons&SCE_CTRL_DOWN)pos[2]-=speed;
  int64_t t0=sceKernelGetSystemTimeWide();
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  if(ok){render_sky(yaw,pitch);draw(&scene,pos,yaw,pitch,draw_layer);draw_scene_hud(draw_layer,overview);}else draw_load_error();
  int64_t t1=sceKernelGetSystemTimeWide();
  vglSwapBuffers(GL_FALSE);
  int64_t t2=sceKernelGetSystemTimeWide();
  if(t1>t0)draw_us+=(uint64_t)(t1-t0);
  if(t2>t1)swap_us+=(uint64_t)(t2-t1);
  bench_frames++;
  if(t2-bench_start>=3000000){
   uint64_t elapsed=(uint64_t)(t2-bench_start);
   measured_fps=(int)((uint64_t)bench_frames*1000000u/elapsed);
   char perf[220];
   snprintf(perf,sizeof(perf),"PERF mode=%s frames=%u fps=%u calls=%u tris=%u textured=%d drawCPUms=%u swapWaitMs=%u",
     swrc_use_cache?"VBO":"LEGACY",bench_frames,measured_fps,
     swrc_last_calls,swrc_last_tris,swrc_last_textures,
     (unsigned)((draw_us/bench_frames)/1000u),(unsigned)((swap_us/bench_frames)/1000u));
   logmsg(perf);
   bench_frames=0;draw_us=0;swap_us=0;bench_start=t2;
  }
  if(++frame==1)logmsg("First frame submitted");
 }
 free_geometry_buffers();
 release(&scene);
 logmsg("Exited normally");
 return 0;
}