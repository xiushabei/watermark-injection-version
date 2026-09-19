#pragma once

struct FontData
{
	unsigned char* data;
	int size;
};

namespace Fonts
{
	void init();
	inline FontData alibaba = { nullptr, 0 };
	inline FontData icons = { nullptr, 0 };
};

