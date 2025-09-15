struct Input
{
    uint vertexIdx : SV_VertexID;
};

struct Output
{
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    float2 pos;
    if (input.vertexIdx == 0)
    {
        pos = (-1.0f).xx;
        output.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        if (input.vertexIdx == 1)
        {
            pos = float2(1.0f, -1.0f);
            output.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
        }
        else
        {
            if (input.vertexIdx == 2)
            {
                pos = float2(0.0f, 1.0f);
                output.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
            }
        }
    }
    output.position = float4(pos, 0.0f, 1.0f);
    return output;
}
