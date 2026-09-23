#extension GL_OES_texture_3D : require

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
precision highp int;
precision mediump sampler3D;
uniform mat4 osg_ProjectionMatrix;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;

uniform vec4 osg_LightSource0_diffuse;
uniform vec4 osg_LightSource0_position;
uniform vec4 osg_Fog_color;
uniform float osg_Fog_density;
// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/include_fog.frag
// #version 120

//#define fog_FuncTION
//in vec3 PointPos;

vec3 fog_Func(vec3 color, int type)
{
	//if (type == 0){
		const float LOG2 = 1.442695;
		//float fogCoord =length(PointPos);
		float fogCoord = osg_ProjectionMatrix[3].z/(gl_FragCoord.z * -2.0 + 1.0 - osg_ProjectionMatrix[2].z);
		float fogFactor = exp2(-osg_Fog_density * osg_Fog_density * fogCoord * fogCoord * LOG2);

		if(osg_Fog_density == 1.0)
			fogFactor=1.0;

		return mix(osg_Fog_color.rgb, color, fogFactor);
}

// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/crop.frag
// #version 120

varying vec4  rawpos;
varying vec4  ecPosition;
varying vec3  VNormal;
varying vec3  Normal;

uniform sampler3D NoiseTex;
uniform sampler2D SampleTex;
uniform sampler2D ColorsTex;

varying vec4 constantColor;

uniform float snowlevel; // From /sim/rendering/snow-level-m

const float scale = 1.0;

#define BLA 1
#define BLA2 0

////fog "include" /////
uniform int fogType;

vec3 fog_Func(vec3 color, int type);
//////////////////////

void main (void)
{

	vec4 basecolor = texture2D(SampleTex, rawpos.xy*0.000144);
	basecolor = texture2D(ColorsTex, vec2( basecolor.r+0.00, 0.5));

	vec4 noisevec   = texture3D(NoiseTex, (rawpos.xyz)*0.01*scale);

	vec4 nvL   = texture3D(NoiseTex, (rawpos.xyz)*0.00066*scale);
	vec4 km = floor((rawpos)/1000.0);

	float fogFactor;
	float fogCoord = ecPosition.z;
	const float LOG2 = 1.442695;
	fogFactor = exp2(-osg_Fog_density * osg_Fog_density * fogCoord * fogCoord * LOG2);
//	float biasFactor = exp2(-0.00000002 * fogCoord * fogCoord * LOG2);
	float biasFactor = fogFactor = clamp(fogFactor, 0.0, 1.0);

	float n=0.06;
	n += nvL[0]*0.4;
	n += nvL[1]*0.6;
	n += nvL[2]*2.0;
	n += nvL[3]*4.0;
	n += noisevec[0]*0.1;
	n += noisevec[1]*0.4;

	n += noisevec[2]*0.8;
	n += noisevec[3]*2.1;
	n = mix(0.6, n, biasFactor);

	// good
	vec4 c1;
	c1 = basecolor * vec4(smoothstep(0.0, 1.15, n), smoothstep(0.0, 1.2, n), smoothstep(0.1, 1.3, n), 1.0);

	//"steep = gray"
	c1 = mix(vec4(n-0.42, n-0.44, n-0.51, 1.0), c1, smoothstep(0.970, 0.990, abs(normalize(Normal).z)+nvL[2]*1.3));

	//"snow"
	c1 = mix(c1, clamp(n+nvL[2]*4.1+vec4(0.1, 0.1, nvL[2]*2.2, 1.0), 0.7, 1.0), smoothstep(snowlevel+300.0, snowlevel+360.0, (rawpos.z)+nvL[1]*3000.0));

    vec3 diffuse = (gl_FrontFacing ? osg_FrontColor : osg_BackColor).rgb * max(0.0, dot(VNormal, osg_LightSource0_position.xyz));
    vec4 ambient_light = constantColor + osg_LightSource0_diffuse * vec4(diffuse, 1.0);

	c1 *= ambient_light;
	vec4 finalColor = c1;

// 	if(osg_Fog_density == 1.0)
// 		fogFactor=1.0;
//
// 	gl_FragColor = mix(osg_Fog_color ,finalColor, fogFactor);
	finalColor.rgb = fog_Func(finalColor.rgb, fogType);
	gl_FragColor = finalColor;
}
