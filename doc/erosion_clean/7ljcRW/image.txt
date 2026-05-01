vec4 GetChannel0(vec2 uv)
{
    uv = clamp(uv, vec2(0.001), vec2(0.999));
    uv *= BUFFER_SIZE / iResolution.xy;
    return texture(iChannel0, uv);
}

vec4 GetChannel1(vec2 uv)
{
    uv = fract(uv);
    uv *= BUFFER_SIZE / iResolution.xy;
    return texture(iChannel1, uv);
}

vec4 map(vec3 p, out float erosion)
{
    vec2 uv = p.xz + vec2(0.5) - vec2(0.5) / BUFFER_SIZE;
    
    vec4 tex = GetChannel0(uv);
    float height = tex.x;
    vec3 normal = tex.yzz;
    normal.y = sqrt(1.0 - dot(normal.xz, normal.xz)); // Recover Y
    
    erosion = tex.w;
    
    return vec4(height, normal);
}

// Ray marching
float march(vec3 ro, vec3 rd, out vec3 normal, out int material, out float s_t)
{
    s_t = 9999.0;
    
    vec3 boxNormal;
    vec2 box = boxIntersection(ro, rd, vec3(0.5, 1.0, 0.5), boxNormal);
    
    if (box.y < 0.0)
    {
        return -1.0;
    }
    
    float tStart = max(0.0, box.x) + 1e-2;
    float tEnd = box.y - 1e-2;
    
    material = M_GROUND;

    float stepSize = 0.0;
    float stepScale = 1.0;
    float t = tStart;
    float altitude = 0.0;
    for (int i = 0; i < 32; i++)
    {
        vec3 pos = ro + rd * t;
        
        float foo;
        vec4 tex = map(pos, foo);
        float h = tex.x;
        normal = tex.yzw;
        
        altitude = pos.y - h;
        
        s_t = max(0.0, min(s_t, altitude / t));
        
        if (altitude < 0.0)
        {
            if (i < 1) // Sides
            {
                /*
                if (diff < -0.1) // Bottom
                {
                    return -1.0;
                }
                */
                if (pos.y < 0.35) // Flat bottom
                {
                    s_t = 9999.0;
                    return -1.0;
                }
                normal = boxNormal;
                material = M_STRATA;
                break;
            }
        }
        
        if (altitude < 0.0)
        {
            // Step back (contact/edge refinement)
            stepScale *= 0.5;
            t -= stepSize * stepScale;
        }
        else
        {
            // Step forward
            // Accelerate the ray by distance to terrain. This would result in horrible aliasing if we didn't do refinement above
            stepSize = abs(altitude) + 1e-2;
            //stepSize = (tEnd - tStart) / float(stepCount);
            t += stepSize * stepScale;
        }
    }
    
    if (t > tEnd)
    {
        s_t = 9999.0;
        return -1.0;
    }
    
#ifdef WATER
    vec3 waterNormal;
    vec2 water = boxIntersection(ro, rd, vec3(0.5, WATER_HEIGHT, 0.5), waterNormal);
    if ((water.y > 0.0 && (water.x < t || t < 0.0)) && material != M_STRATA)
    {
        t = max(0.0, water.x);
        normal = waterNormal;
        material = M_WATER;
    }
    else
#endif

    if (box.y < 0.0)
    {
        return -1.0;
    }

    return t;
}

vec3 GetReflection(vec3 p, vec3 r, vec3 sun, float smoothness)
{
    vec3 refl = SkyColor(r, sun) * 4.0;
    
    vec3 foo;
    float r_t;
    int r_material;
    march(p, r, foo, r_material, r_t);
    return refl * (1.0 - exp(-r_t * 10.0 * sq(smoothness)));
}

// Main image output
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
#ifdef SHOW_BUFFER
    float debugWidth = iResolution.y / 2.0;
#else
    float debugWidth = 0.0;
#endif

    // ==========================================================================================
    // Set up camera
    // ==========================================================================================
    vec2 cameraAngle = vec2(iTime * 0.1 + PI * 1.5, -0.17 * PI);
    float cameraDistance = 5.0;
    
    // Intro animation
    cameraAngle.x -= exp(-iTime * 5.0) * 4.0;
    cameraDistance += exp(-iTime * 5.0) * 5.0;
        
    if (iMouse.z > 0.5)
    {
        cameraAngle.x = PI / 2.0;
        cameraAngle.y = -0.2 * PI;
    }
    
    vec3 ro = vec3(0.0, 0.325, 0.0);
    vec3 rd = CameraRay(11.0, iResolution.xy, fragCoord.xy - vec2(debugWidth / 2.0, 0.0));
    
    mat3 rot = CameraRotation(cameraAngle.yx);
    rd = rot * rd;
    ro = rot * ro + rot * vec3(0, 0, cameraDistance);
    
    // ==========================================================================================
    // Ray march
    // ==========================================================================================
    vec4 foo;
    vec3 normal;
    int material;
    float t = march(ro, rd, normal, material, foo.w);
    
    // ==========================================================================================
    // Shade
    // ==========================================================================================
#ifdef FIXED_SUN
    vec3 sun = normalize(vec3(-1.0, 0.4, 0.05));
#else
    vec3 sun = rot * normalize(vec3(-1.0, 0.1, 0.25));
#endif
    
    vec3 fogColor = 1.0 - exp(-SkyColor(rd, sun) * 2.0);
    
    vec3 color;
    
    if (t < 0.0)
    {
        // Sky
        color = fogColor * (1.0 + pow(fragCoord.y / iResolution.y, 3.0) * 3.0) * 0.5;
#ifdef SHOW_NORMALS
        color = vec3(0.5, 0.5, 1.0);
#endif
    }
    else
    {
        vec3 pos = ro + rd * t;
        
        float erosion;
        //float h = map(pos, erosion).x;
        float diff = pos.y - map(pos, erosion).x;
        
        vec4 breakupTex = vec4(0.0);
        
#ifdef DETAIL_TEXTURE
        breakupTex = GetChannel1(pos.xz + 0.5);
        vec3 breakupNormal = breakupTex.zyw;
        if (material != M_STRATA)
        {
            normal = normalize(normal + breakupNormal.xzy * 0.1);
        }            
#endif
        float breakup = breakupTex.x;

        vec3 f0 = vec3(0.04);
        float smoothness = 0.0;
        float reflAmount = 0.0;
        float occlusion = 1.0;
        
        vec3 r = reflect(rd, normal);
        
        vec3 diffuseColor = vec3(0.5);
        if (material == M_GROUND)
        {
#ifndef GREYSCALE
            occlusion = sq(saturate(erosion + 0.5));
            
            // Cliffs / Dirt
            diffuseColor = CLIFF_COLOR * smoothstep(0.4, 0.52, pos.y);
            //diffuseColor = mix(diffuseColor, DIRT_COLOR, smoothstep(0.8, 0.9, normal.y - erosion * 0.1 + breakup * 0.05));
            diffuseColor = mix(diffuseColor, DIRT_COLOR, smoothstep(0.3, 0.0, occlusion + breakup * 1.0));
            
            // Grass
            vec3 grassMix = mix(GRASS_COLOR1, GRASS_COLOR2, smoothstep(0.4, 0.6, pos.y - erosion * 0.05 + breakup * 0.3));
            //diffuseColor = mix(diffuseColor, grassMix, smoothstep(0.8, 0.95, normal.y - erosion * 0.2 + breakup * 0.1));
            diffuseColor = mix(diffuseColor, grassMix, smoothstep(WATER_HEIGHT + 0.05, WATER_HEIGHT + 0.02, pos.y - breakup * 0.02) * smoothstep(0.8, 1.0, normal.y + breakup * 0.1));
            
            // Snow
            diffuseColor = mix(diffuseColor, vec3(1.0), smoothstep(0.53, 0.6, pos.y + breakup * 0.1));
    #ifdef WATER
            // Sand (beach)
            diffuseColor = mix(diffuseColor, SAND_COLOR, smoothstep(WATER_HEIGHT + 0.005, WATER_HEIGHT, pos.y + breakup * 0.01));
    #endif
            diffuseColor *= 1.0 + breakup * 0.5;
#endif
        }
        else if (material == M_STRATA)
        {
#ifndef GREYSCALE
            vec3 strata = smoothstep(0.0, 1.0, cos(diff * vec3(130.0, 190.0, 250.0)));
            diffuseColor = vec3(0.3);
            diffuseColor = mix(diffuseColor, vec3(0.50), strata.x);
            diffuseColor = mix(diffuseColor, vec3(0.55), strata.y);
            diffuseColor = mix(diffuseColor, vec3(0.60), strata.z);
            
            diffuseColor *= exp(diff * 10.0) * vec3(1.0, 0.9, 0.7);
#endif
        }
        else if (material == M_WATER)
        {
            float shore = normal.y > 1e-2 ? exp(-diff * 60.0) : 0.0;
            float foam = normal.y > 1e-2 ? smoothstep(0.005, 0.0, diff + breakup * 0.005) : 0.0;
        
            diffuseColor = mix(WATER_COLOR, WATER_SHORE_COLOR, shore);
            
            diffuseColor = mix(diffuseColor, vec3(1.0), foam);
            
            //f0 = vec3(0.2);
            smoothness = 0.95;
        }
        
        float shadow = 1.0;
        
#ifdef SHADOWS
        if (material != M_STRATA)
        {
            // Shadow ray
            float s_t;
            int s_material;
            march(pos + vec3(0.0, 1.0, 0.0) * 1e-4, sun, foo.xyz, s_material, s_t);
            shadow = 1.0 - exp(-s_t * 20.0);
            //fragColor = vec4(shadow, shadow, shadow, 1.0);
            //return;
        }
#endif

        // Ambient
        color = diffuseColor * SkyColor(normal, sun) * occlusion * Fd_Lambert();
        // Direct
        color += Shade(diffuseColor, f0, smoothness, normal, -rd, sun, SUN_COLOR * shadow);
        // Bounce
        color += diffuseColor * SUN_COLOR * (dot(normal, sun * vec3(1.0,-1.0, 1.0)) * 0.5 + 0.5) * Fd_Lambert() / PI;
        // Reflection
        color += GetReflection(pos, r, sun, smoothness) * F_Schlick(f0, dot(-rd, normal));
        // Fog
        float fog = exp(-t * t * smoothstep(WATER_HEIGHT, WATER_HEIGHT - 0.5, pos.y) * 0.5);
        //color = mix(fogColor, color, fog);
        
        //color = vec3(exp(-max(0.0, -erosion * 2.0)));
        //color = vec3(occlusion);

#ifdef SHOW_DIFFUSE
        color = pow(diffuseColor, vec3(1.0 / 2.2));
#elif defined(SHOW_NORMALS)
        color = normal.xzy * 0.5 + 0.5;
#endif
    }
    
    vec3 boxNormal;
    vec2 box = boxIntersection(ro, rd, vec3(0.5, 1.0, 0.5), boxNormal);
    
    float costh = dot(rd, sun);
    float phaseR = PhaseRayleigh(costh);
    float phaseM = PhaseMie(costh, 0.6);
    
    vec2 od = vec2(0.0);
    vec3 tsm;
    vec3 sct = vec3(0.0);
    float rayLength = (t > 0.0 ? t : box.y) - box.x;
    float stepSize = rayLength / 16.0;
    for (float i = 0.0; i < 16.0; i++)
    {
        vec3 p = ro + rd * (box.x + (i + 0.5) * stepSize);
        
        float h = max(0.0, p.y - 0.35);
        //float d = exp(-h * 5.0);
        float d = 1.0 - saturate(h / 0.2);
        
        if (p.y < 0.35)
        {
            d = 0.0;
        }
        
        float densityR = d * 1e5;
        float densityM = d * 1e5;
        
        od += stepSize * vec2(densityR, densityM);
        
        tsm = exp(-(od.x * C_RAYLEIGH + od.y * C_MIE));
        
        sct += tsm * C_RAYLEIGH * phaseR * densityR * stepSize;
        sct += tsm * C_MIE * phaseM * densityM * stepSize;
    }
    
    color = color * tsm + sct * 10.0;

#if !defined(SHOW_NORMALS) && !defined(SHOW_DIFFUSE)
    color = Tonemap_ACES(color);
    color = pow(color, vec3(1.0 / 2.2));
#endif

    // Dither
    color += texture(iChannel2, mod(fragCoord.xy, iChannelResolution[2].xy) / iChannelResolution[2].xy).xxx / 255.0;

#ifdef SHOW_BUFFER
    vec2 debugUV = fragCoord / debugWidth;
    if (debugUV.x > 0.0 && debugUV.x < 1.0 && debugUV.y > 0.0 && debugUV.y < 2.0)
    {
        vec4 tex = GetChannel0(fract(debugUV));
        vec3 normal = tex.yzz;
        normal.y = sqrt(1.0 - dot(normal.xz,normal.xz));
        color = debugUV.y > 1.0 ? normal.xzy * 0.5 + 0.5 : tex.xxx * 2.5 - 0.75;
    }
#endif
    
    fragColor = vec4(color, 1.0);
}