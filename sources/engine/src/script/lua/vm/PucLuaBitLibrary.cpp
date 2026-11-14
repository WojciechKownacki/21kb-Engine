// The `bit` library for the Lua 5.4 build, with the semantics of the one LuaJIT carries: every operand is brought to a
// signed 32-bit integer the same way (the number is rounded to the nearest integer and taken modulo 2^32), every result
// is a signed 32-bit integer, shift counts are taken modulo 32. Scripts written for either VM then give the same results.
#include "script/lua/LuaVm.hpp"
#include "script/lua/PucLuaBitLibrary.hpp"

#include <cstdint>
#include <cstring>

namespace kb::script {
namespace {

// The sum with 2^52 + 2^51 puts the rounded integer in the low bits of the double's mantissa.
[[nodiscard]] std::int32_t ToBit(double number) noexcept {
    const double shifted = number + 6755399441055744.0;
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &shifted, sizeof(bits));
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
}

[[nodiscard]] std::int32_t Argument(lua_State* state, int index) {
    return ToBit(luaL_checknumber(state, index));
}

int PushBit(lua_State* state, std::int32_t value) {
    lua_pushinteger(state, static_cast<lua_Integer>(value));
    return 1;
}

int BitToBit(lua_State* state) { return PushBit(state, Argument(state, 1)); }
int BitBNot(lua_State* state) { return PushBit(state, ~Argument(state, 1)); }

template <class Operation>
int Fold(lua_State* state, Operation operation) {
    const int count = lua_gettop(state);
    std::int32_t result = Argument(state, 1);
    for (int index = 2; index <= count; ++index) result = operation(result, Argument(state, index));
    return PushBit(state, result);
}

int BitBAnd(lua_State* state) { return Fold(state, [](std::int32_t a, std::int32_t b) { return a & b; }); }
int BitBOr(lua_State* state) { return Fold(state, [](std::int32_t a, std::int32_t b) { return a | b; }); }
int BitBXor(lua_State* state) { return Fold(state, [](std::int32_t a, std::int32_t b) { return a ^ b; }); }

[[nodiscard]] std::uint32_t Unsigned(std::int32_t value) noexcept { return static_cast<std::uint32_t>(value); }

int BitLShift(lua_State* state) {
    return PushBit(state, static_cast<std::int32_t>(Unsigned(Argument(state, 1)) << (Argument(state, 2) & 31)));
}
int BitRShift(lua_State* state) {
    return PushBit(state, static_cast<std::int32_t>(Unsigned(Argument(state, 1)) >> (Argument(state, 2) & 31)));
}
int BitARShift(lua_State* state) {
    const std::int32_t value = Argument(state, 1);
    const int count = Argument(state, 2) & 31;
    // Arithmetic shift written without relying on the sign handling of >> on negative values.
    return PushBit(state, count == 0 ? value : static_cast<std::int32_t>((Unsigned(value) >> count) | (value < 0 ? ~(~0U >> count) : 0U)));
}
int BitRol(lua_State* state) {
    const std::uint32_t value = Unsigned(Argument(state, 1));
    const int count = Argument(state, 2) & 31;
    return PushBit(state, static_cast<std::int32_t>(count == 0 ? value : (value << count) | (value >> (32 - count))));
}
int BitRor(lua_State* state) {
    const std::uint32_t value = Unsigned(Argument(state, 1));
    const int count = Argument(state, 2) & 31;
    return PushBit(state, static_cast<std::int32_t>(count == 0 ? value : (value >> count) | (value << (32 - count))));
}
int BitBSwap(lua_State* state) {
    const std::uint32_t value = Unsigned(Argument(state, 1));
    return PushBit(state, static_cast<std::int32_t>((value >> 24) | ((value >> 8) & 0xff00U) | ((value << 8) & 0xff0000U) | (value << 24)));
}

// tohex(x [, n]): the low |n| hex digits (default 8, at most 8), upper case when n is negative.
int BitToHex(lua_State* state) {
    const std::uint32_t value = Unsigned(Argument(state, 1));
    int digits = lua_isnoneornil(state, 2) != 0 ? 8 : Argument(state, 2);
    const char* alphabet = "0123456789abcdef";
    if (digits < 0) {
        digits = -digits;
        alphabet = "0123456789ABCDEF";
    }
    if (digits > 8) digits = 8;
    char text[8];
    for (int position = digits - 1; position >= 0; --position) text[position] = alphabet[(value >> ((digits - 1 - position) * 4)) & 15U];
    lua_pushlstring(state, text, static_cast<std::size_t>(digits));
    return 1;
}

constexpr luaL_Reg kBitFunctions[] = {
    { "tobit", &BitToBit },     { "bnot", &BitBNot },       { "band", &BitBAnd },     { "bor", &BitBOr },
    { "bxor", &BitBXor },       { "lshift", &BitLShift },   { "rshift", &BitRShift }, { "arshift", &BitARShift },
    { "rol", &BitRol },         { "ror", &BitRor },         { "bswap", &BitBSwap },   { "tohex", &BitToHex },
    { nullptr, nullptr },
};

} // namespace

int PucLuaBitLibrary::Open(lua_State* state) {
    luaL_newlib(state, kBitFunctions);
    return 1;
}

} // namespace kb::script
