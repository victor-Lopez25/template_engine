struct Input
{
    uint vertexIdx : SV_VertexID;
    float2 position : TEXCOORD0;
};

struct Output
{
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 0.0f, 1.0f);
    if(input.vertexIdx == 0) {
        output.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    } else if(input.vertexIdx == 1) {
        output.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
    } else if (input.vertexIdx == 2) {
        output.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    return output;
}
