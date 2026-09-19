#pragma once

#include <algorithm>
#include <cstdint>

namespace frame_policy {

struct Size {
	uint32_t width, height;
};

// Fit inside a 16:9 processing budget without enlarging small sources.
inline Size processing_size(uint32_t width, uint32_t height, uint32_t max_height)
{
	if (!width || !height || !max_height)
		return {width, height};
	const uint32_t max_width = max_height * 16 / 9;
	if (width <= max_width && height <= max_height)
		return {width, height};
	if (uint64_t(width) * max_height > uint64_t(height) * max_width) {
		height = uint32_t(uint64_t(height) * max_width / width);
		width = max_width;
	} else {
		width = uint32_t(uint64_t(width) * max_height / height);
		height = max_height;
	}
	return {(std::max)(2u, width & ~1u), (std::max)(2u, height & ~1u)};
}

class RateLimiter {
	uint64_t next_ns = 0;
	uint32_t previous_fps = 0;

public:
	void reset() { next_ns = 0; }

	bool due(uint64_t now, uint32_t fps)
	{
		if (!fps) {
			next_ns = 0;
			previous_fps = 0;
			return true;
		}
		if (fps != previous_fps) {
			next_ns = 0;
			previous_fps = fps;
		}
		const uint64_t interval = 1000000000ULL / fps;
		// Allow small render-clock jitter without losing every other frame.
		const uint64_t tolerance = std::min<uint64_t>(1000000, interval / 16);
		if (!next_ns) {
			next_ns = now + interval;
			return true;
		}
		if (now + tolerance < next_ns)
			return false;
		// Keep the original cadence; skip missed deadlines, never build a backlog.
		next_ns += ((now + tolerance - next_ns) / interval + 1) * interval;
		return true;
	}
};

} // namespace frame_policy
