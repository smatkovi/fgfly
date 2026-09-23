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
// #version 120

varying vec4 rawpos;
varying vec4 ecPosition;
varying vec3 VNormal;
varying vec3 Normal;
varying vec4 constantColor;

void osg_ffp_main()
{
	osg_TexCoord[0]  = osg_MultiTexCoord0;

	rawpos = osg_Vertex;
	ecPosition = osg_ModelViewMatrix * osg_Vertex;
	VNormal = normalize(osg_NormalMatrix * osg_Normal);
	Normal = normalize(osg_Normal);
	
	osg_FrontColor = osg_Color;
	
	constantColor = osg_FrontMaterial_emission
		+ osg_Color * (osg_LightModel_ambient + osg_LightSource0_ambient);
	
	gl_Position = (osg_ModelViewProjectionMatrix * osg_Vertex);
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    osg_ffp_main();
    osg_BackColor = osg_FrontColor;
}
