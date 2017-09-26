#ifndef FOX_RENDER_GROUP_H
#define FOX_RENDER_GROUP_H

/* NOTE :

   1) Everywhere outside the renderer, Y _always_ goes upward, X to the right.
   
   2) All bitmaps including the render target are assumed to be bottom-up
      (meaning that the first row pointer points to the bottom-most row
       when viewed on screen).

   3) Unless otherwise specified, all inputs to the renderer are in world
      coordinate ("meters"), NOT pixels.  Anything that is in pixel values
      will be explicitly marked as such.

   4) Z is a special coordinate because it is broken up into discrete slices,
      and the renderer actually understands these slices (potentially).

    // TODO(casey): ZHANDLING

   5) All color values specified to the renderer as V4's are in
      NON-premulitplied alpha.
*/

struct loaded_bitmap
{
    int32 alignX, alignY;
    int32 width;
    int32 height;
    int32 pitch;
    void *memory;
};

struct hero_bitmaps
{
    // Where should we draw this bitmap?
    v2 align;

    loaded_bitmap head;
    loaded_bitmap cape;
    loaded_bitmap torso;
};

struct enviromnet_map
{
    // Level of Detail
    loaded_bitmap lod[4];

    real32 pZ;
};

// NOTE : All the pieces are in the giant buffer 
struct render_basis
{
    v3 pos;
};

struct render_entry_basis
{
    render_basis *basis;
    // This offset also contains alignment of the bitmap
    // Therefore, we don't have to mind alignment in DrawBitmap function
    v3 offset;
};


enum render_group_entry_type
{
    RenderGroupEntryType_render_group_entry_clear,

    RenderGroupEntryType_render_group_entry_coordinate_system,
    RenderGroupEntryType_render_group_entry_bitmap,
    RenderGroupEntryType_render_group_entry_rectangle,
};

// TODO : Get rid of this! This is another sloppy code!
struct render_group_entry_header
{
    render_group_entry_type type;
};

struct render_group_entry_clear
{
    render_group_entry_header header;

    v4 color;
};

struct render_group_entry_coordinate_system
{
    v2 origin;
    v2 xAxis;
    v2 yAxis;
    v4 color;

    loaded_bitmap *bitmap;
    loaded_bitmap *normalMap;

    enviromnet_map *top;
    enviromnet_map *middle;
    enviromnet_map *bottom;
    
    // v2 points[16];
};
    
struct render_group_entry_bitmap
{
    render_entry_basis entryBasis;
    
    loaded_bitmap *bitmap;

    v4 color;
};

struct render_group_entry_rectangle
{
    render_entry_basis entryBasis;

    v4 color;
    // NOTE : This is not the actual meter based dim that entity has!
    // This is pixel based rendering dim(how big it is in the screen!!)
    // Therefore, it is always multiplied by metersToPixels
    v2 dim;
};

struct render_group
{
    render_basis *defaultBasis;
    
    real32 metersToPixels;

    uint32 maxPushBufferSize;
    uint32 pushBufferSize;
    uint8 *pushBufferBase;
};

#endif