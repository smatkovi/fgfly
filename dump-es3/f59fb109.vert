/* ---- converted ---- */
#version 300 es
precision highp float;
precision highp int;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;
out vec4 osg_FrontColor;
out vec4 osg_BackColor;
in vec4 osg_Vertex;
in vec3 osg_Normal;
in vec4 osg_Color;
in vec4 osg_MultiTexCoord0;
out vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_ambient;
uniform vec4 osg_FrontMaterial_emission;
uniform vec4 osg_LightModel_ambient;
// #version 120

out vec4 rawpos;
out vec4 ecPosition;
out vec3 VNormal;
out vec3 Normal;
out vec4 constantColor;

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

/* ---- original ---- */
#version 120

varying vec4 rawpos;
varying vec4 ecPosition;
varying vec3 VNormal;
varying vec3 Normal;
varying vec4 constantColor;

void main(void)
{
	gl_TexCoord[0]  = gl_MultiTexCoord0;

	rawpos = gl_Vertex;
	ecPosition = gl_ModelViewMatrix * gl_Vertex;
	VNormal = normalize(gl_NormalMatrix * gl_Normal);
	Normal = normalize(gl_Normal);
	
	gl_FrontColor = gl_Color;
	
	constantColor = gl_FrontMaterial.emission
		+ gl_Color * (gl_LightModel.ambient + gl_LightSource[0].ambient);
	
	gl_Position = ftransform();
}