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
varying vec2 texCoord;
varying vec4 vertexColor;

void main(void)
{
    gl_FragColor = vertexColor * texture2D(baseTexture, texCoord);
}
