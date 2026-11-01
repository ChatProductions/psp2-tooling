/* SWRC v9 geometry viewer for PS Vita. No original game engine or gameplay.
 * Data: ux0:data/SWRV00001/scene.swv (generated from the user's own PC files).
 * Padded, 64-byte v9 header/chunk structures. Safe enough for diagnostics.
 */
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
typedef struct {unsigned char *raw; size_t size; Header *h; Chunk *chunks;} Scene;
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
static int load(Scene *s,const char *path) {
 memset(s,0,sizeof(*s));
 FILE *f=fopen(path,"rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}
 long len=ftell(f);
 if(len<64||len>96*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
 s->size=(size_t)len;
 s->raw=malloc(s->size);
 if(!s->raw){fclose(f);return 0;}
 size_t n=fread(s->raw,1,s->size,f);fclose(f);
 if(n!=s->size)goto bad;
 s->h=(Header*)s->raw;
 Header *h=s->h;
 if(memcmp(h->magic,"SWV9",4)||h->version!=9||h->header_size!=64||
    h->vertex_stride!=36||h->chunk_stride!=64||h->reserved!=0||
    h->chunk_table_offset!=64||h->chunk_count<1||h->chunk_count>8192||
    h->file_size!=s->size||64ull+h->chunk_count*64ull>s->size)goto bad;
 if(crc32(s->raw+64,s->size-64)!=h->crc32)goto bad;
 s->chunks=(Chunk*)(s->raw+64);
 for(uint32_t i=0;i<h->chunk_count;i++){
  Chunk *c=&s->chunks[i];
  uint64_t endv=(uint64_t)c->vertex_offset+c->vertex_count*36ull;
  uint64_t endi=(uint64_t)c->index_offset+c->index_count*2ull;
  if(c->vertex_count<3||c->vertex_count>40000||c->index_count<3||
    c->index_count%3||c->index_count/3!=c->bsp_triangles+c->mesh_triangles||
    c->vertex_offset<64+h->chunk_count*64||endv>c->index_offset||
    endi>s->size||c->vertex_offset%4||c->index_offset%2)goto bad;
  for(int a=0;a<3;a++)if(!isfinite(c->bounds[a])||!isfinite(c->bounds[a+3])||
     c->bounds[a]>c->bounds[a+3])goto bad;
 }
 return 1;
bad:
 free(s->raw);memset(s,0,sizeof(*s));return 0;
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
static void draw_demo(void){
 static const float pts[]={-150,0,-150, 150,0,-150, 0,0,200};
 static const uint8_t rgb[]={255,80,80,80,255,80,80,80,255};
 camera((float[3]){0,-900,100},1.5707963f,-.10f);
 float t[16]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1};
 glMultMatrixf(t);
 glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
 glVertexPointer(3,GL_FLOAT,0,pts);glColorPointer(3,GL_UNSIGNED_BYTE,0,rgb);
 glDrawArrays(GL_TRIANGLES,0,3);
 glDisableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
}
int main(void){
 Scene scene;
 int ok=load(&scene,"ux0:data/SWRV00001/scene.swv");
 if(!ok)ok=load(&scene,"app0:scene.swv");
 logmsg(ok?"SWV9 scene loaded":"No SWV9 scene found; showing fallback triangle");
 if(!vglInit(0x800000)){logmsg("vglInit failed");free(scene.raw);return 2;}
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
  if(ok)draw(&scene,pos,yaw,pitch);else draw_demo();
  vglSwapBuffers(GL_FALSE);
 }
 free(scene.raw);
 logmsg("Exited normally");
 return 0;
}