#version 330 core

uniform mat4  uViewProj;
uniform vec3  uCameraRight;
uniform float uRotation;

in vec3  vPosition;
in vec3  vNormal;
in vec2  vUV;
in float vLayer;
in vec4  vColour;
in vec2  vOffset;
in float vRotation;
in float vFlags;

out vec3       fNormal;
out vec2       fUV;
flat out float fLayer;
out vec4       fColour;
out vec3       fWorldPos;
flat out float fBillboard;
flat out vec4  fRegion;

void main()
{
    fRegion = vec4(-1.0);
    // Geometry baked for a different isometric view direction is moved outside the clip volume.
    if (vRotation >= 0.0 && abs(vRotation - uRotation) > 0.5)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        fNormal = vec3(0.0, 0.0, 1.0);
        fUV = vec2(0.0);
        fLayer = -1.0;
        fColour = vec4(0.0);
        fWorldPos = vec3(0.0);
        fBillboard = 0.0;
        return;
    }

    // Flags: 0 = plain geometry, 1 = billboard, 2 = projected game sprite. Both sprite kinds carry baked lighting.
    vec3 world = vPosition;
    fBillboard = vFlags > 0.5 ? 1.0 : 0.0;
    if (abs(vFlags - 1.0) < 0.5)
    {
        // Cylindrical billboard: rotate around the vertical axis to face the camera.
        world += uCameraRight * vOffset.x + vec3(0.0, 0.0, vOffset.y);
    }

    fNormal = vNormal;
    if (abs(vFlags - 2.0) < 0.5)
    {
        // Projected sprite: the normal and offset carry the sprite's rectangle in the texture atlas.
        fRegion = vec4(vOffset, vNormal.xy);
        fNormal = vec3(0.0, 0.0, 1.0);
    }
    fUV = vUV;
    fLayer = vLayer;
    fColour = vColour;
    fWorldPos = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}
