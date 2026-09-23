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

varying vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_ambient;
uniform vec4 osg_LightSource0_diffuse;
uniform vec4 osg_LightSource0_specular;
uniform vec4 osg_LightSource0_position;
uniform vec4 osg_LightSource0_halfVector;
uniform vec4 osg_FrontMaterial_diffuse;
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

// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/runway.frag
// -*- mode: C; -*-
// Licence: GPL v2
// � Emilian Huminiuc and Vivian Meazza 2011

// #version 120

varying vec3  rawpos;
varying vec3  VNormal;
varying vec3  VTangent;
varying vec3  VBinormal;
varying vec3  vViewVec;
varying vec3  reflVec;

varying vec4 Diffuse;
varying float alpha;
//in float fogCoord;

uniform samplerCube Environment;
uniform sampler2D Rainbow;
uniform sampler2D BaseTex;
uniform sampler2D Fresnel;
uniform sampler2D Map;
uniform sampler2D NormalTex;
uniform sampler3D Noise;

uniform float spec_adjust;
uniform float rainbowiness;
uniform float fresneliness;
uniform float noisiness;
uniform float ambient_correction;
uniform float normalmap_dds;

//uniform int fogType;

////fog "include" /////
uniform int fogType;

vec3 fog_Func(vec3 color, int type);
//////////////////////

void main (void)
{
    //vec3 halfV;
    //float NdotL, NdotHV;

    vec4 texel = texture2D(BaseTex, osg_TexCoord[0].st);
    vec4 nmap  = texture2D(NormalTex, osg_TexCoord[0].st * 8.0);
    vec4 map   = texture2D(Map, osg_TexCoord[0].st * 8.0);
    vec4 specNoise = texture3D(Noise, rawpos.xyz * 0.0045);
    vec4 noisevec = texture3D(Noise, rawpos.xyz);

    vec3 lightDir = osg_LightSource0_position.xyz;
    vec3 halfVector = osg_LightSource0_halfVector.xyz;
    vec3 N;
    float pf;

    N = nmap.rgb * 2.0 - 1.0;
    N = normalize(N.x * VTangent + N.y * VBinormal + N.z * VNormal);
    if (normalmap_dds > 0.0)
        N = -N;

	float lightness = dot(texel.rgb, vec3( 0.3, 0.59, 0.11 ));
    // calculate the specular light
    float refl_correction = spec_adjust * 2.5 - 1.0;
    float shininess = max (0.35, refl_correction);
    float nDotVP = max(0.0, dot(N, normalize(osg_LightSource0_position.xyz)));
    float nDotHV = max(0.0, dot(N, normalize(osg_LightSource0_halfVector.xyz)));

    if (nDotVP == 0.0)
        pf = 0.0;
    else
        pf = pow(nDotHV, /*gl_FrontMaterial.*/shininess);

    vec4 Diffuse  = osg_LightSource0_diffuse * nDotVP;
    //vec4 Specular = vec4(vec3(0.5*shininess), 1.0)* osg_LightSource0_specular * pf;
	vec4 Specular = vec4(1.0)* lightness * osg_LightSource0_specular * pf;

    vec4 color = (gl_FrontFacing ? osg_FrontColor : osg_BackColor) + Diffuse * osg_FrontMaterial_diffuse;
    //color += Specular * vec4(vec3(0.5*shininess), 1.0) * nmap.a;
	float nFactor = 1.0 - N.z;
	color += Specular * vec4(1.0) * nmap.a * nFactor;
    color.a = texel.a * alpha;
    color = clamp(color, 0.0, 1.0);

    vec3 viewVec = normalize(vViewVec);

    // Map a rainbowish color
    float v = abs(dot(viewVec, normalize(VNormal)));
    vec4 rainbow = texture2D(Rainbow, vec2(v, 0.0));

    // Map a fresnel effect
    vec4 fresnel = texture2D(Fresnel, vec2(v, 0.0));

    // map the refection of the environment
    vec4 reflection = textureCube(Environment, reflVec * dot(N,VNormal));


    // set the user shininess offset
    float transparency_offset = clamp(refl_correction, -1.0, 1.0);
    float reflFactor = 0.0;

    float MixFactor = specNoise.r * specNoise.g * specNoise.b * 350.0;

    MixFactor = 0.75 * smoothstep(0.0, 1.0, MixFactor);

    reflFactor = max(map.a * (texel.r + texel.g), 1.0 - MixFactor)  * (1.0- N.z)  + transparency_offset ;

    reflFactor =0.75 * smoothstep(0.05, 1.0, reflFactor);

    // set ambient adjustment to remove bluiness with user input
    float ambient_offset = clamp(ambient_correction, -1.0, 1.0);
    vec4 ambient_Correction = vec4(osg_LightSource0_ambient.rg, osg_LightSource0_ambient.b * 0.6, 0.5) * ambient_offset ;
    ambient_Correction = clamp(ambient_Correction, -1.0, 1.0);

    // add fringing fresnel and rainbow effects and modulate by reflection
    vec4 reflcolor = mix(reflection, rainbow, rainbowiness * v);
    reflcolor += Specular * nmap.a * nFactor;
    vec4 reflfrescolor = mix(reflcolor, fresnel, fresneliness * v);
    vec4 noisecolor = mix(reflfrescolor, noisevec, noisiness);
    vec4 raincolor = vec4(noisecolor.rgb * reflFactor, 1.0);
    raincolor += Specular * nmap.a * nFactor;


	vec4 mixedcolor = mix(texel, raincolor * (1.0 - refl_correction * (1.0 - lightness)), reflFactor);  //* (1.0 - 0.5 * transparency_offset )

    // the final reflection
    vec4 fragColor = vec4(color.rgb * mixedcolor.rgb  + ambient_Correction.rgb * (1.0 - refl_correction * (1.0 - 0.8 * lightness)) * nFactor, color.a);
	fragColor += Specular * nmap.a * nFactor;

    fragColor.rgb = fog_Func(fragColor.rgb, fogType);
    gl_FragColor = fragColor;
}
