struct InputAttributes {
	float2 ndcPos : POSITION;
};
struct OutputAttributes{
    float4 clipPos: SV_Position;
};

OutputAttributes main(InputAttributes IN)
{
    OutputAttributes OUT;
    OUT.clipPos = float4(IN.ndcPos, 0.0f, 1.0f);
    return OUT;
}