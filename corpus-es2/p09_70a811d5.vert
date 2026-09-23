precision highp float;
precision highp int;
uniform mat4 osg_ModelViewMatrixInverse;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
varying vec4 osg_FrontColor;
varying vec4 osg_BackColor;
attribute vec4 osg_Vertex;
attribute vec4 osg_Color;
attribute vec4 osg_MultiTexCoord0;
varying vec4 osg_TexCoord[1];
uniform vec4 osg_LightSource0_diffuse;
uniform vec4 osg_LightSource0_position;
uniform vec4 osg_FrontLightModelProduct_sceneColor;
uniform float osg_Fog_density;
// -*-C++-*-
// #version 120

varying float fogFactor;
varying vec4  cloudColor;

uniform float range; // From /sim/rendering/clouds3d-vis-range
uniform float detail_range; // From /sim/rendering/clouds3d_detail-range

attribute vec3 usrAttr1;
attribute vec3 usrAttr2;

float shade_factor;
float cloud_height;
float bottom_factor;
float middle_factor;
float top_factor;

void osg_ffp_main()
{
  osg_TexCoord[0] = osg_MultiTexCoord0;
  vec4 ep = osg_ModelViewMatrixInverse * vec4(0.0,0.0,0.0,1.0);
  vec4 l  = osg_ModelViewMatrixInverse * vec4(0.0,0.0,1.0,1.0);
  vec3 u = normalize(ep.xyz - l.xyz);

  // Find a rotation matrix that rotates 1,0,0 into u. u, r and w are
  // the columns of that matrix.
  vec3 absu = abs(u);
  vec3 r = normalize(vec3(-u.y, u.x, 0.0));
  vec3 w = cross(u, r);

  // Do the matrix multiplication by [ u r w pos]. Assume no
  // scaling in the homogeneous component of pos.
  gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
  gl_Position.xyz = osg_Vertex.x * u;
  gl_Position.xyz += osg_Vertex.y * r;
  gl_Position.xyz += osg_Vertex.z * w;
  // Apply Z scaling to allow sprites to be squashed in the z-axis
  gl_Position.z = gl_Position.z * osg_Color.w;

  // Now shift the sprite to the correct position in the cloud.
  gl_Position.xyz += osg_Color.xyz;

  // Determine the position - used for fog and shading calculations
  float fogCoord = length(vec3(osg_ModelViewMatrix * vec4(osg_Color.x, osg_Color.y, osg_Color.z, 1.0)));
  float center_dist = length(vec3(osg_ModelViewMatrix * vec4(0.0,0.0,0.0,1.0)));
  
  if ((fogCoord > detail_range) && (fogCoord > center_dist) && (shade_factor < 0.7)) {
    // More than detail_range away, so discard all sprites on opposite side of
    // cloud center by shifting them beyond the view fustrum
    gl_Position = vec4(0.0,0.0,10.0,1.0);
    cloudColor = vec4(0.0);
  } else {
    // Determine a lighting normal based on the vertex position from the
    // center of the cloud, so that sprite on the opposite side of the cloud to the sun are darker.
    float n = dot(normalize(-osg_LightSource0_position.xyz),
                  normalize(vec3(osg_ModelViewMatrix * vec4(- gl_Position.x, - gl_Position.y, - gl_Position.z, 0.0))));

    // Determine the shading of the vertex. We shade it based on it's position
    // in the cloud relative to the sun, and it's vertical position in the cloud.
    float shade = mix(shade_factor, top_factor,  smoothstep(-0.3, 0.3, n));
    //if (n < 0.0) {
    //  shade = mix(top_factor, shade_factor, abs(n));
    //} 
    
    if (gl_Position.z < 0.5 * cloud_height) {
      shade = min(shade, mix(bottom_factor, middle_factor, gl_Position.z * 2.0 / cloud_height));
    } else {
      shade = min(shade, mix(middle_factor, top_factor, gl_Position.z * 2.0 / cloud_height - 1.0));
    }
                  
    // Final position of the sprite
    gl_Position = osg_ModelViewProjectionMatrix * gl_Position;    
    cloudColor = osg_LightSource0_diffuse * shade + osg_FrontLightModelProduct_sceneColor;
    
    if ((fogCoord > (0.9 * detail_range)) && (fogCoord > center_dist) && (shade_factor < 0.7)) {
      // cloudlet is almost at the detail range, so fade it out.
      cloudColor.a = 1.0 - smoothstep(0.9 * detail_range, detail_range, fogCoord);
    } else {
      // As we get within 100m of the sprite, it is faded out. Equally at large distances it also fades out.
      cloudColor.a = min(smoothstep(10.0, 100.0, fogCoord), 1.0 - smoothstep(0.9 * range, range, fogCoord));
    }
    
    //osg_BackColor = cloudColor;

    // Fog doesn't affect clouds as much as other objects.
    fogFactor = exp( -osg_Fog_density * fogCoord * 0.5);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
  }
}
void main() {
    osg_FrontColor = vec4(1.0);
    osg_BackColor = vec4(1.0);
    shade_factor = usrAttr1.g;
    cloud_height = usrAttr1.b;
    bottom_factor = usrAttr2.r;
    middle_factor = usrAttr2.g;
    top_factor = usrAttr2.b;
    osg_ffp_main();
}
