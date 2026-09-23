/* ---- converted ---- */
#version 300 es
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
precision highp int;
// #version 300 es
// gl3_FragmentShader
#ifdef GL_ES
    precision highp float;
#endif
uniform sampler2D baseTexture;
in vec2 texCoord;
in vec4 vertexColor;
out vec4 color;
void main(void)
{
    color = vertexColor * texture(baseTexture, texCoord);
}
/* ---- original ---- */
#version 300 es
// gl3_FragmentShader
#ifdef GL_ES
    precision highp float;
#endif
uniform sampler2D baseTexture;
in vec2 texCoord;
in vec4 vertexColor;
out vec4 color;
void main(void)
{
    color = vertexColor * texture(baseTexture, texCoord);
}
