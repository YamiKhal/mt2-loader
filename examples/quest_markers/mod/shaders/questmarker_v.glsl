#version 330
// Quest Markers: the game's tint_v.glsl, with two changes. The marker turns to face the camera (around the
// upright axis only), and it's colored like the game's marker in the color the player picked (the instance
// color): models are painted in the game's marker colors, the picked color takes the yellow's place, and the
// orange becomes that color shaded the way the game shades its yellow. Pure black parts stay black, as with
// tint_v.glsl.
uniform bool fog;
uniform float fogDensity;
uniform mat4 worldToView;
uniform mat4 viewToProjection;
uniform vec3 cameraPosition;
in mat4 localToWorldAttrib;
in vec4 instanceColorAttrib;

out vec3 cameraToFragment;
in vec4 vertex;
in vec3 normal;
in vec4 color;

out vec4 frontColor;
out vec4 worldPosition;
out vec4 viewPosition;
out vec4 localPosition;
#ifdef LIT
out vec3 local_light_pos;
out vec3 local_normal;
out vec3 fragNormal;
#endif // LIT
out vec3 scale;

#ifdef TEXTURE
in vec2 texcoord;
out vec2 texcoord_out;
#endif

#ifdef LIT
struct lightSourceParameters
{
	vec4 ambient;
	vec4 diffuse;
	vec4 specular;
	vec3 position;
	vec3 halfVector;
};

uniform lightSourceParameters lightSource[4];
#endif // LIT

// The game's marker is painted yellow, blending into orange where it's shaded. The orange isn't just a darker
// yellow: its hue is 26.4 degrees warmer, it's 0.902 as saturated and 0.787 as bright. A picked color is shaded
// the same way, so the game's own yellow comes out exactly as the game draws it.
const vec3 markerYellow = vec3(0.992, 0.996, 0.047);
const vec3 markerOrange = vec3(0.784, 0.490, 0.110);

vec3 hsv_of(vec3 color)
{
	vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = mix(vec4(color.bg, k.wz), vec4(color.gb, k.xy), step(color.b, color.g));
	vec4 q = mix(vec4(p.xyw, color.r), vec4(color.r, p.yzx), step(p.x, color.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 rgb_of(vec3 hsv)
{
	vec3 p = abs(fract(hsv.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
	return hsv.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
}

vec3 shaded(vec3 color)
{
	vec3 hsv = hsv_of(color);
	return rgb_of(vec3(fract(hsv.x - 26.4 / 360.0), hsv.y * 0.902, hsv.z * 0.787));
}

// How far a vertex color is from the game's yellow toward its orange: 0 lit, 1 shaded.
float shading_of(vec3 color)
{
	vec3 along = markerOrange - markerYellow;
	return clamp(dot(color - markerYellow, along) / dot(along, along), 0.0, 1.0);
}

void main(void)
{
#ifdef TEXTURE
	texcoord_out = texcoord;
#endif // TEXTURE

	if ( max(max(color.r, color.g), color.b) < 0.02 )
		frontColor.rgb = color.rgb;
	else
		frontColor.rgb = mix(instanceColorAttrib.rgb, shaded(instanceColorAttrib.rgb), shading_of(color.rgb));

	frontColor.a = color.a;

	// Where the game puts the marker and how big, but turned toward the camera: the model's -z faces it, and its
	// +x is on the right of the screen.
	vec3 origin = localToWorldAttrib[3].xyz;
	float size = length(localToWorldAttrib[1].xyz);
	vec3 fromCamera = origin - cameraPosition;
	fromCamera.y = 0.0;
	vec3 away = length(fromCamera) > 0.0001 ? normalize(fromCamera) : vec3(0.0, 0.0, -1.0);
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = cross(up, away);

	localPosition = vertex;
	worldPosition = vec4(origin + (right * vertex.x + up * vertex.y + away * vertex.z) * size, 1.0);
	vec3 turnedNormal = right * normal.x + up * normal.y + away * normal.z;
	vec4 camToFragment = worldToView * worldPosition;
	cameraToFragment = camToFragment.xyz;

	gl_Position = viewToProjection * camToFragment;
	viewPosition = gl_Position;

	scale = vec3(size);

#ifdef LIT
	fragNormal = normalize(turnedNormal);
	local_light_pos = normalize(worldToView * vec4(lightSource[0].position.xyz, 0.0)).xyz;
	local_normal = (worldToView * vec4(fragNormal,0.0)).xyz;
#endif // LIT
}
