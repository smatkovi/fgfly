/* ---- converted ---- */
#version 300 es
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
precision highp int;
uniform mat4 osg_ProjectionMatrix;
in vec4 osg_FrontColor;
in vec4 osg_BackColor;
out vec4 osg_FragColor;
in vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_specular;
uniform vec4 osg_LightSource0_position;
uniform vec4 osg_LightSource0_halfVector;
uniform vec4 osg_FrontMaterial_specular;
uniform float osg_FrontMaterial_shininess;
uniform vec4 osg_Fog_color;
uniform float osg_Fog_density;
// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/default.frag
// -*-C++-*-

// Ambient term comes in (gl_FrontFacing ? osg_FrontColor : osg_BackColor).rgb.
// #version 120

in vec4 diffuse_term;
in vec3 normal;
in vec2 orthoTexCoord;

uniform sampler2D osg_ru_texture;
uniform sampler2D orthophotoTexture;

////fog "include" /////
uniform int fogType;

uniform bool orthophotoAvailable;

vec3 fog_Func(vec3 color, int type);
//////////////////////

float luminance(vec3 color)
{
    return dot(vec3(0.212671, 0.715160, 0.072169), color);
}

void main()
{
    vec3 n;
    float NdotL, NdotHV, fogFactor;
    vec4 color = (gl_FrontFacing ? osg_FrontColor : osg_BackColor);
    vec3 lightDir = osg_LightSource0_position.xyz;
    vec3 halfVector = osg_LightSource0_halfVector.xyz;
    vec4 texel;
    vec4 fragColor;
    vec4 specular = vec4(0.0);

    // If (gl_FrontFacing ? osg_FrontColor : osg_BackColor).a == 0.0, this is a back-facing polygon and the
    // normal should be reversed.
    n = (2.0 * (gl_FrontFacing ? osg_FrontColor : osg_BackColor).a - 1.0) * normal;
    n = normalize(n);

    NdotL = dot(n, lightDir);
    if (NdotL > 0.0) {
        color += diffuse_term * NdotL;
        NdotHV = max(dot(n, halfVector), 0.0);
        if (osg_FrontMaterial_shininess > 0.0)
            specular.rgb = (osg_FrontMaterial_specular.rgb
                            * osg_LightSource0_specular.rgb
                            * pow(NdotHV, osg_FrontMaterial_shininess));
    }
    color.a = diffuse_term.a;
    // This shouldn't be necessary, but our lighting becomes very
    // saturated. Clamping the color before modulating by the osg_ru_texture
    // is closer to what the OpenGL fixed function pipeline does.
    color = clamp(color, 0.0, 1.0);
    texel = texture(osg_ru_texture, osg_TexCoord[0].st);
    
    if (orthophotoAvailable) {
        vec4 sat_texel = texture(orthophotoTexture, orthoTexCoord);
        if (sat_texel.a > 0.0) {
            texel.rgb = sat_texel.rgb;
        }
    }

    
    fragColor = color * texel + specular;

    fragColor.rgb = fog_Func(fragColor.rgb, fogType);
    osg_FragColor = fragColor;
}
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
/* ---- original ---- */
// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/default.frag
// -*-C++-*-

// Ambient term comes in gl_Color.rgb.
#version 120

varying vec4 diffuse_term;
varying vec3 normal;
varying vec2 orthoTexCoord;

uniform sampler2D texture;
uniform sampler2D orthophotoTexture;

////fog "include" /////
uniform int fogType;

uniform bool orthophotoAvailable;

vec3 fog_Func(vec3 color, int type);
//////////////////////

float luminance(vec3 color)
{
    return dot(vec3(0.212671, 0.715160, 0.072169), color);
}

void main()
{
    vec3 n;
    float NdotL, NdotHV, fogFactor;
    vec4 color = gl_Color;
    vec3 lightDir = gl_LightSource[0].position.xyz;
    vec3 halfVector = gl_LightSource[0].halfVector.xyz;
    vec4 texel;
    vec4 fragColor;
    vec4 specular = vec4(0.0);

    // If gl_Color.a == 0, this is a back-facing polygon and the
    // normal should be reversed.
    n = (2.0 * gl_Color.a - 1.0) * normal;
    n = normalize(n);

    NdotL = dot(n, lightDir);
    if (NdotL > 0.0) {
        color += diffuse_term * NdotL;
        NdotHV = max(dot(n, halfVector), 0.0);
        if (gl_FrontMaterial.shininess > 0.0)
            specular.rgb = (gl_FrontMaterial.specular.rgb
                            * gl_LightSource[0].specular.rgb
                            * pow(NdotHV, gl_FrontMaterial.shininess));
    }
    color.a = diffuse_term.a;
    // This shouldn't be necessary, but our lighting becomes very
    // saturated. Clamping the color before modulating by the texture
    // is closer to what the OpenGL fixed function pipeline does.
    color = clamp(color, 0.0, 1.0);
    texel = texture2D(texture, gl_TexCoord[0].st);
    
    if (orthophotoAvailable) {
        vec4 sat_texel = texture2D(orthophotoTexture, orthoTexCoord);
        if (sat_texel.a > 0) {
            texel.rgb = sat_texel.rgb;
        }
    }

    
    fragColor = color * texel + specular;

    fragColor.rgb = fog_Func(fragColor.rgb, fogType);
    gl_FragColor = fragColor;
}
// ---- merged shader object: /home/defaultuser/.local/share/harbour-fgview/fgdata/Compositor/Shaders/Default/include_fog.frag
#version 120

//#define fog_FuncTION
//varying vec3 PointPos;

vec3 fog_Func(vec3 color, int type)
{
	//if (type == 0){
		const float LOG2 = 1.442695;
		//float fogCoord =length(PointPos);
		float fogCoord = gl_ProjectionMatrix[3].z/(gl_FragCoord.z * -2.0 + 1.0 - gl_ProjectionMatrix[2].z);
		float fogFactor = exp2(-gl_Fog.density * gl_Fog.density * fogCoord * fogCoord * LOG2);

		if(gl_Fog.density == 1.0)
			fogFactor=1.0;

		return mix(gl_Fog.color.rgb, color, fogFactor);
}

