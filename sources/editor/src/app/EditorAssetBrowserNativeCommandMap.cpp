#include "app/EditorAssetBrowserNativeCommandMap.hpp"

namespace kb::editor {
namespace {

inline constexpr std::uint32_t kImport = 2001U;
inline constexpr std::uint32_t kNewFolder = 2002U;
inline constexpr std::uint32_t kNewLuaScript = 2003U;
inline constexpr std::uint32_t kNewInputAction = 2004U;
inline constexpr std::uint32_t kNewInputMappingContext = 2005U;
inline constexpr std::uint32_t kNewInputAxis = 2006U;
inline constexpr std::uint32_t kNewMaterial = 2007U;
inline constexpr std::uint32_t kCreateMaterialInstance = 2008U;
inline constexpr std::uint32_t kNewMaterialGraph = 2009U;
inline constexpr std::uint32_t kNewMaterialType = 2010U;
inline constexpr std::uint32_t kCreateMaterialFromGraph = 2011U;
inline constexpr std::uint32_t kCreateMaterialFromMaterialType = 2012U;
inline constexpr std::uint32_t kNewMaterialFunction = 2013U;
inline constexpr std::uint32_t kNewAudioMixer = 2014U;
inline constexpr std::uint32_t kNewParticleEffect = 2015U;
inline constexpr std::uint32_t kDirectionalLight = 2101U;
inline constexpr std::uint32_t kPointLight = 2102U;
inline constexpr std::uint32_t kSpotLight = 2103U;
inline constexpr std::uint32_t kRename = 2201U;
inline constexpr std::uint32_t kDelete = 2202U;
inline constexpr std::uint32_t kRefresh = 2203U;
inline constexpr std::uint32_t kExtractMaterials = 2301U;

} // namespace

std::uint32_t EditorAssetBrowserNativeCommandMap::Id(EditorAssetContextCommand command) noexcept {
    switch (command) {
    case EditorAssetContextCommand::Import: return kImport;
    case EditorAssetContextCommand::NewFolder: return kNewFolder;
    case EditorAssetContextCommand::NewLuaScript: return kNewLuaScript;
    case EditorAssetContextCommand::NewMaterial: return kNewMaterial;
    case EditorAssetContextCommand::NewMaterialFunction: return kNewMaterialFunction;
    case EditorAssetContextCommand::NewMaterialGraph: return kNewMaterialGraph;
    case EditorAssetContextCommand::NewMaterialType: return kNewMaterialType;
    case EditorAssetContextCommand::CreateMaterialInstance: return kCreateMaterialInstance;
    case EditorAssetContextCommand::CreateMaterialFromGraph: return kCreateMaterialFromGraph;
    case EditorAssetContextCommand::CreateMaterialFromMaterialType: return kCreateMaterialFromMaterialType;
    case EditorAssetContextCommand::NewInputAction: return kNewInputAction;
    case EditorAssetContextCommand::NewInputAxis: return kNewInputAxis;
    case EditorAssetContextCommand::NewInputMappingContext: return kNewInputMappingContext;
    case EditorAssetContextCommand::NewAudioMixer: return kNewAudioMixer;
    case EditorAssetContextCommand::NewParticleEffect: return kNewParticleEffect;
    case EditorAssetContextCommand::ExtractMaterials: return kExtractMaterials;
    case EditorAssetContextCommand::AddDirectionalLight: return kDirectionalLight;
    case EditorAssetContextCommand::AddPointLight: return kPointLight;
    case EditorAssetContextCommand::AddSpotLight: return kSpotLight;
    case EditorAssetContextCommand::Rename: return kRename;
    case EditorAssetContextCommand::Delete: return kDelete;
    case EditorAssetContextCommand::Refresh: return kRefresh;
    case EditorAssetContextCommand::Open:
    case EditorAssetContextCommand::Duplicate:
    case EditorAssetContextCommand::FindReferences:
    case EditorAssetContextCommand::AddLighting:
    case EditorAssetContextCommand::None:
    default: return 0U;
    }
}

EditorAssetContextCommand EditorAssetBrowserNativeCommandMap::Command(std::uint32_t id) noexcept {
    switch (id) {
    case kImport: return EditorAssetContextCommand::Import;
    case kNewFolder: return EditorAssetContextCommand::NewFolder;
    case kNewLuaScript: return EditorAssetContextCommand::NewLuaScript;
    case kNewMaterial: return EditorAssetContextCommand::NewMaterial;
    case kNewMaterialFunction: return EditorAssetContextCommand::NewMaterialFunction;
    case kNewMaterialGraph: return EditorAssetContextCommand::NewMaterialGraph;
    case kNewMaterialType: return EditorAssetContextCommand::NewMaterialType;
    case kCreateMaterialInstance: return EditorAssetContextCommand::CreateMaterialInstance;
    case kCreateMaterialFromGraph: return EditorAssetContextCommand::CreateMaterialFromGraph;
    case kCreateMaterialFromMaterialType: return EditorAssetContextCommand::CreateMaterialFromMaterialType;
    case kNewInputAction: return EditorAssetContextCommand::NewInputAction;
    case kNewInputAxis: return EditorAssetContextCommand::NewInputAxis;
    case kNewInputMappingContext: return EditorAssetContextCommand::NewInputMappingContext;
    case kNewAudioMixer: return EditorAssetContextCommand::NewAudioMixer;
    case kNewParticleEffect: return EditorAssetContextCommand::NewParticleEffect;
    case kExtractMaterials: return EditorAssetContextCommand::ExtractMaterials;
    case kDirectionalLight: return EditorAssetContextCommand::AddDirectionalLight;
    case kPointLight: return EditorAssetContextCommand::AddPointLight;
    case kSpotLight: return EditorAssetContextCommand::AddSpotLight;
    case kRename: return EditorAssetContextCommand::Rename;
    case kDelete: return EditorAssetContextCommand::Delete;
    case kRefresh: return EditorAssetContextCommand::Refresh;
    default: return EditorAssetContextCommand::None;
    }
}

} // namespace kb::editor
