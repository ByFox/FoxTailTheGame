#include "fox_render_group.h"

struct bilinear_sample
{
    uint32 a, b, c, d;
};

inline v4
SRGB255ToLinear1(v4 c)
{
    v4 result;

    // NOTE : because the input value is in 255 space, 
    // convert it to be in linear 1 space
    real32 inv255 = 1.0f/255.0f;

    result.r = Square(inv255*c.r);
    result.g = Square(inv255*c.g);
    result.b = Square(inv255*c.b);
    // NOTE : alpha is not a part of this operation!
    // because it just means how much should we blend
    result.a = inv255*c.a;

    return result;
}

inline v4
Linear1ToSRGB255(v4 c)
{
    v4 result;

    result.r = 255.0f*Root2(c.r);
    result.g = 255.0f*Root2(c.g);
    result.b = 255.0f*Root2(c.b);
    result.a = 255.0f*c.a;

    return result;
}

inline v4
Unpack4x8(uint32 packed)
{
    v4 result = {(real32)((packed >> 16) & 0xFF),
                (real32)((packed >> 8) & 0xFF),
                (real32)((packed >> 0) & 0xFF),   
                (real32)((packed >> 24) & 0xFF)};
        
    return result;
}

inline v4
UnscaleAndBiasNormal(v4 normal)
{
    v4 result;

    real32 inv255 = 1.0f / 255.0f;

    result.x = -1.0f + 2.0f*(inv255*normal.x);
    result.y = -1.0f + 2.0f*(inv255*normal.y);
    result.z = -1.0f + 2.0f*(inv255*normal.z);

    result.w = inv255*normal.w;

    return result;
}


inline v4
SRGBBilinearBlend(bilinear_sample sample, real32 fX, real32 fY)
{
    v4 pixel0 = Unpack4x8(sample.a);
    v4 pixel1 = Unpack4x8(sample.b);
    v4 pixel2 = Unpack4x8(sample.c);
    v4 pixel3 = Unpack4x8(sample.d);
            
    pixel0 = SRGB255ToLinear1(pixel0);
    pixel1 = SRGB255ToLinear1(pixel1);
    pixel2 = SRGB255ToLinear1(pixel2);
    pixel3 = SRGB255ToLinear1(pixel3);

    v4 result = Lerp(Lerp(pixel0, fX, pixel1), fY, Lerp(pixel2, fX, pixel3));

    return result;
}

inline bilinear_sample
BilinearSample(loaded_bitmap *texture, int x, int y)
{
    bilinear_sample result;

    uint8 *texelPtr = ((uint8 *)texture->memory + 
                        x*sizeof(uint32) + 
                        y*texture->pitch);
        
    // Get all 4 texels around the texelX and texelY
    result.a = *(uint32 *)(texelPtr);
    result.b = *(uint32 *)(texelPtr + sizeof(uint32));
    result.c = *(uint32 *)(texelPtr + texture->pitch);
    result.d = *(uint32 *)(texelPtr + texture->pitch + sizeof(uint32));

    return result;
}

inline v3 
SampleEnvironmentMap(v2 screenSpaceUV, v3 sampleBounce, v3 normal, real32 roughness, enviromnet_map *map)
{
    uint32 lodIndex = (uint32)(roughness * (real32)(ArrayCount(map->lod) - 1.0f) + 0.5f);
    Assert(lodIndex < ArrayCount(map->lod));

    loaded_bitmap *lod = &map->lod[lodIndex];

    real32 distanceFromMiddle = 0.01f;
    real32 c = distanceFromMiddle / sampleBounce.y;
    v2 offset = c * V2(sampleBounce.x, sampleBounce.z);
    v2 uv = screenSpaceUV + offset;

    real32 tX = (uv.x*(real32)(lod->width - 2));
    real32 tY = (uv.y*(real32)(lod->height - 2));

    int32 x = (int32)tX;
    int32 y = (int32)tY;

    real32 fX = tX - (real32)x;
    real32 fY = tX - (real32)y;

    bilinear_sample sample = BilinearSample(lod, x, y);

    v3 result = SRGBBilinearBlend(sample, fX, fY).xyz;

    return result;
}

internal void
DrawRectangle(loaded_bitmap *buffer, v2 realMin, v2 realMax, v4 color)
{
    real32 r = color.r;
    real32 g = color.g;
    real32 b = color.b;
    real32 a = color.a;

    //Because we are going to display to the screen
    // It should be in int(pixel)
    int32 minX = RoundReal32ToInt32(realMin.x);
    int32 minY = RoundReal32ToInt32(realMin.y);
    int32 maxX = RoundReal32ToInt32(realMax.x);
    int32 maxY = RoundReal32ToInt32(realMax.y);

    //buffer overflow protection
    if(minX < 0)
    {
        minX = 0;
    }
    if(minY < 0)
    {
        minY = 0;
    }
    if(maxX > buffer->width)
    {
        maxX = buffer->width;
    }
    if(maxY > buffer->height)
    {
        maxY = buffer->height;
    }

    uint32 color32 = ((RoundReal32ToInt32(a * 255.0f) << 24) |
                    (RoundReal32ToInt32(r * 255.0f) << 16) |
                    (RoundReal32ToInt32(g * 255.0f) << 8) |
                    (RoundReal32ToInt32(b * 255.0f) << 0));

    uint8 *row = ((uint8 *)buffer->memory + 
                    minX * BITMAP_BYTES_PER_PIXEL +
                    minY * buffer->pitch);
    for(int y = minY;
        y < maxY;
        ++y)
    {
        uint32 *pixel = (uint32 *)row;        
        for(int x = minX;
            x < maxX;
            ++x)
        {
            *pixel++ = color32;
        }
        row += buffer->pitch;  
    }
}

internal void
DrawRectangleOutline(loaded_bitmap *buffer, v2 realMin, v2 realMax, v4 color)
{
    // NOTE : This is of course in pixels
    real32 thickness = 2.0f;

    real32 width = realMax.x - realMin.x;
    real32 height = realMax.y - realMin.y;

    // Left
    DrawRectangle(buffer, realMin, realMin + V2(thickness, height), color);
    // Right
    DrawRectangle(buffer, realMax - V2(thickness, height), realMax, color);
    // Top
    DrawRectangle(buffer, realMin, realMin + V2(width, thickness), color);
    // Bottom
    DrawRectangle(buffer, realMax - V2(width, thickness), realMax, color);
}


internal void
DrawRectangleSlowly(loaded_bitmap *buffer, v2 origin, v2 xAxis, v2 yAxis, v4 color,
                    loaded_bitmap *texture, loaded_bitmap *normalMap,
                    enviromnet_map *top, 
                    enviromnet_map *middle,
                    enviromnet_map *bottom)
{
    // NOTE : Premulitplied color alpha!
    color.rgb *= color.a;

    real32 invXAxisSquare = 1.0f/LengthSq(xAxis);
    real32 invYAxisSquare = 1.0f/LengthSq(yAxis);
    
    uint32 color32 = ((RoundReal32ToInt32(color.a * 255.0f) << 24) |
                    (RoundReal32ToInt32(color.r * 255.0f) << 16) |
                    (RoundReal32ToInt32(color.g * 255.0f) << 8) |
                    (RoundReal32ToInt32(color.b * 255.0f) << 0));
            
    int32 widthMax = buffer->width;
    int32 heightMax = buffer->height;

    // Setting these to as low or high as they can so that we can modify
    int minX = widthMax;
    int minY = heightMax;
    int maxX = 0;
    int maxY = 0;
    
    v2 points[4] = {origin, origin+xAxis, origin+yAxis, origin+xAxis+yAxis};
    for(uint32 pointIndex = 0;
        pointIndex < ArrayCount(points);
        ++pointIndex)
    {
        v2 testPoint = points[pointIndex];
        int32 floorX = FloorReal32ToInt32(testPoint.x);
        int32 ceilX = CeilReal32ToInt32(testPoint.x);
        int32 floorY = FloorReal32ToInt32(testPoint.y);
        int32 ceilY = CeilReal32ToInt32(testPoint.y);
        
        if(minX > floorX) {minX = floorX;}
        if(minY > floorY) {minY = floorY;}
        if(maxX < ceilX) {maxX = ceilX;}
        if(maxY < ceilY) {maxY = ceilY;}
    }

    if(minX < 0)
    {
        minX = 0;
    }
    if(minY < 0)
    {
        minY = 0;
    }
    if(maxX > buffer->width)
    {
        maxX = buffer->width;
    }
    if(maxY > buffer->height)
    {
        maxY = buffer->height;
    }

    uint8 *row = ((uint8 *)buffer->memory + 
                            minX * BITMAP_BYTES_PER_PIXEL +
                            minY * buffer->pitch);
        
    for(int y = minY;
        y < maxY;
        ++y)
    {
        uint32 *pixel = (uint32 *)row;        
        for(int x = minX;
            x < maxX;
            ++x)
        {
            v2 screenSpaceUV = V2((real32)x/widthMax, (real32)y/heightMax);

#if 1
            v2 pixelPos = V2i(x, y);
            // pixelPos based on the origin
            v2 basePos = pixelPos - origin;

            // The checking positions are all different because
            // the positions should have same origins with the edges that are checking against.
            // which means, they should be in same space!
            // Also, we need to consider clockwise to get the origin.
            // For example, the origin of the upper edge should be V2(xAxis, yAxis)
            real32 edge0 = Inner(basePos, -yAxis);
            real32 edge1 = Inner(basePos - xAxis, xAxis);
            real32 edge2 = Inner(basePos - xAxis - yAxis, yAxis);
            real32 edge3 = Inner(basePos - yAxis, -xAxis);

            if((edge0 < 0) && (edge1 < 0) && (edge2 < 0) && (edge3 < 0))
            {
#if 1
                // Transform to the u-v coordinate system to get the bitmap based pixels!
                // First of all, we need to divide the value with the legnth of the axis 
                // to make the axis unit length
                // Second, we need to divdie with the length of the axis AGAIN
                // because we need the coordinate to be matched to the normalized axis!
                real32 u = invXAxisSquare*Inner(basePos, xAxis);
                real32 v = invYAxisSquare*Inner(basePos, yAxis);

                // TODO(casey): SSE clamping.
                //Assert(u >= 0.0f && u <= 1.0f);
                //Assert(v >= 0.0f && v <= 1.0f);

                // NOTE : x and y in texture in floating point value.
                // real32 texelX = (u*(real32)(texture->width - 1) + 0.5f);
                // real32 texelY = (v*(real32)(texture->height - 1) + 0.5f);

                // TODO : Put this back to the original thing!
                real32 texelX = 1.0f + (u*(real32)(texture->width - 3) + 0.5f);
                real32 texelY = 1.0f + (v*(real32)(texture->height - 3) + 0.5f);

                // What pixel should we use in the bitmap?
                // NOTE : x and y in texture in integer value
                int32 texelPixelX = (int32)(texelX);
                int32 texelPixelY = (int32)(texelY);
                
                real32 fX = texelX - (real32)texelPixelX;
                real32 fY = texelY - (real32)texelPixelY;

                bilinear_sample texelSample = BilinearSample(texture, texelPixelX, texelPixelY);
                v4 texel = SRGBBilinearBlend(texelSample, fX, fY);

                if(normalMap)
                {
                    bilinear_sample normalSample = BilinearSample(normalMap, texelPixelX, texelPixelY);

                    v4 normal0 = Unpack4x8(normalSample.a);
                    v4 normal1 = Unpack4x8(normalSample.b);
                    v4 normal2 = Unpack4x8(normalSample.c);
                    v4 normal3 = Unpack4x8(normalSample.d);

                    v4 normal = Lerp(Lerp(normal0, fX, normal1), fY, Lerp(normal2, fX, normal3));

                    normal = UnscaleAndBiasNormal(normal);

                    // NOTE : The eye vector is always assumed to be [0, 0, 1]
                    // which is straigt out of the screen
                    v3 eyeVector = {0, 0, 1};

                    // e^T * N * N means n direction vector wich size of e transposed to N
                    // The equation below is the simplified version of -e + 2e^T*N*N
                    // because the x and y component of eyeVector is 0, e dot N is normal.z! 
                    v3 bounceDirection = 2.0f*normal.z*normal.xyz;
                    bounceDirection.z -= 1.0f;

                    // NOTE : Tells us the blend of the enviromnet
                    real32 tEnvMap = normal.y;
                    // NOTE : How much should we grab from the farmap comparing to the middlemap 
                    real32 tFarMap = 0.0f;
                    enviromnet_map *farMap = 0;
                    if(tEnvMap < -0.5f)
                    {
                        farMap = bottom;
                        // NOTE: If the tEnvMap is -0.5f, it means it's not even looking at the 
                        // bottom so the tFarMap should be 0
                        // if it is -1.0f, it means it's directly looking at the bottom
                        // so the tFarMap should be 1
                        tFarMap = -1.0f - 2.0f*tEnvMap;

                        // NOTE : We want to keep the y value of bounceDirection to be positive
                        // so that the funtion SampleEnvironmentMap doesn't care about the environment
                        bounceDirection.y = -bounceDirection.y;
                    }
                    else if(tEnvMap > 0.5f)
                    {
                        farMap = top;
                        tFarMap = 2.0f*tEnvMap - 1.0f;
                    }

                    v3 lightColor = {0, 0, 0};
                    if(farMap)
                    {
                        v3 farMapColor = SampleEnvironmentMap(screenSpaceUV, bounceDirection, normal.xyz, normal.w, farMap);
                        lightColor = Lerp(lightColor, tFarMap, farMapColor);
                    }
                    
                    texel.rgb += texel.a*lightColor;
                }
                // texel = Hadamard(texel, color);
                texel.r = Clamp01(texel.r);
                texel.g = Clamp01(texel.g);
                texel.b = Clamp01(texel.b);
                
                // color channels of the dest bmp
                v4 dest = Unpack4x8(*pixel);

                dest = SRGB255ToLinear1(dest);

                real32 invTexelA = (1.0f - texel.a);
                
                // NOTE : Color blending equation : (1-sa)*d + s -> this s should be premulitplied by alpha!
                // Color blending equation with color : (1-sa)*d + ca*c*s
                v4 blended = {invTexelA*dest.r + texel.r,
                                invTexelA*dest.g + texel.g,
                                invTexelA*dest.b + texel.b,
                                (texel.a + dest.a - texel.a*dest.a)};

                v4 blended255 = Linear1ToSRGB255(blended);
    
                // NOTE : Put it back as a, r, g, b order
                *pixel = (((uint32)(blended255.a + 0.5f) << 24) |
                        ((uint32)(blended255.r + 0.5f) << 16) |
                        ((uint32)(blended255.g + 0.5f) << 8) |
                        ((uint32)(blended255.b + 0.5f) << 0));
                        
#else
                *pixel = color32;
#endif
            }

            // We could not use *pixel++ as we did because 
            // we are performing some tests against pixels!
            pixel++;
#else
            // Use this to see what region of pixels are we testing!
            *pixel++ = color32;
#endif
        }

        row += buffer->pitch;  
    }
}

/*
    NOTE : This is how DrawBitmap works!
    1. blend two bitamps(buffer and sourceBitmap)
        Let's say that the result is blendedColor
    2. blend the screen and the blendedColor
        This is our result!
    
    Basically, the function is B(Cs, B(S, D))
    and the equation is 
    Alpha = A0 + A1 - A0A1
*/

internal void
DrawBitmap(loaded_bitmap *buffer, loaded_bitmap *sourceBitmap,
            real32 realX, real32 realY, real32 cAlpha = 1.0f)
{
    //Because we are going to display to the screen
    int32 minX = RoundReal32ToInt32(realX);
    int32 minY = RoundReal32ToInt32(realY);
    int32 maxX = minX + sourceBitmap->width;
    int32 maxY = minY + sourceBitmap->height;

    int32 sourceOffsetX = 0;
    int32 sourceOffsetY = 0;

    //buffer overflow protection
    if(minX < 0)
    {
        sourceOffsetX = -minX;
        minX = 0;
    }
    if(minY < 0)
    {
        sourceOffsetY = -minY;
        minY = 0;
    }
    if(maxX > buffer->width)
    {
        maxX = buffer->width;
    }
    if(maxY > buffer->height)
    {
        maxY = buffer->height;
    }

    Assert(maxX - minX <= sourceBitmap->width);
    Assert(maxY - minY <= sourceBitmap->height);

    int32 bytesPerPixel = BITMAP_BYTES_PER_PIXEL;
    uint8 *sourceRow = (uint8 *)sourceBitmap->memory + sourceOffsetY*sourceBitmap->pitch + sourceOffsetX*bytesPerPixel;
    uint8 *destRow = (uint8 *)buffer->memory + 
                        minY * buffer->pitch + 
                        minX * bytesPerPixel;
    for(int32 y = minY;
        y < maxY;
        ++y)
    {
        uint32 *dest = (uint32 *)destRow;
        uint32 *source = (uint32 *)sourceRow;
        for(int32 x = minX;
            x < maxX;
            ++x)
        {
            // color channels of the source bmp
            v4 texel = {(real32)((*source >> 16) & 0xFF),
                        (real32)((*source >> 8) & 0xFF),   
                        (real32)((*source >> 0) & 0xFF),
                        (real32)((*source >> 24) & 0xFF)};
            
            texel = SRGB255ToLinear1(texel);
            texel *= cAlpha;

            // color channels of the dest bmp
            v4 dexel = {(real32)((*dest >> 16) & 0xFF),
                        (real32)((*dest >> 8) & 0xFF),
                        (real32)((*dest >> 0) & 0xFF),
                        (real32)((*dest >> 24) & 0xFF)};

            dexel = SRGB255ToLinear1(dexel);

            real32 invSourceA = (1.0f - texel.a);

            v4 result = {invSourceA*dexel.r + texel.r,
                        invSourceA*dexel.g + texel.g,
                        invSourceA*dexel.b + texel.b,
                        255.0f*(texel.a + dexel.a - texel.a*dexel.a)};

            result = Linear1ToSRGB255(result);

            *dest = (((uint32)(result.a + 0.5f) << 24) |
                    ((uint32)(result.r + 0.5f) << 16) |
                    ((uint32)(result.g + 0.5f) << 8) |
                    ((uint32)(result.b + 0.5f) << 0));

            ++dest;
            ++source;
        }
        
        destRow += buffer->pitch;
        sourceRow += sourceBitmap->pitch;
    }
}

internal v2
GetRenderEntityBasePoint(render_group *group, render_entry_basis *entryBasis, v2 screenCenter)
{
    v3 entityBasePos = entryBasis->basis->pos;
    real32 zFudge = (1.0f + 0.1f*(entityBasePos.z + entryBasis->offsetZ));

    real32 entityGroundX = screenCenter.x + group->metersToPixels*zFudge*entityBasePos.x;
    real32 entityGroundY = screenCenter.y - group->metersToPixels*zFudge*entityBasePos.y;
    real32 entityZ = -group->metersToPixels*entityBasePos.z;

    v2 center = {entityGroundX + entryBasis->offset.x,
                entityGroundY + entryBasis->offset.y + entryBasis->offsetZ + entryBasis->entityZC*entityZ};

    return center;
}

internal void
RenderGroupToOutput(render_group *renderGroup, loaded_bitmap *outputTarget)
{
    v2 screenCenter = {0.5f * (real32)outputTarget->width, 0.5f * (real32)outputTarget->height};

    for(uint32 baseIndex = 0;
        baseIndex < renderGroup->pushBufferSize;
        )
    {
        render_group_entry_header *header = (render_group_entry_header *)(renderGroup->pushBufferBase + baseIndex);
        void *data = (uint8 *)header + sizeof(*header);
        
        switch(header->type)
        {
            case RenderGroupEntryType_render_group_entry_clear:
            {
                render_group_entry_clear *entry = (render_group_entry_clear *)data;
                
                // Most hardware has faster way to clear the buffer rather than 
                // drawing the whole buffer again, so change the thing after!
                // TODO : Maybe add alpha value to this buffer?
                DrawRectangle(outputTarget, V2(0, 0), 
                    V2((real32)outputTarget->width, (real32)outputTarget->height), entry->color);

                                baseIndex += sizeof(*entry) + sizeof(*header);
            }break;

            case RenderGroupEntryType_render_group_entry_coordinate_system:
            {
                render_group_entry_coordinate_system *entry = (render_group_entry_coordinate_system *)data;

                // Draw Origin
                v2 dim = V2i(5, 5);
                v2 pos = entry->origin;
                
                v4 color = V4(1.0f, 0.9f, 0.1f, 1.0f);

                DrawRectangleSlowly(outputTarget, pos, entry->xAxis, entry->yAxis, entry->color,
                                    entry->bitmap, entry->normalMap,
                                    entry->top, entry->middle, entry->bottom);

                DrawRectangle(outputTarget, pos - dim, pos, color);

                // Draw end of the X axis
                pos = entry->origin + entry->xAxis;
                DrawRectangle(outputTarget, pos - dim, pos, color);
                
                // Draw end of the Y axis
                pos = entry->origin + entry->yAxis;
                DrawRectangle(outputTarget, pos - dim, pos, color);
                
                // Draw end of the Xaxis AND Y axis
                pos = entry->origin + entry->xAxis + entry->yAxis;
                DrawRectangle(outputTarget, pos - dim, pos, color);

                baseIndex += sizeof(*entry) + sizeof(*header);
            }break;

            case RenderGroupEntryType_render_group_entry_bitmap:
            {
                render_group_entry_bitmap *entry = (render_group_entry_bitmap *)data;
#if 0

                // Every entry of this type should have bitmap!
                Assert(entry->bitmap);
                v2 basePos = GetRenderEntityBasePoint(renderGroup, &entry->entryBasis, screenCenter);
                DrawBitmap(outputTarget, entry->bitmap, basePos.x, basePos.y, entry->a);

#endif
                baseIndex += sizeof(*entry) + sizeof(*header);
            }break;

            case RenderGroupEntryType_render_group_entry_rectangle:
            {
                render_group_entry_rectangle *entry = (render_group_entry_rectangle *)data;
                
                v2 basePos = GetRenderEntityBasePoint(renderGroup, &entry->entryBasis, screenCenter);
                DrawRectangle(outputTarget, basePos, basePos + entry->dim, entry->color);

                baseIndex += sizeof(*entry) + sizeof(*header);        
            }break;

            InvalidDefaultCase;
        }
    }
}

internal render_group *
AllocateRenderGroup(memory_arena *arena, uint32 maxPushBufferSize, real32 metersToPixels)
{
    render_group *result = PushStruct(arena, render_group);
    result->pushBufferBase = (uint8 *)PushSize(arena, maxPushBufferSize);
    result->maxPushBufferSize = maxPushBufferSize;
    result->pushBufferSize = 0;
    result->metersToPixels = metersToPixels;

    // So that we don't need to check defaultBasis is NULL
    render_basis *defaultBasis = PushStruct(arena, render_basis);
    result->defaultBasis = defaultBasis;

    return result;
}


#define PushRenderElement(group, type) (type *)PushRenderElement_(group, sizeof(type), RenderGroupEntryType_##type)
internal void *
PushRenderElement_(render_group *group, uint32 size, render_group_entry_type type)
{
    void *result = 0;
    // NOTE : Because we are now pushing the header and the entry spartely!
    size += sizeof(render_group_entry_type);

    if(group->pushBufferSize + size < group->maxPushBufferSize)
    {
        render_group_entry_header *header = (render_group_entry_header *)(group->pushBufferBase + group->pushBufferSize);
        header->type = type;
        result = (uint8 *)header + sizeof(*header);
        group->pushBufferSize += size;
    }
    else
    {
        InvalidCodePath;
    }

    return result;
}

// Add piece to the piecegroup so when we draw the group, we can draw this piece.
inline void
PushPiece(render_group *group, loaded_bitmap *bitmap, v2 offset, real32 offsetZ, 
        v2 align, v2 dim, v4 color, real32 entityZC)
{
    render_group_entry_bitmap *piece = PushRenderElement(group, render_group_entry_bitmap);
    if(piece)
    {
        piece->bitmap = bitmap;

        piece->entryBasis.basis = group->defaultBasis;
        piece->entryBasis.offset = group->metersToPixels*V2(offset.x, -offset.y) - align;
        piece->entryBasis.offsetZ = group->metersToPixels*offsetZ;
        piece->entryBasis.entityZC = entityZC;

        piece->color = color;
    }
}

inline void
PushBitmap(render_group *group, loaded_bitmap *bitmap, 
        v2 offset, real32 offsetZ, v2 align, real32 alpha = 1.0f, real32 entityZC = 1.0f)
{
    PushPiece(group, bitmap, offset, offsetZ, align, V2(0, 0), V4(1.0f, 1.0f, 1.0f, alpha), entityZC);
}

inline void
PushRect(render_group *group, v2 offset, real32 offsetZ, 
            v2 dim, v4 color, real32 entityZC = 1.0f)
{
    render_group_entry_rectangle *piece = PushRenderElement(group, render_group_entry_rectangle);
    if(piece)
    {
        v2 halfDim = 0.5f*group->metersToPixels*dim;

        piece->entryBasis.basis = group->defaultBasis;
        piece->entryBasis.offset = group->metersToPixels*V2(offset.x, -offset.y) - halfDim;
        piece->entryBasis.offsetZ = group->metersToPixels*offsetZ;
        piece->entryBasis.entityZC = entityZC;

        piece->color = color;

        piece->dim = group->metersToPixels*dim;
    }
}

inline void
PushRectOutline(render_group *group, v2 offset, real32 offsetZ, 
            v2 dim, v4 color, real32 entityZC = 1.0f)
{
    real32 thickness = 0.1f;
    
    // Top and Bottom
    PushRect(group, offset - V2(0, 0.5f*dim.y), offsetZ, V2(dim.x, thickness), color, entityZC);
    PushRect(group, offset + V2(0, 0.5f*dim.y), offsetZ, V2(dim.x, thickness), color, entityZC);
    
    // Left and Right
    PushRect(group, offset - V2(0.5f*dim.x, 0), offsetZ, V2(thickness, dim.y), color, entityZC);
    PushRect(group, offset + V2(0.5f*dim.x, 0), offsetZ, V2(thickness, dim.y), color, entityZC);
}

inline void
Clear(render_group *group, v4 color)
{
    render_group_entry_clear *piece = PushRenderElement(group, render_group_entry_clear);
    if(piece)
    {
        piece->color = color;
    }
}

inline render_group_entry_coordinate_system *
PushCoordinateSystem(render_group *group, loaded_bitmap *bitmap, v2 origin, v2 xAxis, v2 yAxis, v4 color,
                    loaded_bitmap *normalMap, enviromnet_map *top, enviromnet_map *middle, enviromnet_map *bottom)
{
    render_group_entry_coordinate_system *piece = PushRenderElement(group, render_group_entry_coordinate_system);
    if(piece)
    {
        piece->origin = origin;
        piece->xAxis = xAxis;
        piece->yAxis = yAxis;
        piece->color = color;
        piece->bitmap = bitmap;

        piece->normalMap = normalMap;

        piece->top = top;
        piece->middle = middle;
        piece->bottom = bottom;

        piece->top = top;
        piece->middle = middle;
        piece->bottom = bottom;
    }

    return piece;
}