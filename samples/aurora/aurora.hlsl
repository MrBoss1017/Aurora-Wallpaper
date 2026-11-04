// Aurora Ribbon · self-contained Direct3D 11 pixel shader.
// See docs/WALLPAPER_FORMAT.md for the 32-byte uniform contract.
cbuffer Wallpaper : register(b0) {
    float2 resolution;
    float time;
    float deltaTime;
    float2 mouse;
    float2 padding;
};
float4 PSMain(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 p=(position.xy / max(resolution,float2(1,1)))*2.0-1.0;
    p.x*=resolution.x/max(1.0,resolution.y);
    float3 color=float3(0.018,0.027,0.065);
    float2 pointer=(mouse / max(resolution,float2(1,1)))*2.0-1.0;
    [unroll] for(int i=0;i<5;i++) {
        float f=(float)i;
        float wave=0.24*sin(p.x*1.65+time*0.17+f*0.45)
                  +0.13*sin(p.x*3.2-time*0.12+f*0.2)+f*0.11-0.25;
        wave+=0.04*sin(pointer.x+time*0.05);
        float ribbon=exp(-abs(p.y-wave)*(13.0+f*3.0));
        float3 tint=lerp(float3(0.11,0.65,0.53),float3(0.36,0.18,0.65),f/4.0);
        color+=ribbon*tint*0.28;
    }
    float vignette=saturate(1.15-dot(uv-0.5,uv-0.5)*1.8);
    return float4(pow(saturate(color*vignette),0.8),1);
}
