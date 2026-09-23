/* ---- converted ---- */
#version 300 es
precision highp float;
precision highp int;
out vec4 osg_FrontColor;
out vec4 osg_BackColor;
// #version 300 es
// gl3_VertexShader
#ifdef GL_ES
    precision highp float;
#endif
in vec4 osg_Vertex;
in vec4 osg_Color;
in vec4 osg_MultiTexCoord0;
uniform mat4 osg_ModelViewProjectionMatrix;
out vec2 texCoord;
out vec4 vertexColor;
void osg_ffp_main()
{
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
    texCoord = osg_MultiTexCoord0.xy;
    vertexColor = osg_Color; 
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}

/* ---- original ---- */
#version 300 es
// gl3_VertexShader
#ifdef GL_ES
    precision highp float;
#endif
in vec4 osg_Vertex;
in vec4 osg_Color;
in vec4 osg_MultiTexCoord0;
uniform mat4 osg_ModelViewProjectionMatrix;
out vec2 texCoord;
out vec4 vertexColor;
void main(void)
{
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
    texCoord = osg_MultiTexCoord0.xy;
    vertexColor = osg_Color; 
}
