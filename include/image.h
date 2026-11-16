#ifndef _IMAGE_H
#define _IMAGE_H
#include <life.h>
#ifndef IMAGES
#define IMAGES 512
#endif
#define IMAGE_PREPARING 1
#define IMAGE_ACTIVE    2
#define IMAGE_ENDED     3
#define IMAGE_EXIT      1
#define IMAGE_ABORT     2
#define IMAGE_EXEC      3
#define IMAGE_TIMEOUT   4
#define IMAGE_OBSERVER  1
#define IMAGE_CLEANUP   2
typedef struct Imageid Imageid;
typedef struct Image   Image;
struct Imageid
{
  u64 generation;
  u32 index, reserved;
};
struct Image
{
  u64    generation;
  Handle pid;
  Lifeid life;
  u64    number, sourceimage;
  u32    state, reason;
  u16    owner, observers, cleanup, reserve;
};
STATIC_ASSERT(IMAGES > 0 && IMAGES <= 512, image_capacity);
STATIC_ASSERT(sizeof(Image) == 64, image_budget);
/* Trusted constructor; allocates a fresh number from the retained Life. */
int imagecreate(Lifeid life, u64 sourceimage, Imageid *out);
int imageorigin(Imageid id, u64 sourceimage);
int imagereserve(Imageid id, u32 count);
int imageusefloor(Imageid id);
int imageactivate(Imageid id);
int imageend(Imageid id, u32 reason);
int imagehold(Imageid id, u32 kind);
int imagedrop(Imageid id, u32 kind);
/* Drop the Proc/preparation owner only after ENDED. */
int imagedetach(Imageid id);
int imagestat(Imageid id, Image *out);
/* Cold path, bounded by IMAGES; only returns an existing retained identity. */
int imagelookup(Lifeid life, u64 number, Imageid *out);
int imagefind(Handle pid, u64 number, Imageid *out);
int imagecurrent(Lifeid life, Imageid *out);
#endif
