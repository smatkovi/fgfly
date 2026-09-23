#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
precision highp int;

precision mediump float;
uniform sampler2D fgfs_tex;
varying vec4 fgfs_color;
varying vec2 fgfs_tc;
void main() { gl_FragColor = fgfs_color * texture2D(fgfs_tex, fgfs_tc); }
