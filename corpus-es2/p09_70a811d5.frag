#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
precision highp int;

varying vec4 osg_TexCoord[1];
uniform vec4 osg_Fog_color;
// #version 120

uniform sampler2D baseTexture;
varying float fogFactor;
varying vec4  cloudColor;

void main(void)
{
      vec4 base = texture2D(baseTexture, osg_TexCoord[0].st);
      if (base.a < 0.02)
        discard;

      vec4 finalColor = base * cloudColor;
      gl_FragColor.rgb = mix(osg_Fog_color.rgb, finalColor.rgb, fogFactor );
      gl_FragColor.a = finalColor.a;
}
