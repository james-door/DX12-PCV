struct InputAttributes {
    float3 modelPos : POSITION;
    float3 colour : COLOR;
};
struct OutputAttributes{
    float4 clipPos: SV_Position;
    float3 colour : COLOR;
};

struct Transform
{
    matrix mat;

};
ConstantBuffer<Transform> TransformCB : register(b0);

OutputAttributes main(InputAttributes IN)
{
    OutputAttributes OUT;
    OUT.clipPos = mul(TransformCB.mat,float4(IN.modelPos, 1.0f));
    OUT.colour = IN.colour;
   
    return OUT;
}