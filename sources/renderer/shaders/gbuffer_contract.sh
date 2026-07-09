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

#endif // KB_GBUFFER_CONTRACT_SH
