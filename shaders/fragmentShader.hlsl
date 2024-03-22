struct InputAttributes
{
    float4 clipPos : SV_Position;
    float3 colour : COLOR;
};

//[earlydepthstencil]
float4 main(InputAttributes IN) : SV_Target0
{
    //depth = IN.clipPos.z / IN.clipPos.w * 0.5 + 0.5; // This attempts to modify the depth value.
    return float4(IN.colour, 1.0f);
}