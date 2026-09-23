precision highp float;
precision highp int;
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
uniform vec4 osg_FrontMaterial_emission;
uniform vec4 osg_LightModel_ambient;
uniform mat4 osg_TextureMatrix0;
// -*- mode: C; -*-
// Licence: GPL v2
// Author: Frederic Bouvier
// #version 120

varying vec4  rawpos;
varying vec4  ecPosition;
varying vec3  VNormal;
varying vec3  Normal;
varying vec3  VTangent;
varying vec3  VBinormal;
varying vec4  constantColor;

attribute vec3 tangent, binormal;

////fog "include"////////
// uniform int fogType;
//
// void fog_Func(int type);
/////////////////////////

void osg_ffp_main()
{
    rawpos     = osg_Vertex;
    ecPosition = osg_ModelViewMatrix * osg_Vertex;
    VNormal = normalize(osg_NormalMatrix * osg_Normal);
    Normal = normalize(osg_Normal);
    VTangent  = osg_NormalMatrix * tangent;
    VBinormal = osg_NormalMatrix * binormal;
    osg_FrontColor = osg_Color;
    constantColor = osg_FrontMaterial_emission
        + osg_Color * (osg_LightModel_ambient + osg_LightSource0_ambient);
    gl_Position = (osg_ModelViewProjectionMatrix * osg_Vertex);
    osg_TexCoord[0] = osg_TextureMatrix0 * osg_MultiTexCoord0;
//     fog_Func(fogType);
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}
