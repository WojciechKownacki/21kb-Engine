// .kbloc localization catalogs.
#include "FuzzSupport.hpp"

#include "engine/localization/LocalizationCatalogIO.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static_cast<void>(kb::localization::LocalizationCatalogIO::Load(kb::fuzz::Bytes(data, size)));
    return 0;
}
