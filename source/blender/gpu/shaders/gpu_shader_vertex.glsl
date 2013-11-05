attribute vec4 weight;
attribute vec4 index;
attribute float numbones;

uniform bool useshwskin;
uniform  mat4 bonematrices[128];

varying vec3 varposition;
varying vec3 varnormal;

void hardware_skinning(in vec4 position, in vec3 normal, out vec4 transpos, out vec3 transnorm)
{
	transpos = vec4(0.0);
	transnorm = vec3(0.0);

	vec4 curidx = index;
	vec4 curweight = weight;

	for (int i = 0; i < int(numbones); ++i)
	{
		mat4 m44 = bonematrices[int(curidx.x)];

		transpos += m44 * position * curweight.x;

		mat3 m33 = mat3(m44[0].xyz,
		        m44[1].xyz,
		        m44[2].xyz);

		transnorm += m33 * normal * curweight.x;

		curidx = curidx.yzwx;
		curweight = curweight.yzwx;
	}
}

void main()
{

	vec4 invertex;
	vec3 innormal;

	if (useshwskin) {
		hardware_skinning(gl_Vertex, gl_Normal, invertex, innormal);
	}
	else {
		invertex = gl_Vertex;
		innormal = gl_Normal;
	}

	vec4 co = gl_ModelViewMatrix * invertex;

	varposition = co.xyz;
	varnormal = normalize(gl_NormalMatrix * innormal);
	gl_Position = gl_ProjectionMatrix  * co;

#ifdef GPU_NVIDIA
	// Setting gl_ClipVertex is necessary to get glClipPlane working on NVIDIA
	// graphic cards, while on ATI it can cause a software fallback.
	gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex; 
#endif 

