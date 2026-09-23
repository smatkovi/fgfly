precision highp float;
precision highp int;
uniform mat4 osg_ModelViewMatrixInverse;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
vec4 osg_ClipVertexDummy;
attribute vec4 osg_Vertex;
attribute vec3 osg_Normal;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
varying vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_ambient;
uniform vec4 osg_FrontMaterial_emission;
uniform vec4 osg_FrontMaterial_diffuse;
uniform vec4 osg_LightModel_ambient;
uniform mat4 osg_TextureMatrix0;
// -*- mode: C; -*-
// UBERSHADER - vertex shader
// Licence: GPL v2
// © Emilian Huminiuc and Vivian Meazza 2011
// #version 120

varying vec4	diffuseColor;
varying vec3	VBinormal;
varying vec3	VNormal;
varying vec3	VTangent;
varying vec3	rawpos;
varying vec3 	eyeVec;
varying vec3	eyeDir;

attribute vec3	tangent;
attribute vec3	binormal;

uniform int  		nmap_enabled;
uniform int			rembrandt_enabled;

void osg_ffp_main()
{
		rawpos = osg_Vertex.xyz;
		vec4 ecPosition = osg_ModelViewMatrix * osg_Vertex;
		eyeVec = ecPosition.xyz;
		eyeDir = osg_ModelViewMatrixInverse[3].xyz - osg_Vertex.xyz;

		VNormal = normalize(osg_NormalMatrix * osg_Normal);

		vec3 n = normalize(osg_Normal);

// 		generate "fake" binormals/tangents
		vec3 c1 = cross(n, vec3(0.0,0.0,1.0));
		vec3 c2 = cross(n, vec3(0.0,1.0,0.0));
		vec3 tempTangent = c1;

		if(length(c2)>length(c1)){
			tempTangent = c2;
		}

		vec3 tempBinormal = cross(n, tempTangent);

		if (nmap_enabled > 0){
			tempTangent = tangent;
			tempBinormal  = binormal;
		}

		VTangent = normalize(osg_NormalMatrix * tempTangent);
		VBinormal = normalize(osg_NormalMatrix * tempBinormal);

		diffuseColor = osg_Color;
    // Super hack: if diffuse material alpha is less than 1, assume a
	// transparency animation is at work
		if (osg_FrontMaterial_diffuse.a < 1.0)
			diffuseColor.a = osg_FrontMaterial_diffuse.a;

		if(rembrandt_enabled < 1){
		osg_FrontColor = osg_FrontMaterial_emission + osg_Color
					  * (osg_LightModel_ambient + osg_LightSource0_ambient);
		} else {
		  osg_FrontColor = osg_Color;
		}
		
		gl_Position = (osg_ModelViewProjectionMatrix * osg_Vertex);
		osg_ClipVertexDummy = ecPosition;
		osg_TexCoord[0] = osg_TextureMatrix0 * osg_MultiTexCoord0;
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}
