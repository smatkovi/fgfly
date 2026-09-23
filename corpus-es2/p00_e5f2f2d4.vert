precision highp float;
precision highp int;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
vec4 osg_ClipVertexDummy;
attribute vec4 osg_Vertex;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
// #version 300 es
precision highp float;

varying vec2 texCoord;
varying vec4 vertexColor;

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
