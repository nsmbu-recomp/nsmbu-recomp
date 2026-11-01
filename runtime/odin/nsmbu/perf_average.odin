package nsmbu

FRAME_COUNT :: 120

PerfAverage :: struct {
	started:   bool,
	_pad:      [7]u8,
	since:     f64,
	fps:       f64,
	logic:     f64,
	first_frames: u64,
	first_steps:  u64,
	last_frames:  u64,
	last_steps:   u64,
	renderer:  i32,
	mode:      i32,
	target:    i32,
	scale:     f32,
}

#assert(size_of(PerfAverage) == 80)
#assert(align_of(PerfAverage) == 8)

read_f32 :: proc "c" (p: ^f32, i: int) -> f32 {
	base := uintptr(cast(rawptr)p)
	return (cast(^f32)(base + uintptr(i * size_of(f32))))^
}

read_u32 :: proc "c" (p: ^u32, i: int) -> u32 {
	base := uintptr(cast(rawptr)p)
	return (cast(^u32)(base + uintptr(i * size_of(u32))))^
}

ceil_positive :: proc "c" (x: f64) -> i32 {
	if x <= 0 {
		return 0
	}
	i := i32(x)
	if f64(i) < x {
		return i + 1
	}
	return i
}

sort_ascending :: proc "c" (data: ^[FRAME_COUNT]f32, n: int) #no_bounds_check {
	for i in 1 ..< n {
		key := data[i]
		j := i - 1
		for j >= 0 && data[j] > key {
			data[j + 1] = data[j]
			j -= 1
		}
		data[j + 1] = key
	}
}

@(export)
nsmbu_perf_average_reset :: proc "c" (state: ^PerfAverage) {
	state.started = false
	state.fps = 0
	state.logic = 0
}

@(export)
nsmbu_perf_average_sample :: proc "c" (
	state: ^PerfAverage,
	now: f64,
	frames: u64,
	steps: u64,
	api: i32,
	rate_mode: i32,
	rate: i32,
	resolution: f32,
) {
	if !state.started ||
	   api != state.renderer ||
	   rate_mode != state.mode ||
	   rate != state.target ||
	   resolution != state.scale ||
	   frames < state.last_frames ||
	   steps < state.last_steps ||
	   now < state.since {
		state.started = true
		state.since = now
		state.first_frames = frames
		state.first_steps = steps
		state.renderer = api
		state.mode = rate_mode
		state.target = rate
		state.scale = resolution
		state.fps = 0
		state.logic = 0
	} else if now > state.since {
		state.fps = f64(frames - state.first_frames) / (now - state.since)
		state.logic = f64(steps - state.first_steps) / (now - state.since)
	}
	state.last_frames = frames
	state.last_steps = steps
}

@(export)
nsmbu_frame_history_percentile_ms :: proc "c" (ms: ^f32, p: f32) -> f32 #no_bounds_check {
	sorted: [FRAME_COUNT]f32
	n := 0
	for i in 0 ..< FRAME_COUNT {
		v := read_f32(ms, i)
		if v > 0 {
			sorted[n] = v
			n += 1
		}
	}
	if n == 0 {
		return 0
	}
	sort_ascending(&sorted, n)
	idx := ceil_positive(f64(f32(n) * p)) - 1
	if idx < 0 {
		idx = 0
	}
	return sorted[idx]
}

@(export)
nsmbu_frame_history_low1_fps :: proc "c" (ms: ^f32) -> f32 {
	p99 := nsmbu_frame_history_percentile_ms(ms, 0.99)
	if p99 > 0 {
		return 1000.0 / p99
	}
	return 0
}

@(export)
nsmbu_frame_history_compute :: proc "c" (
	ms: ^f32,
	logic: ^u32,
	avg_ms: ^f32,
	worst_ms: ^f32,
	fps: ^f32,
	logic_rate: ^f64,
) {
	sum: f64 = 0
	logic_sum: u64 = 0
	worst_ms^ = 0
	for i in 0 ..< FRAME_COUNT {
		frame_ms := read_f32(ms, i)
		sum += f64(frame_ms)
		if frame_ms > worst_ms^ {
			worst_ms^ = frame_ms
		}
		logic_sum += u64(read_u32(logic, i))
	}
	avg_ms^ = f32(sum / f64(FRAME_COUNT))
	fps^ = 0
	logic_rate^ = 0
	if sum > 0 {
		fps^ = f32(1000.0 * f64(FRAME_COUNT) / sum)
		logic_rate^ = f64(logic_sum) / (sum / 1000.0)
	}
}
