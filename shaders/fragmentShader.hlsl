struct InputAttributes
{
    float4 clipPos : SV_Position;
    float3 colour : COLOR;
};

float4 main(InputAttributes IN) : SV_Target0
{
    return float4(IN.colour, 1.0f);
}