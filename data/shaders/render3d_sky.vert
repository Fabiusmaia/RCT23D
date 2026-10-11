#version 330 core

in vec2 vPosition;

out vec2 fNdc;

void main()
{
    fNdc = vPosition;
    gl_Position = vec4(vPosition, 0.0, 1.0);
}
