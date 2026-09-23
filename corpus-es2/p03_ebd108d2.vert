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
uniform vec4 osg_LightSource0_position;
uniform vec4 osg_FrontMaterial_emission;
uniform vec4 osg_FrontMaterial_diffuse;
uniform vec4 osg_LightModel_ambient;
uniform mat4 osg_TextureMatrix0;
// -*- mode: C; -*-
// Licence: GPL v2
// � Emilian Huminiuc and Vivian Meazza 2011
// #version 120

varying vec3  rawpos;
varying float fogCoord;
varying vec3  VNormal;
varying vec3  VTangent;
varying vec3  VBinormal;
varying vec3  Normal;
varying vec3 vViewVec;
varying vec3 reflVec;

varying vec4 Diffuse;
varying float alpha;

uniform mat4 osg_ViewMatrixInverse;

attribute vec3 tangent;
attribute vec3 binormal;

////fog "include"////////
// uniform int fogType;
//
// void fog_Func(int type);
/////////////////////////

void osg_ffp_main()
{
    rawpos     = osg_Vertex.xyz / osg_Vertex.w;
    vec4 ecPosition = osg_ModelViewMatrix * osg_Vertex;
    ecPosition.xyz = ecPosition.xyz / ecPosition.w;
    //fogCoord = ecPosition.z;
    //fog_Func(fogType);

    vec3 n = normalize(osg_Normal);
    vec3 t = cross(osg_Normal, vec3(1.0,0.0,0.0));
    vec3 b = cross(n,t);

    VNormal = normalize(osg_NormalMatrix * osg_Normal);
    VTangent = normalize(osg_NormalMatrix * tangent);
    VBinormal = normalize(osg_NormalMatrix * binormal);
    Normal = normalize(osg_Normal);

    Diffuse = osg_Color * osg_LightSource0_diffuse;
    //Diffuse= osg_Color.rgb * max(0.0, dot(normalize(VNormal), osg_LightSource0_position.xyz));

    // Super hack: if diffuse material alpha is less than 1, assume a
    // transparency animation is at work
    if (osg_FrontMaterial_diffuse.a < 1.0)
        alpha = osg_FrontMaterial_diffuse.a;
    else
        alpha = osg_Color.a;

    // Vertex in eye coordinates
    vec3 vertVec = ecPosition.xyz;

    vViewVec.x = dot(t, vertVec);
    vViewVec.y = dot(b, vertVec);
    vViewVec.z = dot(n, vertVec);

    // calculate the reflection vector
    vec4 reflect_eye = vec4(reflect(vertVec, VNormal), 0.0);
    reflVec = normalize(osg_ModelViewMatrixInverse * reflect_eye).xyz;

    osg_FrontColor = osg_FrontMaterial_emission + osg_Color * (osg_LightModel_ambient + osg_LightSource0_ambient);

    gl_Position = (osg_ModelViewProjectionMatrix * osg_Vertex);
    osg_TexCoord[0] = osg_TextureMatrix0 * osg_MultiTexCoord0;
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}
