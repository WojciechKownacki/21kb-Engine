/*
 * Copyright 2011-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bimg/blob/master/LICENSE
 */

#ifndef BIMG_ETC2_ALPHA_H_HEADER_GUARD
#define BIMG_ETC2_ALPHA_H_HEADER_GUARD

#include <stdint.h>

namespace bimg
{
	static constexpr int8_t s_etc2aMod[16][8] =
	{
		{ -3, -6,  -9, -15, 2, 5, 8, 14 },
		{ -3, -7, -10, -13, 2, 6, 9, 12 },
		{ -2, -5,  -8, -13, 1, 4, 7, 12 },
		{ -2, -4,  -6, -13, 1, 3, 5, 12 },
		{ -3, -6,  -8, -12, 2, 5, 7, 11 },
		{ -3, -7,  -9, -11, 2, 6, 8, 10 },
		{ -4, -7,  -8, -11, 3, 6, 7, 10 },
		{ -3, -5,  -8, -11, 2, 4, 7, 10 },
		{ -2, -6,  -8, -10, 1, 5, 7,  9 },
		{ -2, -5,  -8, -10, 1, 4, 7,  9 },
		{ -2, -4,  -8, -10, 1, 3, 7,  9 },
		{ -2, -5,  -7, -10, 1, 4, 6,  9 },
		{ -3, -4,  -7, -10, 2, 3, 6,  9 },
		{ -1, -2,  -3, -10, 0, 1, 2,  9 },
		{ -4, -6,  -8,  -9, 3, 5, 7,  8 },
		{ -3, -5,  -7,  -9, 2, 4, 6,  8 },
	};
} // namespace bimg

#endif // BIMG_ETC2_ALPHA_H_HEADER_GUARD
