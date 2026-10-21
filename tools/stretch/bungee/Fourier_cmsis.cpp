// Bungee Fourier backend on CMSIS-DSP arm_rfft_fast_f32, replacing src/Fourier.cpp
// (pffft) on the Blackbox (and in the tools/lab Cortex-M7 cost probe). pffft's ordered real layout,
// [re0, reN/2, re1, im1, ...], is the same as CMSIS's, so the glue matches
// Fourier.cpp's Pffft kernel. CMSIS overwrites its input and divides the
// inverse by N; pffft does neither, so copy in and scale back out.
// One scratch buffer for every kernel: Bungee runs one transform at a time, and a buffer per
// kernel (five of them, one per size and direction) cost 64 KB of the leaf's SRAM.
#include "src/Fourier.h"
#include "src/Assert.h"

#include "dsp/transform_functions.h"

#include <cstdlib>
#include <cstring>

namespace Bungee::Fourier {

namespace {

struct Cmsis
{
	struct Kernel
	{
		arm_rfft_fast_instance_f32 s;

		Kernel(int log2TransformLength);

		void forward(int log2TransformLength, float *t, std::complex<float> *f) const;
		void inverse(int log2TransformLength, float *t, std::complex<float> *f) const;
	};

	typedef Kernel Forward;
	typedef Kernel Inverse;
};

constexpr int maxLog2 = 12;  // Cache<Cmsis, maxLog2>: 4096 points, the largest Bungee uses at 48 kHz
alignas(8) float scratch[1 << maxLog2];

Cmsis::Kernel::Kernel(int log2TransformLength)
{
	if (log2TransformLength > maxLog2 || arm_rfft_fast_init_f32(&s, (uint16_t)(1 << log2TransformLength)) != ARM_MATH_SUCCESS)
		abort();
}

void Cmsis::Kernel::forward(int log2TransformLength, float *t, std::complex<float> *f) const
{
	const auto n = 1 << log2TransformLength;
	std::memcpy(scratch, t, n * sizeof(float));
	arm_rfft_fast_f32(const_cast<arm_rfft_fast_instance_f32 *>(&s), scratch, (float *)f, 0);
	f[n / 2] = f[0].imag();
	f[0].imag(0.f);
}

void Cmsis::Kernel::inverse(int log2TransformLength, float *t, std::complex<float> *f) const
{
	const auto n = 1 << log2TransformLength;
	std::memcpy(scratch, f, n * sizeof(float));
	scratch[1] = f[n / 2].real();
	arm_rfft_fast_f32(const_cast<arm_rfft_fast_instance_f32 *>(&s), scratch, t, 1);
	const float k = (float)n;
	for (int i = 0; i < n; ++i)
		t[i] *= k;
}

} // namespace

typedef Cache<Cmsis, maxLog2> Implementation;

Transforms::Transforms()
{
	p = new Implementation;
}

Transforms::~Transforms()
{
	delete reinterpret_cast<Implementation *>(p);
}

void Transforms::prepareForward(int log2TransformLength)
{
	reinterpret_cast<Implementation *>(p)->prepareForward(log2TransformLength);
}

void Transforms::prepareInverse(int log2TransformLength)
{
	reinterpret_cast<Implementation *>(p)->prepareInverse(log2TransformLength);
}

void Transforms::forward(int log2TransformLength, const Eigen::Ref<const Eigen::ArrayXXf> &t, Eigen::Ref<Eigen::ArrayXXcf> f)
{
	reinterpret_cast<Implementation *>(p)->forward(log2TransformLength, t, f);
}

void Transforms::inverse(int log2TransformLength, Eigen::Ref<Eigen::ArrayXXf> t, const Eigen::Ref<const Eigen::ArrayXXcf> &f)
{
	reinterpret_cast<Implementation *>(p)->inverse(log2TransformLength, t, f);
}

} // namespace Bungee::Fourier
