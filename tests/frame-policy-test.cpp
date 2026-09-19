#include "../src/frame-policy.h"
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char *message)
{
	if (!value) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

static int count_frames(int source_fps, int target_fps, bool jitter)
{
	frame_policy::RateLimiter limiter;
	int count = 0;
	for (int n = 0; n < source_fps * 10; ++n) {
		const int64_t offset = jitter ? (n % 2 ? -50000 : 50000) : 0;
		const uint64_t now = 1000000000ULL + uint64_t(n) * 1000000000ULL / source_fps + offset;
		count += limiter.due(now, target_fps);
	}
	return count;
}

int main()
{
	for (int fps : {15, 24, 30, 60}) {
		check(count_frames(fps, fps, true) == fps * 10, "matching rate must tolerate clock jitter");
		check(count_frames(60, fps, false) == fps * 10, "60 FPS downsampling must keep its cadence");
	}
	check(count_frames(30, 0, true) == 300, "unlimited submits every frame");
	check(count_frames(30, 60, true) == 300, "cannot submit above source rate");
	frame_policy::RateLimiter limiter;
	check(limiter.due(1000000000, 30), "first frame immediate");
	check(!limiter.due(1000000000, 30), "duplicate tick rejected");
	check(limiter.due(5000000000, 30), "resume after a long pause");
	check(!limiter.due(5000000001, 30), "no catch-up burst");
	check(limiter.due(5000000002, 15), "rate change immediate");
	limiter.reset();
	check(limiter.due(1, 30), "reset allows new clock epoch");
	for (uint32_t limit : {720u, 1080u, 1440u, 2160u}) {
		auto size = frame_policy::processing_size(3840, 2160, limit);
		check(size.width == limit * 16 / 9 && size.height == limit, "4K scales to selected budget");
	}
	auto size = frame_policy::processing_size(640, 480, 1080);
	check(size.width == 640 && size.height == 480, "small sources not enlarged");
	size = frame_policy::processing_size(3840, 2160, 0);
	check(size.width == 3840 && size.height == 2160, "source mode preserves dimensions");
	size = frame_policy::processing_size(2160, 3840, 1080);
	check(size.width == 606 && size.height == 1080, "portrait preserves aspect within rounding");
	size = frame_policy::processing_size(3440, 1440, 1080);
	check(size.width == 1920 && size.height == 802, "ultrawide respects width limit");
	size = frame_policy::processing_size(0, 0, 1080);
	check(!size.width && !size.height, "empty source remains empty");
	std::puts("PASS: cadence/jitter/pause/rate-change and resolution/aspect-ratio tests");
}
