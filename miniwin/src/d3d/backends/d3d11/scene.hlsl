// Fixed-function emulation matching sdlgpu/shaders/src/scene.{vert,frag}.
cbuffer VertexUniforms : register(b0)
{
	float2 viewport;
	float2 vertexPad;
};
cbuffer FragmentUniforms : register(b1)
{
	int colorOp;
	int alphaOp;
	int specular;
	int alphaFunc;
	float alphaRef;
	int colorKey;
	float2 fragmentPad;
	float4 pointOriginStep;
	float2 pointCount;
	float2 pointPad;
};
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

struct VertexInput {
	float4 position : POSITION;
	float4 color : COLOR0;
	float4 specular : COLOR1;
	float2 uv : TEXCOORD0;
};
struct VertexOutput {
	float4 position : SV_Position;
	float4 color : COLOR0;
	float4 specular : COLOR1;
	float2 uv : TEXCOORD0;
};

VertexOutput vs_main(VertexInput input)
{
	VertexOutput output;
	float w = 1.0 / input.position.w;
	output.position = float4((input.position.x * 2.0 / viewport.x - 1.0) * w,
							 (1.0 - input.position.y * 2.0 / viewport.y) * w,
							 input.position.z * w, // Direct3D depth is 0..w, including at feature level 10.x.
							 w);
	output.color = input.color.bgra;
	output.specular = input.specular.bgra;
	output.uv = input.uv;
	return output;
}

float4 ps_main(VertexOutput input) : SV_Target
{
	if (pointCount.x < 0.0) {
		discard;
	}
	float4 sampled = float4(1.0, 1.0, 1.0, 1.0);
	if (colorOp != 2 || alphaOp != 2) {
		// Match the GL/SDL_GPU original-resolution point-sample lattice.
		float2 uv = input.uv;
		float2 dx = ddx(input.uv), dy = ddy(input.uv);
		if (pointCount.x > 0.0) {
			float2 pixel = floor((uv - pointOriginStep.xy) / pointOriginStep.zw + 0.5);
			uv = pointOriginStep.xy + clamp(pixel, float2(0.0, 0.0), pointCount - 1.0) * pointOriginStep.zw;
			dx = float2(pointOriginStep.z, 0.0);
			dy = float2(0.0, pointOriginStep.w);
		}
		sampled = sceneTexture.SampleGrad(sceneSampler, uv, dx, dy);
	}
	if (colorKey != 0 && sampled.a < 0.5) {
		discard;
	}
	float4 color;
	color.rgb = colorOp == 0 ? input.color.rgb * sampled.rgb : (colorOp == 1 ? sampled.rgb : input.color.rgb);
	color.a = alphaOp == 0 ? input.color.a * sampled.a : (alphaOp == 1 ? sampled.a : input.color.a);
	if (specular != 0) {
		color.rgb += input.specular.rgb;
	}
	bool passed = true;
	if (alphaFunc == 1) {
		passed = false;
	}
	else if (alphaFunc == 2) {
		passed = color.a < alphaRef;
	}
	else if (alphaFunc == 3) {
		passed = color.a == alphaRef;
	}
	else if (alphaFunc == 4) {
		passed = color.a <= alphaRef;
	}
	else if (alphaFunc == 5) {
		passed = color.a > alphaRef;
	}
	else if (alphaFunc == 6) {
		passed = color.a != alphaRef;
	}
	else if (alphaFunc == 7) {
		passed = color.a >= alphaRef;
	}
	if (!passed) {
		discard;
	}
	return color;
}
