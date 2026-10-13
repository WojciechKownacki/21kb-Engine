#ifndef KB_GBUFFER_CONTRACT_SH
#define KB_GBUFFER_CONTRACT_SH

#define KB_GBUFFER_SHADING_MODEL_UNLIT 0.0
#define KB_GBUFFER_SHADING_MODEL_DEFAULT_LIT 1.0
#define KB_GBUFFER_SHADING_MODEL_SUBSURFACE 2.0
#define KB_GBUFFER_SHADING_MODEL_CLEAR_COAT 3.0
#define KB_GBUFFER_SHADING_MODEL_CLOTH 4.0
#define KB_GBUFFER_SHADING_MODEL_HAIR 5.0
#define KB_GBUFFER_SHADING_MODEL_EYE 6.0
#define KB_GBUFFER_SHADING_MODEL_SINGLE_LAYER_WATER 7.0
#define KB_GBUFFER_SHADING_MODEL_THIN_TRANSLUCENT 8.0

float KbEncodeGBufferShadingModel(float shadingModel)
{
    return clamp(shadingModel, 0.0, 255.0) * (1.0 / 255.0);
}

float KbDecodeGBufferShadingModel(float encodedShadingModel)
{
    return floor(clamp(encodedShadingModel, 0.0, 1.0) * 255.0 + 0.5);
}

// The shading model's two parameters (Unreal CustomData0/1, shading_models.sh): CustomData0 is the
// albedo target's alpha; CustomData1 rides in the normal target's alpha as 1 + value, which keeps the
// alpha 0 of the cleared background (the "nothing drawn here" test of every pass) apart.
float KbEncodeGBufferCustomData1(float value)
{
    return 1.0 + clamp(value, 0.0, 1.0);
}

float KbDecodeGBufferCustomData1(float encodedNormalAlpha)
{
    return clamp(encodedNormalAlpha - 1.0, 0.0, 1.0);
}

#endif // KB_GBUFFER_CONTRACT_SH
