#version 330 core

uniform sampler2DArray  uTexture;
uniform sampler2DShadow uShadowMap;
uniform mat4  uLightViewProj;
uniform int   uShadowsEnabled;
uniform float uShadowTexel;
uniform int   uShadowPass;
uniform vec3  uSunDirection;
uniform vec3  uSunColour;
uniform vec3  uAmbientColour;
uniform vec3  uFogColour;
uniform vec3  uSkyColour;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3  uCameraPos;
uniform int   uTransparentPass;
uniform float uTime;
// Cross-fade between two sets of sprites: 1 keeps the pixels below uFade, 2 keeps the rest, 0 keeps everything.
uniform int   uDitherMode;
uniform float uFade;

in vec3       fNormal;
in vec2       fUV;
flat in float fLayer;
in vec4       fColour;
in vec3       fWorldPos;
flat in float fBillboard;
flat in vec4  fRegion;

out vec4 oColour;

// Fraction of sunlight reaching this point: 1 in light, 0 in full shadow, softened over a few shadow map texels.
float SunVisibility(vec3 normal)
{
    if (uShadowsEnabled == 0)
    {
        return 1.0;
    }
    vec4 light = uLightViewProj * vec4(fWorldPos, 1.0);
    vec3 coords = light.xyz / light.w * 0.5 + 0.5;
    if (coords.x <= 0.0 || coords.y <= 0.0 || coords.x >= 1.0 || coords.y >= 1.0 || coords.z >= 1.0)
    {
        return 1.0;
    }
    // Surfaces at a grazing angle to the sun need more bias to avoid shadowing themselves. Sprite faces get a fixed,
    // larger bias since their depth only approximates the object.
    // One shadow map texel covers about a world unit, so the bias is a couple of units in depth (range 24000).
    float bias = fBillboard > 0.5 ? 0.0003 : 0.00009 + 0.00025 * (1.0 - max(dot(normal, uSunDirection), 0.0));
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            sum += texture(uShadowMap, vec3(coords.xy + vec2(x, y) * uShadowTexel, coords.z - bias));
        }
    }
    return sum / 9.0;
}

// Ordered dither threshold in [0, 1), fixed to the screen so the cross-fade pattern does not crawl.
float DitherThreshold()
{
    const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;
    return (bayer[p.y * 4 + p.x] + 0.5) / 16.0;
}

void main()
{
    if (uDitherMode != 0)
    {
        bool incoming = DitherThreshold() < uFade;
        if ((uDitherMode == 1) != incoming)
        {
            discard;
        }
    }

    // Faces of projected sprites extend past the sprite; never sample the neighbouring images in the atlas.
    if (fRegion.x >= 0.0
        && (fUV.x < fRegion.x || fUV.y < fRegion.y || fUV.x > fRegion.z || fUV.y > fRegion.w))
    {
        discard;
    }

    vec4 base = fColour;
    if (fLayer >= 0.0)
    {
        vec4 texel = texture(uTexture, vec3(fUV, fLayer));
        if (fBillboard > 0.5)
        {
            // Take coverage from a sharper mip level than the colour: averaged alpha in small mip levels makes thin
            // features such as fences disappear at a distance, while full resolution alone makes them sparkle.
            texel.a = texture(uTexture, vec3(fUV, fLayer), -1.5).a;
        }
        base *= texel;
    }

    if (uTransparentPass == 0)
    {
        if (base.a < 0.5)
        {
            discard;
        }
        base.a = 1.0;
    }

    if (uShadowPass != 0)
    {
        // Only depth matters when rendering the shadow map.
        oColour = vec4(1.0);
        return;
    }

    vec3 normal = fBillboard > 0.5 ? vec3(0.0, 0.0, 1.0) : normalize(fNormal);
    float diffuse;
    vec3 ambient;
    if (fBillboard > 0.5)
    {
        // Sprites already contain the game's baked lighting, so only dim them for time of day and shadow.
        diffuse = 0.75;
        ambient = uAmbientColour;
    }
    else
    {
        diffuse = max(dot(normal, uSunDirection), 0.0);
        // Light from the sky above is brighter than light bounced off the ground below.
        ambient = uAmbientColour * (0.78 + 0.22 * normal.z);
    }

    float sun = SunVisibility(normal);
    vec3 colour = base.rgb * (ambient + uSunColour * diffuse * sun);

    vec3 toCamera = normalize(uCameraPos - fWorldPos);
    if (uTransparentPass != 0)
    {
        // Water: gentle moving ripples, the sky reflected at shallow angles and the sun's glint.
        vec3 waterNormal = normalize(vec3(
            0.06 * sin(fWorldPos.x * 0.15 + uTime * 1.7), 0.06 * sin(fWorldPos.y * 0.13 - uTime * 1.3), 1.0));
        float fresnel = pow(1.0 - max(dot(waterNormal, toCamera), 0.0), 3.0);
        colour = mix(colour, uSkyColour, 0.15 + 0.6 * fresnel);
        float glint = pow(max(dot(reflect(-uSunDirection, waterNormal), toCamera), 0.0), 80.0);
        colour += uSunColour * glint * sun * 1.5;
        base.a = mix(base.a, 1.0, fresnel * 0.5);
    }

    float distance = length(fWorldPos - uCameraPos);
    float fog = clamp((distance - uFogStart) / max(uFogEnd - uFogStart, 1.0), 0.0, 1.0);
    colour = mix(colour, uFogColour, fog * fog);

    oColour = vec4(colour, base.a);
}
