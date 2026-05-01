// Copyright 2020 Clay John

// Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
// and associated documentation files (the "Software"), to deal in the Software without restriction, 
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do 
// so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all copies or 
// substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT 
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

/*
==========================================================================================

Buffer A generates the heightmap (X), normals (YZ) and erosion mask used for coloring (W)

==========================================================================================
*/

// code adapted from https://www.shadertoy.com/view/llsGWl
// name: Gavoronoise
// author: guil
// license: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License
// Code has been modified to return analytic derivatives and to favour 
// direction quite a bit.
vec3 Erosion(in vec2 p, vec2 dir)
{
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float f = 2.0 * PI;
    vec3 va = vec3(0.0);
    float wt = 0.0;
    for (int i=-2; i<=1; i++)
    {
        for (int j=-2; j<=1; j++)
        {
            vec2 o = vec2(i, j);
            vec2 h = hash(ip - o) * 0.5;
            vec2 pp = fp + o - h;
            float d = dot(pp, pp);
            float w = exp(-d * 2.0);
            wt +=w;
            float mag = dot(pp, dir);
            va += vec3(cos(mag * f), -sin(mag * f) * (pp * 0.0 + dir)) * w;
        }
    }
    return va / wt;
}

vec2 Heightmap(vec2 uv)
{
    vec2 p = uv * HEIGHT_TILES;
    
    // FBM terrain
    vec3 n = vec3(0.0);
    float nf = 1.0;
    float na = HEIGHT_AMP;
    for (int i = 0; i < HEIGHT_OCTAVES; i++)
    {
        n += noised(p * nf) * na * vec3(1.0, nf, nf);
        na *= HEIGHT_GAIN;
        nf *= HEIGHT_LACUNARITY;
    }
    
    // [-1, 1] -> [0, 1]
    n.x = n.x * 0.5 + 0.5;
    
    // Take the curl of the normal to get the gradient facing down the slope
    vec2 dir = n.zy * vec2(1.0, -1.0) * EROSION_SLOPE_STRENGTH;
    
    // Now we compute another fbm type noise
    // erosion is a type of noise with a strong directionality
    // we pass in the direction based on the slope of the terrain
    // erosion also returns the slope. we add that to a running total
    // so that the direction of successive layers are based on the
    // past layers
    vec3 h = vec3(0.0);
    
    float a = 0.5;
    float f = 1.0;
    
    // Smooth valleys
    //a *= (smoothstep(0.0, 1.0, n.x));
    
    a *= smoothstep(WATER_HEIGHT - 0.1, WATER_HEIGHT + 0.2, n.x);

    int octaves = EROSION_OCTAVES;
    
#ifdef COMPARISON_SLIDER
    if (iMouse.z > 0.5 && (iMouse.x / iResolution.x - 0.5) * 1.5 < (0.5 - uv.y) || iMouse.z < 0.5 && 1.0 - uv.y > (-cos(iTime) * 1.0 + 0.5))
    {
        octaves = 0;
        h.x = 0.5;
    }
#endif

    for (int i = 0; i < octaves; i++)
    {
        h += Erosion(p * EROSION_TILES * f, dir + h.zy * vec2(1.0, -1.0) * EROSION_BRANCH_STRENGTH) * a * vec3(1.0, f, f);
        a *= EROSION_GAIN;;
        f *= EROSION_LACUNARITY;
    }
    
    return vec2(n.x + (h.x - 0.5) * EROSION_STRENGTH, h.x);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    if (fragCoord.x >= BUFFER_SIZE.x || fragCoord.y >= BUFFER_SIZE.y)
    {
        return;
    }

    vec2 uv = fragCoord / BUFFER_SIZE;
    uv.x += TIME_SCROLL_OFFSET;
    
    float s = 0.1;//scaling factor for heightmap
    
    vec2 h = Heightmap(uv);
    
    // Calculate an accurate normal from neighbouring points
    vec2 uv1 = uv + vec2(1.0, 0.0) / 512.0;
    vec2 uv2 = uv + vec2(0.0, 1.0) / 512.0;
    vec2 h1 = Heightmap(uv1);
    vec2 h2 = Heightmap(uv2);
    vec3 v1 = vec3(uv1 - uv, (h1.x - h.x));
    vec3 v2 = vec3(uv2 - uv, (h2.x - h.x));
    vec3 normal = normalize(cross(v1, v2)).xzy;
    
    fragColor = vec4(h.x, normal.xz, h.y);
}