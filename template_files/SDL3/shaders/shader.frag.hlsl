struct Input
{
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

Texture2D<float4> texture : register(t0, space2);
SamplerState smp : register(s0, space2);

float4 main(Input input) : SV_Target0
{
    return texture.Sample(smp, input.uv) * input.color;
}