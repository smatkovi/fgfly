precision highp float;
precision highp int;
uniform mat4 osg_ModelViewMatrixInverse;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
attribute vec4 osg_Vertex;
attribute vec3 osg_Normal;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
varying vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_ambient;
uniform vec4 osg_LightSource0_diffuse;
uniform vec4 osg_FrontMaterial_emission;
uniform vec4 osg_FrontMaterial_ambient;
uniform vec4 osg_FrontMaterial_diffuse;
uniform vec4 osg_LightModel_ambient;
uniform mat4 osg_TextureMatrix0;
// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/include_fog.vert
// #version 120
//out float fogCoord;
varying vec3 PointPos;
//out vec4 EyePos;

void fog_Func(int type)
{
    PointPos = (osg_ModelViewMatrix * osg_Vertex).xyz;
    //PointPos = osg_Vertex;
    //EyePos = osg_ModelViewMatrixInverse * vec4(0.0,0.0,0.0,1.0);
		//fogCoord = abs(ecPosition.z);
}

// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/default.vert
// -*-C++-*-

// Shader that uses OpenGL state values to do per-pixel lighting
//
// The only light used is gl_LightSource[0], which is assumed to be
// directional.
//
// Diffuse colors come from the osg_Color, ambient from the material. This is
// equivalent to osg::Material::DIFFUSE.
// #version 120
#define MODE_OFF 0
#define MODE_DIFFUSE 1
#define MODE_AMBIENT_AND_DIFFUSE 2

attribute vec2 orthophotoTexCoord;

// The constant term of the lighting equation that doesn't depend on
// the surface normal is passed in gl_{Front,Back}Color. The alpha
// component is set to 1 for front, 0 for back in order to work around
// bugs with gl_FrontFacing in the fragment shader.
varying vec4 diffuse_term;
varying vec3 normal;
varying vec2 orthoTexCoord;

uniform int colorMode;

////fog "include"////////
//uniform int fogType;
//
//void fog_Func(int type);
/////////////////////////

void osg_ffp_main()
{
    gl_Position = (osg_ModelViewProjectionMatrix * osg_Vertex);
    osg_TexCoord[0] = osg_TextureMatrix0 * osg_MultiTexCoord0;
    orthoTexCoord = orthophotoTexCoord;
    normal = osg_NormalMatrix * osg_Normal;
    vec4 ambient_color, diffuse_color;
    if (colorMode == MODE_DIFFUSE) {
        diffuse_color = osg_Color;
        ambient_color = osg_FrontMaterial_ambient;
    } else if (colorMode == MODE_AMBIENT_AND_DIFFUSE) {
        diffuse_color = osg_Color;
        ambient_color = osg_Color;
    } else {
        diffuse_color = osg_FrontMaterial_diffuse;
        ambient_color = osg_FrontMaterial_ambient;
    }
    diffuse_term = diffuse_color * osg_LightSource0_diffuse;
    vec4 constant_term = osg_FrontMaterial_emission + ambient_color *
        (osg_LightModel_ambient +  osg_LightSource0_ambient);
    // Super hack: if diffuse material alpha is less than 1, assume a
    // transparency animation is at work
    if (osg_FrontMaterial_diffuse.a < 1.0)
        diffuse_term.a = osg_FrontMaterial_diffuse.a;
    else
        diffuse_term.a = osg_Color.a;
    // Another hack for supporting two-sided lighting without using
    // gl_FrontFacing in the fragment shader.
    osg_FrontColor.rgb = constant_term.rgb;  osg_FrontColor.a = 1.0;
    osg_BackColor.rgb = constant_term.rgb; osg_BackColor.a = 0.0;
    //fogCoord = abs(ecPosition.z / ecPosition.w);
		//fog_Func(fogType);
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
}
