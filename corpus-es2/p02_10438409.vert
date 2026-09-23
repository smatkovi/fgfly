precision highp float;
precision highp int;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
attribute vec4 osg_Vertex;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
uniform mat4 osg_ModelViewProjectionMatrix;
varying vec4 fgfs_color;
varying vec2 fgfs_tc;
void osg_ffp_main() {
  gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
  fgfs_color = osg_Color;
  fgfs_tc = osg_MultiTexCoord0.st;
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}
