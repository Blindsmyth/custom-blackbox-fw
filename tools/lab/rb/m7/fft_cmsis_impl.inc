/* D_CMSIS: float RFFT via CMSIS-DSP arm_rfft_fast_f32. Included from FFT.cpp under HAVE_CMSIS_DSP. */
#ifdef HAVE_CMSIS_DSP

#include "dsp/transform_functions.h"

class D_CMSIS : public FFTImpl
{
public:
    D_CMSIS(int size) :
        m_size(size),
        m_hs(size / 2)
    {
        m_buf = new float[m_size];
        m_packed = new float[m_size]; /* CMSIS real-FFT packing, length N */
        if (arm_rfft_fast_init_f32(&m_rfft, (uint16_t)m_size) != ARM_MATH_SUCCESS)
            abort();
    }

    ~D_CMSIS() {
        delete[] m_buf;
        delete[] m_packed;
    }

    int getSize() const { return m_size; }

    FFT::Precisions getSupportedPrecisions() const {
        return FFT::SinglePrecision;
    }

    void initFloat() { }
    void initDouble() { }

    /* CMSIS's inverse divides by N; Rubber Band's FFTs leave the inverse
     * unnormalised (D_Builtin, D_KISSFFT), and the stretchers scale for that. */
    void unnormalise(float *BQ_R__ out) {
        const float n = (float)m_size;
        for (int i = 0; i < m_size; ++i)
            out[i] *= n;
    }

    /* CMSIS pack: [re0, reN/2, re1, im1, re2, im2, ...] */
    void unpackFloat(float *BQ_R__ re, float *BQ_R__ im) {
        re[0] = m_packed[0];
        if (im) im[0] = 0.f;
        re[m_hs] = m_packed[1];
        if (im) im[m_hs] = 0.f;
        for (int i = 1; i < m_hs; ++i) {
            re[i] = m_packed[2 * i];
            if (im) im[i] = m_packed[2 * i + 1];
        }
    }

    void packFloat(const float *BQ_R__ re, const float *BQ_R__ im) {
        m_packed[0] = re[0];
        m_packed[1] = re[m_hs];
        for (int i = 1; i < m_hs; ++i) {
            m_packed[2 * i] = re[i];
            m_packed[2 * i + 1] = im ? im[i] : 0.f;
        }
    }

    void forward(const float *BQ_R__ realIn, float *BQ_R__ realOut, float *BQ_R__ imagOut) {
        v_copy(m_buf, realIn, m_size);
        arm_rfft_fast_f32(&m_rfft, m_buf, m_packed, 0);
        unpackFloat(realOut, imagOut);
    }

    void forwardInterleaved(const float *BQ_R__ realIn, float *BQ_R__ complexOut) {
        v_copy(m_buf, realIn, m_size);
        arm_rfft_fast_f32(&m_rfft, m_buf, m_packed, 0);
        complexOut[0] = m_packed[0];
        complexOut[1] = 0.f;
        for (int i = 1; i < m_hs; ++i) {
            complexOut[2 * i] = m_packed[2 * i];
            complexOut[2 * i + 1] = m_packed[2 * i + 1];
        }
        complexOut[2 * m_hs] = m_packed[1];
        complexOut[2 * m_hs + 1] = 0.f;
    }

    void forwardPolar(const float *BQ_R__ realIn, float *BQ_R__ magOut, float *BQ_R__ phaseOut) {
        v_copy(m_buf, realIn, m_size);
        arm_rfft_fast_f32(&m_rfft, m_buf, m_packed, 0);
        magOut[0] = fabsf(m_packed[0]);
        phaseOut[0] = m_packed[0] < 0.f ? float(M_PI) : 0.f;
        for (int i = 1; i < m_hs; ++i) {
            float re = m_packed[2 * i], im = m_packed[2 * i + 1];
            c_magphase(magOut + i, phaseOut + i, re, im);
        }
        magOut[m_hs] = fabsf(m_packed[1]);
        phaseOut[m_hs] = m_packed[1] < 0.f ? float(M_PI) : 0.f;
    }

    void forwardMagnitude(const float *BQ_R__ realIn, float *BQ_R__ magOut) {
        v_copy(m_buf, realIn, m_size);
        arm_rfft_fast_f32(&m_rfft, m_buf, m_packed, 0);
        magOut[0] = fabsf(m_packed[0]);
        for (int i = 1; i < m_hs; ++i) {
            float re = m_packed[2 * i], im = m_packed[2 * i + 1];
            magOut[i] = sqrtf(re * re + im * im);
        }
        magOut[m_hs] = fabsf(m_packed[1]);
    }

    void inverse(const float *BQ_R__ realIn, const float *BQ_R__ imagIn, float *BQ_R__ realOut) {
        packFloat(realIn, imagIn);
        arm_rfft_fast_f32(&m_rfft, m_packed, realOut, 1);
        unnormalise(realOut);
    }

    void inverseInterleaved(const float *BQ_R__ complexIn, float *BQ_R__ realOut) {
        m_packed[0] = complexIn[0];
        m_packed[1] = complexIn[2 * m_hs];
        for (int i = 1; i < m_hs; ++i) {
            m_packed[2 * i] = complexIn[2 * i];
            m_packed[2 * i + 1] = complexIn[2 * i + 1];
        }
        arm_rfft_fast_f32(&m_rfft, m_packed, realOut, 1);
        unnormalise(realOut);
    }

    void inversePolar(const float *BQ_R__ magIn, const float *BQ_R__ phaseIn, float *BQ_R__ realOut) {
        float re, im;
        c_phasor(&re, &im, phaseIn[0]);
        m_packed[0] = magIn[0] * re;
        c_phasor(&re, &im, phaseIn[m_hs]);
        m_packed[1] = magIn[m_hs] * re;
        for (int i = 1; i < m_hs; ++i) {
            c_phasor(&re, &im, phaseIn[i]);
            m_packed[2 * i] = magIn[i] * re;
            m_packed[2 * i + 1] = magIn[i] * im;
        }
        arm_rfft_fast_f32(&m_rfft, m_packed, realOut, 1);
        unnormalise(realOut);
    }

    void inverseCepstral(const float *BQ_R__ magIn, float *BQ_R__ cepOut) {
        m_packed[0] = logf(magIn[0] + 0.000001f);
        m_packed[1] = logf(magIn[m_hs] + 0.000001f);
        for (int i = 1; i < m_hs; ++i) {
            m_packed[2 * i] = logf(magIn[i] + 0.000001f);
            m_packed[2 * i + 1] = 0.f;
        }
        arm_rfft_fast_f32(&m_rfft, m_packed, cepOut, 1);
        unnormalise(cepOut);
    }

    /* Double paths: convert through float (R2 float build never calls these hot). */
    void forward(const double *BQ_R__ realIn, double *BQ_R__ realOut, double *BQ_R__ imagOut) {
        v_convert(m_buf, realIn, m_size);
        float *re = new float[m_hs + 1];
        float *im = new float[m_hs + 1];
        forward(m_buf, re, im);
        v_convert(realOut, re, m_hs + 1);
        v_convert(imagOut, im, m_hs + 1);
        delete[] re; delete[] im;
    }
    void forwardInterleaved(const double *BQ_R__ realIn, double *BQ_R__ complexOut) {
        v_convert(m_buf, realIn, m_size);
        float *tmp = new float[m_size + 2];
        forwardInterleaved(m_buf, tmp);
        v_convert(complexOut, tmp, m_size + 2);
        delete[] tmp;
    }
    void forwardPolar(const double *BQ_R__ realIn, double *BQ_R__ magOut, double *BQ_R__ phaseOut) {
        v_convert(m_buf, realIn, m_size);
        float *mag = new float[m_hs + 1];
        float *ph = new float[m_hs + 1];
        forwardPolar(m_buf, mag, ph);
        v_convert(magOut, mag, m_hs + 1);
        v_convert(phaseOut, ph, m_hs + 1);
        delete[] mag; delete[] ph;
    }
    void forwardMagnitude(const double *BQ_R__ realIn, double *BQ_R__ magOut) {
        v_convert(m_buf, realIn, m_size);
        float *mag = new float[m_hs + 1];
        forwardMagnitude(m_buf, mag);
        v_convert(magOut, mag, m_hs + 1);
        delete[] mag;
    }
    void inverse(const double *BQ_R__ realIn, const double *BQ_R__ imagIn, double *BQ_R__ realOut) {
        float *re = new float[m_hs + 1];
        float *im = new float[m_hs + 1];
        v_convert(re, realIn, m_hs + 1);
        v_convert(im, imagIn, m_hs + 1);
        inverse(re, im, m_buf);
        v_convert(realOut, m_buf, m_size);
        delete[] re; delete[] im;
    }
    void inverseInterleaved(const double *BQ_R__ complexIn, double *BQ_R__ realOut) {
        float *tmp = new float[m_size + 2];
        v_convert(tmp, complexIn, m_size + 2);
        inverseInterleaved(tmp, m_buf);
        v_convert(realOut, m_buf, m_size);
        delete[] tmp;
    }
    void inversePolar(const double *BQ_R__ magIn, const double *BQ_R__ phaseIn, double *BQ_R__ realOut) {
        float *mag = new float[m_hs + 1];
        float *ph = new float[m_hs + 1];
        v_convert(mag, magIn, m_hs + 1);
        v_convert(ph, phaseIn, m_hs + 1);
        inversePolar(mag, ph, m_buf);
        v_convert(realOut, m_buf, m_size);
        delete[] mag; delete[] ph;
    }
    void inverseCepstral(const double *BQ_R__ magIn, double *BQ_R__ cepOut) {
        float *mag = new float[m_hs + 1];
        v_convert(mag, magIn, m_hs + 1);
        inverseCepstral(mag, m_buf);
        v_convert(cepOut, m_buf, m_size);
        delete[] mag;
    }

private:
    const int m_size;
    const int m_hs;
    arm_rfft_fast_instance_f32 m_rfft;
    float *m_buf;
    float *m_packed;
};

#endif /* HAVE_CMSIS_DSP */
