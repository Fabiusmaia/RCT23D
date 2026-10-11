#version 330 core

uniform mat4  uInvViewProj;
uniform vec3  uZenithColour;
uniform vec3  uHorizonColour;
uniform vec3  uSunDirection;
uniform vec3  uSunColour;
uniform float uSunVisible;

in vec2 fNdc;

out vec4 oColour;

void main()
{
    // View ray through this pixel.
    vec4 nearPoint = uInvViewProj * vec4(fNdc, -1.0, 1.0);
    vec4 farPoint = uInvViewProj * vec4(fNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);

    // Pale near the horizon, deeper towards the top; below the horizon (past the map edge) stays horizon coloured.
    float height = clamp(dir.z, 0.0, 1.0);
    vec3 colour = mix(uHorizonColour, uZenithColour, pow(height, 0.6));

    // The sun and the bright haze around it.
    float toSun = max(dot(dir, uSunDirection), 0.0);
    colour += uSunColour * uSunVisible * (pow(toSun, 600.0) * 4.0 + pow(toSun, 12.0) * 0.25);

    oColour = vec4(colour, 1.0);
}
