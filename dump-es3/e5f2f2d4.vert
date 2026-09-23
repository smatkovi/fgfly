/* ---- converted ---- */
#version 300 es
precision highp float;
precision highp int;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
out vec4 osg_FrontColor;
out vec4 osg_BackColor;
vec4 osg_ClipVertexDummy;
in vec4 osg_Vertex;
in vec4 osg_Color;
in vec4 osg_MultiTexCoord0;
// #version 300 es
precision highp float;

out vec2 texCoord;
out vec4 vertexColor;

void osg_ffp_main()
{
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
    texCoord = osg_MultiTexCoord0.xy;
    vertexColor = osg_Color;

#if !defined(GL_ES) && __VERSION__<140
    osg_ClipVertexDummy = osg_ModelViewMatrix * osg_Vertex;
#endif
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}

/* ---- original ---- */
#version 300 es
precision highp float;

out vec2 texCoord;
out vec4 vertexColor;

void main(void)
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;
    vertexColor = gl_Color;

#if !defined(GL_ES) && __VERSION__<140
    gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;
#endif
}

