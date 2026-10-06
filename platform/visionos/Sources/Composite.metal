#include <metal_stdlib>
using namespace metal;

// Shows an eye's image, which the game drew (port_spatial.c), over the room:
// what is the key color (PORT_KEY_*) is where the game drew nothing.

struct CompositeOut {
    float4 position [[position]];
    float2 uv;
};

struct CompositeUniforms {
    /// The key, as the game's 8-bit color.
    float3 key;
    /// The depth (reverse-Z, as the compositor's) to give what the game
    /// drew: the stage's, for the compositor's reprojection.
    float depth;
};

vertex CompositeOut composite_vertex(uint vid [[vertex_id]])
{
    // A triangle covering the view
    float2 p = float2((vid << 1) & 2, vid & 2);
    CompositeOut out;
    out.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
    out.uv = float2(p.x, 1.0 - p.y);
    return out;
}

struct CompositeFragment {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

static float3 srgb_to_linear(float3 c)
{
    return select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045);
}

fragment CompositeFragment composite_fragment(CompositeOut in [[stage_in]],
                                              texture2d<float> eye [[texture(0)]],
                                              constant CompositeUniforms& u [[buffer(0)]])
{
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 c = eye.sample(s, in.uv).rgb;

    // Exactly the key, give or take the 8 bits it was stored in: nothing
    // drawn. Edges blended into it keep their color, near the black the
    // game would have had behind them.
    if (all(abs(c - u.key) < 0.5 / 255.0)) {
        discard_fragment();
    }

    CompositeFragment out;
    out.color = float4(srgb_to_linear(c), 1.0);
    out.depth = u.depth;
    return out;
}
