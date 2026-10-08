#pragma once

#include <cstddef>
#include <span>

namespace kb::platform {

// Rewrites, inside a minidump, every module path to the module's file name: the loaded and the
// unloaded module lists, and the PDB path in each module's CodeView record. A path names the
// folder the game was installed in, which is the player's user folder as often as not, and the
// build machine's folders for the PDB. What a symbol server needs stays untouched: the module
// file name, its time stamp and image size, and the PDB file name, GUID and age.
//
// Works in place and never allocates, so it can run on the crash reporter thread over a mapped
// dump. Every name only gets shorter; the file keeps its size and layout. Returns false, having
// changed nothing it could not prove in bounds, when the bytes are not a well-formed minidump.
[[nodiscard]] bool StripMinidumpModulePaths(std::span<std::byte> dump) noexcept;

} // namespace kb::platform
