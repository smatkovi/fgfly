precision highp float;
precision highp int;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
// #version 300 es
// gl3_VertexShader
#ifdef GL_ES
    precision highp float;
#endif
attribute vec4 osg_Vertex;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
uniform mat4 osg_ModelViewProjectionMatrix;
varying vec2 texCoord;
varying vec4 vertexColor;
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
