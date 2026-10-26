// No vertex buffers, descriptors, constants, or input layout: only shader execution.
float4 vs_main(uint id : SV_VertexID) : SV_Position
{
	float2 position = float2((id << 1) & 2, id & 2);
	return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

float4 ps_main() : SV_Target0
{
#if PROBE_DXIL
	return float4(0.0, 1.0, 0.0, 1.0);
#else
	return float4(1.0, 0.0, 0.0, 1.0);
#endif
}
