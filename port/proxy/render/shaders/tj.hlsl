
struct VSIn {
    float4 pos : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float invW : TEXCOORD2;
    float4 spec : COLOR1;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float invW : TEXCOORD2;
    float4 spec : COLOR1;
};

VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = v.pos;
    o.color = v.color;
    o.uv = v.uv;
    o.uv1 = v.uv1;
    o.invW = v.invW;
    o.spec = v.spec;
    return o;
}

cbuffer Params : register(b0) {
    uint op0;
    uint arg00;
    uint arg01;
    uint aop0;
    uint aarg00;
    uint aarg01;
    uint op1;
    uint arg10;
    uint arg11;
    uint aop1;
    uint aarg10;
    uint aarg11;
    uint flags;
    float alphaRef;
    float fogStart;
    float fogEnd;
    float4 factor;
    float4 fogColor;

    uint alphaFunc;
    uint stageCount;
};

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);

float4 pick(uint a, float4 diffuse, float4 current, float4 tex, float4 tf, float4 spec) {
    uint sel = a & 15;
    float4 v = diffuse;
    if (sel == 2) v = tex;
    else if (sel == 1) v = current;
    else if (sel == 3) v = tf;
    else if (sel == 4) v = spec;

    if (a & 32) v = v.aaaa;
    if (a & 16) v = 1.0 - v;
    return v;
}

float3 color_op(uint op, float3 c1, float3 c2, float a1) {
    if (op == 2) return c1;
    if (op == 3) return c2;
    if (op == 7) return c1 + c2;
    if (op == 10) return c1 - c2;
    if (op == 5) return c1 * c2 * 2.0;
    if (op == 18) return c1 + c2 * a1;
    return c1 * c2;
}

float alpha_op(uint op, float a1, float a2) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 7) return a1 + a2;
    if (op == 10) return a1 - a2;

    if (op == 5) return a1 * a2 * 2.0;
    if (op == 18) return a1;
    return a1 * a2;
}

float4 stage_apply(uint op, uint a0, uint a1, uint aop, uint aa0, uint aa1, float4 current,
                   float4 diffuse, float4 tex, float4 tf, float4 spec) {
    float4 x = pick(a0, diffuse, current, tex, tf, spec);
    float4 y = pick(a1, diffuse, current, tex, tf, spec);
    float4 xa = pick(aa0, diffuse, current, tex, tf, spec);
    float4 ya = pick(aa1, diffuse, current, tex, tf, spec);
    float3 c = color_op(op, x.rgb, y.rgb, x.a);
    float a = (aop == 1) ? current.a : alpha_op(aop, xa.a, ya.a);
    return saturate(float4(c, a));
}

struct PSIn {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    noperspective float invW : TEXCOORD2;
    float4 spec : COLOR1;
};

float4 PSMain(PSIn i) : SV_Target {
    float4 t0 = float4(1, 1, 1, 1);
    if (flags & 2) t0 = (flags & 128) ? tex0.Sample(samp1, i.uv) : tex0.Sample(samp0, i.uv);
    float4 t1 = float4(1, 1, 1, 1);
    if (flags & 4) t1 = (flags & 256) ? tex1.Sample(samp1, i.uv1) : tex1.Sample(samp0, i.uv1);
    float4 spec = i.spec;
    float4 c = i.color;

    if (stageCount > 0) c = stage_apply(op0, arg00, arg01, aop0, aarg00, aarg01, c, i.color, t0,
                                        factor, spec);
    if (stageCount > 1) c = stage_apply(op1, arg10, arg11, aop1, aarg10, aarg11, c, i.color, t1,
                                        factor, spec);
    if (flags & 8) {
        float eye = i.invW != 0.0 ? 1.0 / i.invW : 0.0;
        float f = fogEnd > fogStart ? saturate((fogEnd - eye) / (fogEnd - fogStart)) : 1.0;
        c.rgb = lerp(fogColor.rgb, c.rgb, f);
    }
    if (flags & 64) return float4(t0.rgb, 1);
    if (flags & 16) c.a = 1.0;
    if (flags & 1) {

        uint a8 = (uint)(saturate(c.a) * 255.0 + 0.5);
        uint r8 = (uint)(saturate(alphaRef) * 255.0 + 0.5);
        bool keep;
        if (alphaFunc == 1) keep = false;
        else if (alphaFunc == 2) keep = a8 < r8;
        else if (alphaFunc == 3) keep = a8 == r8;
        else if (alphaFunc == 4) keep = a8 <= r8;
        else if (alphaFunc == 5) keep = a8 > r8;
        else if (alphaFunc == 6) keep = a8 != r8;
        else if (alphaFunc == 7) keep = a8 >= r8;
        else keep = true;
        if (!keep) discard;
    }
    return c;
}
