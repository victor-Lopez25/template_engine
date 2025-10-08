struct Input
{
    uint vertexIdx : SV_VertexID;
    float2 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct Output
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 0.0f, 1.0f);
    if((input.vertexIdx % 3) == 0) {
        output.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    } else if((input.vertexIdx % 3) == 1) {
        output.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
    } else if ((input.vertexIdx % 3) == 2) {
        output.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    output.uv = input.uv;
    return output;
}
