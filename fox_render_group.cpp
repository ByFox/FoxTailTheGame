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
SampleEnvironmentMap(v2 screenSpaceUV, v3 sampleDirection, real32 roughness, 
                    enviromnet_map *map, real32 distanceFromMapInZ)
{
    /* NOTE :

       ScreenSpaceUV tells us where the ray is being cast _from_ in
       normalized screen coordinates.

       SampleDirection tells us what direction the cast is going -
       it does not have to be normalized.

       Roughness says which LODs of Map we sample from.

       DistanceFromMapInZ says how far the map is from the sample point in Z, given
       in meters.
    */

    // NOTE : Pick which LOD to sample from
    uint32 lodIndex = (uint32)(roughness * (real32)(ArrayCount(map->lod) - 1) + 0.5f);
    Assert(lodIndex < ArrayCount(map->lod));

    loaded_bitmap *lod = &map->lod[lodIndex];

    // NOTE  Compute the distance to the map and the scaling
    // factor for meters to UVs
    real32 uvsPerMeter = 0.05f;
    real32 c = (uvsPerMeter*distanceFromMapInZ) / sampleDirection.y;
    v2 offset = c * V2(sampleDirection.x, sampleDirection.z);

    // NOTE : Find the intersection point
    v2 uv = screenSpaceUV + offset;

    uv.x = Clamp01(uv.x);
    uv.y = Clamp01(uv.y);
    
    // NOTE : Bilinear sample
    real32 tX = (uv.x*(real32)(lod->width - 2));
    real32 tY = (uv.y*(real32)(lod->height - 2));

    int32 x = (int32)tX;
    int32 y = (int32)tY;

    real32 fX = tX - (real32)x;
    real32 fY = tY - (real32)y;

#if 0
    // NOTE : Turn this on to see where in the map you're sampling!
    uint8 *TexelPtr = ((uint8 *)LOD->Memory) + Y*LOD->Pitch + X*sizeof(uint32);
    *(uint32 *)TexelPtr = 0xFFFFFFFF;
#endif

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
                    enviromnet_map *bottom,
                    real32 pixelsToMeters)
{
    // NOTE : Premulitplied color alpha!
    color.rgb *= color.a;

    real32 xAxisLength = Length(xAxis);
    real32 yAxisLength = Length(yAxis);

    real32 invXAxisSquare = 1.0f/LengthSq(xAxis);
    real32 invYAxisSquare = 1.0f/LengthSq(yAxis);
    
    v2 nxAxis = (yAxisLength / xAxisLength) * xAxis;
    v2 nyAxis = (xAxisLength / yAxisLength) * yAxis;

    // NOTE : NzScale could be a parameter if we want people to
    // have control over the amount of scaling in the Z direction
    // that the normals appear to have.
    real32 nzScale = 0.5f*(xAxisLength + yAxisLength);
    
    int32 widthMax = buffer->width - 1;
    int32 heightMax = buffer->height - 1;

    // TODO : This will need to be specified separately!!
    real32 originZ = 0.0f;
    real32 originY = (origin + 0.5f*xAxis + 0.5f*yAxis).y;
    real32 fixedCastY = originY / heightMax;

    uint32 color32 = ((RoundReal32ToInt32(color.a * 255.0f) << 24) |
                    (RoundReal32ToInt32(color.r * 255.0f) << 16) |
                    (RoundReal32ToInt32(color.g * 255.0f) << 8) |
                    (RoundReal32ToInt32(color.b * 255.0f) << 0));
        
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
                // NOTE : This is for the card like normal, and the y value 
                // should be the fixed value for each card
                v2 screenSpaceUV = {(real32)x/widthMax, fixedCastY};
                real32 zDiff = pixelsToMeters * ((real32)y - originY);
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
                real32 texelX= ((u*(real32)(texture->width - 2)));
                real32 texelY = ((v*(real32)(texture->height - 2)));

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

                    // NOTE : Because normal is 255 space, put it back to -101 space
                    normal = UnscaleAndBiasNormal(normal);

                    // Because this normal axis is based on the xAxis and yAxis,
                    // recompute it based on those axises
                    normal.xy = normal.x*nxAxis + normal.y*nyAxis;
                    // This is not a 100% correct value, but it does the job.
                    normal.z *= nzScale;
                    normal.xyz = Normalize(normal.xyz);

                    // e^T * N * N means n direction vector wich size of e transposed to N
                    // The equation below is the simplified version of -e + 2e^T*N*N where e is eyevector 0, 0, 1
                    // because the x and y component of eyeVector is 0, e dot N is normal.z! 
                    v3 bounceDirection = 2.0f*normal.z*normal.xyz;
                    bounceDirection.z -= 1.0f;

                    bounceDirection.z = -bounceDirection.z;

                    enviromnet_map *farMap = 0;
                    real32 pZ = originZ + zDiff;
                    real32 mapZ = 2.0f;
                    // NOTE : Tells us the blend of the enviromnet
                    real32 tEnvMap = bounceDirection.y;
                    // NOTE : How much should we grab from the farmap comparing to the middlemap 
                    real32 tFarMap = 0.0f;
                    if(tEnvMap < -0.5f)
                    {
                        farMap = bottom;
                        // NOTE: If the tEnvMap is -0.5f, it means it's not even looking at the 
                        // bottom so the tFarMap should be 0
                        // if it is -1.0f, it means it's directly looking at the bottom
                        // so the tFarMap should be 1
                        tFarMap = -1.0f - 2.0f*tEnvMap;
                    }
                    else if(tEnvMap > 0.5f)
                    {
                        farMap = top;
                        tFarMap = 2.0f*tEnvMap - 1.0f;
                    }

                    v3 lightColor = {0, 0, 0};
                    if(farMap)
                    {
                        real32 distanceFromMapInZ = farMap->pZ - pZ;
                        v3 farMapColor = SampleEnvironmentMap(screenSpaceUV, bounceDirection, normal.w, farMap, distanceFromMapInZ);
                        lightColor = Lerp(lightColor, tFarMap, farMapColor);
                    }
                    
                    texel.rgb += texel.a*lightColor;
#if 1
                    // NOTE : Draws the bounce direction
                    texel.rgb = V3(0.5f, 0.5f, 0.5f) + 0.5f*bounceDirection;
                    texel.rgb *= texel.a;
#endif
                }
                texel = Hadamard(texel, color);

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
    real32 zFudge = (1.0f + 0.1f*(entityBasePos.z + entryBasis->offset.z));

    real32 entityGroundX = screenCenter.x + group->metersToPixels*zFudge*entityBasePos.x;
    real32 entityGroundY = screenCenter.y + group->metersToPixels*zFudge*entityBasePos.y;
    real32 entityZ = group->metersToPixels*entityBasePos.z;

    v2 center = {entityGroundX + entryBasis->offset.x,
                entityGroundY + entryBasis->offset.y + entryBasis->offset.z + entityZ};

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
                                    entry->top, entry->middle, entry->bottom, 1.0f/renderGroup->metersToPixels);

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
#if 1
                // Every entry of this type should have bitmap!
                Assert(entry->bitmap);
                v2 basePos = GetRenderEntityBasePoint(renderGroup, &entry->entryBasis, screenCenter);
                DrawBitmap(outputTarget, entry->bitmap, basePos.x, basePos.y, entry->color.a);

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

inline void
PushBitmap(render_group *group, loaded_bitmap *bitmap, v3 offset, v4 color = V4(1, 1, 1, 1))
{
    render_group_entry_bitmap *piece = PushRenderElement(group, render_group_entry_bitmap);
    if(piece)
    {
        v2 align = V2i(bitmap->alignX, bitmap->alignY);

        piece->bitmap = bitmap;
        piece->entryBasis.basis = group->defaultBasis;
        piece->entryBasis.offset.xy = group->metersToPixels*V2(offset.x, offset.y) - align;
        piece->entryBasis.offset.z = group->metersToPixels*offset.z;

        piece->color = color;
    }
}

inline void
PushRect(render_group *group, v3 offset, v2 dim, v4 color)
{
    render_group_entry_rectangle *piece = PushRenderElement(group, render_group_entry_rectangle);
    if(piece)
    {
        v2 halfDim = 0.5f*group->metersToPixels*dim;

        piece->entryBasis.basis = group->defaultBasis;
        piece->entryBasis.offset.xy = group->metersToPixels*V2(offset.x, offset.y) - halfDim;
        piece->entryBasis.offset.z = group->metersToPixels*offset.z;

        piece->color = color;

        piece->dim = group->metersToPixels*dim;
    }
}

inline void
PushRectOutline(render_group *group, v3 offset, v2 dim, v4 color)
{
    real32 thickness = 0.1f;
    
    // Top and Bottom
    PushRect(group, offset - V3(0, 0.5f*dim.y, 0), V2(dim.x, thickness), color);
    PushRect(group, offset + V3(0, 0.5f*dim.y, 0), V2(dim.x, thickness), color);
    
    // Left and Right
    PushRect(group, offset - V3(0.5f*dim.x, 0, 0), V2(thickness, dim.y), color);
    PushRect(group, offset + V3(0.5f*dim.x, 0, 0), V2(thickness, dim.y), color);
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