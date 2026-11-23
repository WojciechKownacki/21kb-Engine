#include "kb/render/resources/RenderMaterialGraphNodeDocs.hpp"

#include <algorithm>
#include <array>

namespace kb::render {
namespace {

using Kind = RenderMaterialGraphNodeKind;

constexpr RenderMaterialGraphNodeDoc kDocs[]{
    // Output
    { Kind::MaterialOutput, "Utility", "The material's result: connect colour, roughness, emissive, alpha and the other surface channels to its inputs. The graph has exactly one.", "" },

    // Constants
    { Kind::ConstantScalar, "Constant", "One fixed number.", "number, e.g. 0.5" },
    { Kind::ConstantVector2, "Constant", "A fixed pair of numbers (a UV offset, a 2D size).", "x y" },
    { Kind::ConstantVector, "Constant", "A fixed vector of three numbers (a direction, a position).", "x y z" },
    { Kind::ConstantColor, "Constant", "A fixed colour with alpha.", "r g b a (linear), or #RRGGBB" },
    { Kind::ConstantBool, "Constant", "A fixed true or false for If and the static switches.", "true | false" },

    // Parameters
    { Kind::ParameterScalar, "Parameter", "A number that instances and scripts can change without a new graph.", "default number" },
    { Kind::ParameterVector, "Parameter", "A vector of three numbers an instance can change.", "x y z" },
    { Kind::ParameterColor, "Parameter", "A colour an instance can change.", "#RRGGBB or r g b a (linear)" },
    { Kind::ParameterTexture, "Parameter", "A texture slot an instance can fill with another image; connect it to a Texture Sample.", "texture path or none" },
    { Kind::CollectionParameter, "Parameter", "A value shared by every material from a parameter collection asset (global wetness, time of day).", "" },
    { Kind::StaticBoolParameter, "Parameter", "A true or false chosen when the shader is built; a Static Switch keeps only one branch.", "true | false" },
    { Kind::DynamicParameter, "Parameter", "Four values a particle emitter or script feeds each frame (a dynamic parameter).", "" },

    // Textures
    { Kind::TextureSample, "Texture", "Reads an image at a UV coordinate; costs one texture sample.", "texture path of the project" },
    { Kind::TextureObject, "Texture", "A texture handed to nodes that sample it themselves (Parallax Occlusion Mapping, World Aligned Texture).", "texture path of the project" },
    { Kind::TextureSampleCube, "Texture", "Reads a cube map along a direction (sky or reflection look).", "cube texture path" },
    { Kind::TextureObjectCube, "Texture", "A cube map handed to nodes that sample it themselves.", "cube texture path" },
    { Kind::TextureSampleVolume, "Texture", "Reads a 3D texture at a UVW position.", "3D texture path" },
    { Kind::TextureObjectVolume, "Texture", "A 3D texture handed to nodes that sample it themselves.", "3D texture path" },
    { Kind::TextureSample2DArray, "Texture", "Reads one layer of a 2D array texture.", "array texture path" },
    { Kind::TextureObject2DArray, "Texture", "A 2D array texture handed to nodes that sample it themselves.", "array texture path" },
    { Kind::WorldAlignedTexture, "Texture", "Projects a texture along the world axes so it tiles across objects without stretching.", "" },
    { Kind::ParallaxOcclusionMapping, "Texture", "Fake depth from a height map: shifts the UVs along the view ray. Expensive; avoid on phones.", "height channel 0-3 (0 = red)" },
    { Kind::AntialiasedTextureMask, "Texture", "Turns a 0..1 value into a crisp 0/1 mask with a one-pixel soft edge.", "threshold, default 0.5" },
    { Kind::FlipBook, "Texture", "Picks the cell of a sprite sheet for an animation phase and returns the UV inside that cell.", "" },

    // Math
    { Kind::Add, "Math", "A plus B per channel.", "" },
    { Kind::Subtract, "Math", "A minus B per channel.", "" },
    { Kind::Multiply, "Math", "A times B per channel (tint a colour, scale a value).", "" },
    { Kind::Divide, "Math", "A divided by B per channel.", "" },
    { Kind::Power, "Math", "Base to the power of Exponent (sharpen a mask or a gradient).", "" },
    { Kind::OneMinus, "Math", "1 minus the value (invert a mask).", "" },
    { Kind::Negate, "Math", "The value with its sign flipped.", "" },
    { Kind::Absolute, "Math", "The value without its sign.", "" },
    { Kind::Sign, "Math", "-1, 0 or 1 by the sign of the value.", "" },
    { Kind::Minimum, "Math", "The smaller of A and B per channel.", "" },
    { Kind::Maximum, "Math", "The larger of A and B per channel.", "" },
    { Kind::Clamp, "Math", "Keeps the value between Min and Max.", "" },
    { Kind::Saturate, "Math", "Keeps the value between 0 and 1.", "" },
    { Kind::Floor, "Math", "Rounds down to a whole number.", "" },
    { Kind::Ceil, "Math", "Rounds up to a whole number.", "" },
    { Kind::Round, "Math", "Rounds to the nearest whole number.", "" },
    { Kind::Truncate, "Math", "Drops the fraction, towards zero.", "" },
    { Kind::Fraction, "Math", "The part after the point; repeats a ramp 0..1.", "" },
    { Kind::Fmod, "Math", "The remainder of A divided by B.", "" },
    { Kind::SquareRoot, "Math", "Square root of the value.", "" },
    { Kind::Exponential, "Math", "e to the power of the value.", "" },
    { Kind::Exponential2, "Math", "2 to the power of the value.", "" },
    { Kind::Logarithm, "Math", "Natural logarithm of the value.", "" },
    { Kind::Logarithm2, "Math", "Base-2 logarithm of the value.", "" },
    { Kind::Logarithm10, "Math", "Base-10 logarithm of the value.", "" },
    { Kind::Sine, "Math", "Sine of the value in radians; with Time it pulses or waves.", "" },
    { Kind::Cosine, "Math", "Cosine of the value in radians.", "" },
    { Kind::Tangent, "Math", "Tangent of the value in radians.", "" },
    { Kind::ArcSine, "Math", "Inverse sine, in radians.", "" },
    { Kind::ArcCosine, "Math", "Inverse cosine, in radians.", "" },
    { Kind::ArcTangent, "Math", "Inverse tangent, in radians.", "" },
    { Kind::ArcTangent2, "Math", "The angle of the point (X, Y), in radians.", "" },
    { Kind::ArcSineFast, "Math", "Cheaper, less exact inverse sine.", "" },
    { Kind::ArcCosineFast, "Math", "Cheaper, less exact inverse cosine.", "" },
    { Kind::ArcTangentFast, "Math", "Cheaper, less exact inverse tangent.", "" },
    { Kind::ArcTangent2Fast, "Math", "Cheaper, less exact angle of the point (X, Y).", "" },
    { Kind::Lerp, "Math", "Blends A to B by T (0 gives A, 1 gives B); the workhorse for mixing two colours with a mask.", "" },
    { Kind::InverseLerp, "Math", "Where Value lies between A and B, as 0..1.", "" },
    { Kind::Step, "Math", "0 below Edge, 1 at or above it: a hard threshold.", "" },
    { Kind::SmoothStep, "Math", "A soft 0..1 ramp between Min and Max.", "" },
    { Kind::RemapValueRange, "Math", "Maps Input from one range to another.", "" },
    { Kind::ConstantBiasScale, "Math", "(Input + bias) * scale on every channel.", "bias scale" },
    { Kind::If, "Math", "Chooses Less, Equal or Greater by comparing A and B.", "constA constB equalsThreshold" },
    { Kind::PartialDerivativeX, "Math", "How fast the value changes from pixel to pixel in X on screen.", "" },
    { Kind::PartialDerivativeY, "Math", "How fast the value changes from pixel to pixel in Y on screen.", "" },
    { Kind::Sobol, "Math", "A low-discrepancy random number by cell and index, for dithering and sampling.", "" },

    // Vectors
    { Kind::DotProduct, "Vector", "Dot product of two vectors (how aligned they are).", "" },
    { Kind::CrossProduct, "Vector", "Cross product of two vectors (a vector perpendicular to both).", "" },
    { Kind::Normalize, "Vector", "The vector scaled to length 1.", "" },
    { Kind::Length, "Vector", "The length of a vector.", "" },
    { Kind::Distance, "Vector", "The distance between two points.", "" },
    { Kind::BreakVector, "Vector", "Splits a vector into its X, Y, Z and W numbers.", "" },
    { Kind::MakeVector, "Vector", "Builds a vector from X, Y, Z and W numbers.", "" },
    { Kind::AppendVector, "Vector", "Adds the channels of B after those of A (a 2-vector plus a number makes a 3-vector).", "" },
    { Kind::StaticComponentMask, "Vector", "Keeps only the chosen channels of a vector, the rest become 0.", "channels kept, e.g. rg or xyz" },
    { Kind::Transform, "Vector", "Turns a direction from one space to another.", "from to, each tangent | world | view" },
    { Kind::TransformPosition, "Vector", "Turns a point from one space to another.", "from to, each tangent | world | view" },
    { Kind::RotateAboutAxis, "Vector", "Rotates a position about an axis; the base of bending and swaying.", "axisX axisY axisZ angle" },
    { Kind::SphereMask, "Vector", "A soft round mask of the distance between points A and B.", "radius hardness" },
    { Kind::DeriveNormalZ, "Vector", "Computes the Z of a unit normal from its X and Y.", "" },
    { Kind::NormalUnpack, "Vector", "Turns a normal-map colour (0..1) into a normal vector (-1..1).", "" },

    // Colour
    { Kind::Desaturate, "Color", "Moves a colour towards grey by Fraction.", "fraction, default 1" },
    { Kind::SrgbToLinear, "Color", "Converts a colour from sRGB to linear.", "" },
    { Kind::LinearToSrgb, "Color", "Converts a colour from linear to sRGB.", "" },
    { Kind::HsvToRgb, "Color", "Converts hue, saturation, value to red, green, blue.", "" },
    { Kind::RgbToHsv, "Color", "Converts red, green, blue to hue, saturation, value.", "" },
    { Kind::HueShift, "Color", "Rotates the hue of a colour.", "" },
    { Kind::Contrast, "Color", "Raises or lowers the contrast of a colour around grey.", "" },
    { Kind::ChannelMixer, "Color", "Mixes the colour channels with Red, Green and Blue weights.", "" },
    { Kind::ColorRamp, "Color", "Maps a 0..1 value to a gradient of colour stops.", "stops: pos r g b, repeated; at least two" },
    { Kind::BlackBody, "Color", "The colour of a hot body at a temperature in kelvin (fire, lava, lamps).", "" },
    { Kind::BlendOverlay, "Color", "Overlay blend of Blend over Base.", "" },
    { Kind::BlendScreen, "Color", "Screen blend of Blend over Base (lightens).", "" },
    { Kind::BlendSoftLight, "Color", "Soft-light blend of Blend over Base.", "" },
    { Kind::BlendHardLight, "Color", "Hard-light blend of Blend over Base.", "" },
    { Kind::BlendLinearLight, "Color", "Linear-light blend of Blend over Base.", "" },
    { Kind::BlendColorBurn, "Color", "Colour-burn blend of Blend over Base (darkens).", "" },
    { Kind::BlendColorDodge, "Color", "Colour-dodge blend of Blend over Base (brightens).", "" },
    { Kind::BlendDarken, "Color", "The darker of Base and Blend.", "" },
    { Kind::BlendLighten, "Color", "The lighter of Base and Blend.", "" },
    { Kind::BlendDifference, "Color", "The absolute difference of Base and Blend.", "" },
    { Kind::VertexColor, "Color", "The colour painted on the mesh vertices.", "" },

    // Patterns
    { Kind::Noise, "Pattern", "Procedural noise: clouds, grain, stone, dirt. More levels look richer and cost more.", "function turbulence scale quality levels levelScale outMin outMax; function simplex|gradient|value|voronoi; e.g. simplex false 8 1 2 2 0 1" },
    { Kind::VectorNoise, "Pattern", "Noise that returns a random vector (swirls, flow).", "" },
    { Kind::Checkerboard, "Pattern", "A two-colour checker pattern over UV.", "" },
    { Kind::Ellipse, "Pattern", "A soft-edged ellipse mask over UV.", "" },
    { Kind::Rectangle, "Pattern", "A rectangle mask over UV.", "" },
    { Kind::RoundedRectangle, "Pattern", "A rounded-rectangle mask over UV (buttons, panels).", "" },
    { Kind::Twirl, "Pattern", "Twists UV around a centre.", "" },
    { Kind::PolarCoordinates, "Pattern", "Turns UV into angle and radius around a centre.", "" },
    { Kind::Fresnel, "Pattern", "Bright at glancing angles, dark facing the camera: rims, glass, shells.", "exponent baseReflect, default 5 0.04" },

    // Coordinates
    { Kind::Uv, "Coordinate", "The mesh UV coordinate.", "0 for the first UV set, 1 for the second" },
    { Kind::TextureCoordinate, "Coordinate", "The UV coordinate scaled by a tiling; the usual way to repeat a pattern.", "uTile vTile set" },
    { Kind::Panner, "Coordinate", "Scrolls a coordinate over time (flowing water, conveyors).", "speedU speedV" },
    { Kind::Rotator, "Coordinate", "Rotates a coordinate about a centre over time.", "speed centerU centerV" },
    { Kind::BumpOffset, "Coordinate", "Shifts UV with the view angle for a cheap depth look.", "heightRatio, default 0.05" },
    { Kind::ScreenPosition, "Coordinate", "The pixel's position on screen, 0..1.", "" },
    { Kind::ViewportUV, "Coordinate", "The pixel's position in the viewport, 0..1.", "" },
    { Kind::PixelPosition, "Coordinate", "The pixel's position in pixels.", "" },

    // Time
    { Kind::Time, "Time", "Seconds since the game started; feed it to Sine, Panner or a noise for animation.", "" },
    { Kind::DeltaTime, "Time", "Seconds of the last frame.", "" },

    // Object and position
    { Kind::WorldPosition, "Object", "The pixel's position in the world.", "" },
    { Kind::LocalPosition, "Object", "The pixel's position in the object's own space.", "" },
    { Kind::ObjectPosition, "Object", "The world position of the object's pivot.", "" },
    { Kind::ObjectRadius, "Object", "The radius of the object's bounds.", "" },
    { Kind::ObjectBounds, "Object", "The size of the object's bounds.", "" },
    { Kind::ObjectOrientation, "Object", "The direction the object's up axis points in the world.", "" },
    { Kind::PerInstanceRandom, "Object", "A random 0..1 number fixed per instance (varies grass blades or crates).", "" },
    { Kind::PerInstanceFadeAmount, "Object", "How faded an instance is, 1 fully visible.", "" },
    { Kind::PerInstanceCustomData, "Object", "A number the instance carries from the game.", "" },
    { Kind::DistanceCullFade, "Object", "Fades out an object as it nears its draw distance.", "" },
    { Kind::PreSkinnedPosition, "Object", "The vertex position before skeletal animation.", "" },
    { Kind::PreSkinnedNormal, "Object", "The vertex normal before skeletal animation.", "" },
    { Kind::VertexNormalWS, "Object", "The interpolated vertex normal in world space.", "" },
    { Kind::VertexTangentWS, "Object", "The vertex tangent in world space.", "" },
    { Kind::PixelNormalWS, "Object", "The pixel's final normal in world space.", "" },
    { Kind::TwoSidedSign, "Object", "1 on a front face, -1 on a back face of a two-sided material.", "" },

    // Camera and scene
    { Kind::CameraPosition, "Camera", "The camera's position in the world.", "" },
    { Kind::CameraVector, "Camera", "The direction from the pixel to the camera.", "" },
    { Kind::ReflectionVector, "Camera", "The view direction mirrored by the surface normal.", "" },
    { Kind::LightVector, "Camera", "The direction towards the main light; toon and rim shading use it.", "" },
    { Kind::ViewProperty, "Camera", "A property of the view.", "viewSize | invViewSize | screenPosition | pixelPosition" },
    { Kind::ViewSize, "Camera", "The size of the view in pixels.", "" },
    { Kind::PixelDepth, "Camera", "The pixel's depth from the camera.", "" },
    { Kind::CameraDepthFade, "Camera", "0..1 by the distance from the camera: fades near objects away.", "fadeLength fadeOffset" },
    { Kind::SceneDepth, "Scene", "The depth of the opaque scene behind this pixel. Needs a translucent material; heavy on phones.", "" },
    { Kind::SceneColor, "Scene", "The opaque scene colour behind this pixel (refraction, distortion). Needs a translucent or post-process material; heavy on phones.", "" },
    { Kind::SceneTexture, "Scene", "A buffer of the scene (colour or depth) at a screen position. Heavy on phones.", "color | depth" },
    { Kind::DepthFade, "Scene", "0..1 by how far this surface is in front of the scene behind it: soft water edges, soft particles.", "fadeDistance" },

    // Attributes
    { Kind::MakeMaterialAttributes, "Attributes", "Packs all surface channels into one attribute bundle.", "" },
    { Kind::BreakMaterialAttributes, "Attributes", "Splits an attribute bundle into its channels.", "" },
    { Kind::BlendMaterialAttributes, "Attributes", "Blends two attribute bundles by a factor (layering whole materials).", "" },
    { Kind::GetMaterialAttributes, "Attributes", "Reads chosen channels of an attribute bundle.", "" },
    { Kind::SetMaterialAttributes, "Attributes", "Changes chosen channels of an attribute bundle.", "" },
    { Kind::LayerStack, "Attributes", "A stack of material layers blended with masks into one attribute bundle.", "" },

    // Switches
    { Kind::RuntimeSwitch, "Switch", "Picks one of four cases by an index at run time.", "" },
    { Kind::StaticSwitch, "Switch", "Keeps the True or the False branch, chosen when the shader is built.", "" },
    { Kind::QualitySwitch, "Switch", "A different branch per quality level (Low for phones).", "" },
    { Kind::FeatureLevelSwitch, "Switch", "A different branch per graphics level (ES3 phones, SM5, SM6 desktop).", "" },
    { Kind::ShadingPathSwitch, "Switch", "A different branch per lighting path (forward, forward plus, deferred).", "" },
    { Kind::ShaderStageSwitch, "Switch", "A different branch for the vertex and the pixel stage.", "" },

    // Functions and organisation
    { Kind::CustomCode, "Function", "A snippet of shader code with inputs A and B. For developers; prefer nodes.", "" },
    { Kind::MaterialFunctionCall, "Function", "Calls a reusable material function (.kbmatfn).", "" },
    { Kind::FunctionInput, "Function", "An input of a material function (only inside a function).", "" },
    { Kind::FunctionOutput, "Function", "An output of a material function (only inside a function).", "" },
    { Kind::CompositeInput, "Utility", "The input of a collapsed group of nodes.", "" },
    { Kind::CompositeOutput, "Utility", "The output of a collapsed group of nodes.", "" },
    { Kind::Reroute, "Utility", "A pass-through point that tidies a long link.", "" },
    { Kind::NamedRerouteDeclaration, "Utility", "Names a value so far-away nodes can use it without a link.", "" },
    { Kind::NamedRerouteUsage, "Utility", "Reads a value named by a Named Reroute Declaration.", "" },
};

} // namespace

std::span<const RenderMaterialGraphNodeDoc> RenderMaterialGraphNodeDocs() noexcept {
    return kDocs;
}

const RenderMaterialGraphNodeDoc* FindRenderMaterialGraphNodeDoc(RenderMaterialGraphNodeKind kind) noexcept {
    const auto found = std::ranges::find(kDocs, kind, &RenderMaterialGraphNodeDoc::kind);
    return found != std::end(kDocs) ? &*found : nullptr;
}

} // namespace kb::render
