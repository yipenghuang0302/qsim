// Copyright 2019 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STATESPACE_AVX_H_
#define STATESPACE_AVX_H_

#include <immintrin.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>

#include "statespace.h"
#include "util.h"

namespace qsim {

namespace detail {

inline __m256i GetZeroMaskAVX(uint64_t i, uint64_t mask, uint64_t bits) {
  __m256i s1 = _mm256_setr_epi64x(i + 0, i + 2, i + 4, i + 6);
  __m256i s2 = _mm256_setr_epi64x(i + 1, i + 3, i + 5, i + 7);
  __m256i ma = _mm256_set1_epi64x(mask);
  __m256i bi = _mm256_set1_epi64x(bits);

  s1 = _mm256_and_si256(s1, ma);
  s2 = _mm256_and_si256(s2, ma);

  s1 = _mm256_cmpeq_epi64(s1, bi);
  s2 = _mm256_cmpeq_epi64(s2, bi);

  return _mm256_blend_epi32(s1, s2, 170);  // 10101010
}

inline double HorizontalSumAVX(__m256 s) {
  float buf[8];
  _mm256_storeu_ps(buf, s);
  return buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6] + buf[7];
}

}  // namespace detail

// Routines for state-vector manipulations.
// State is a vectorized sequence of eight real components followed by eight
// imaginary components. Eight single-precison floating numbers can be loaded
// into an AVX register.
template <typename For>
class StateSpaceAVX : public StateSpace<StateSpaceAVX<For>, For, float> {
 private:
  using Base = StateSpace<StateSpaceAVX<For>, For, float>;

 public:
  using State = typename Base::State;
  using fp_type = typename Base::fp_type;

  template <typename... ForArgs>
  explicit StateSpaceAVX(unsigned num_qubits, ForArgs&&... args)
      : Base(MinimumRawSize(2 * (uint64_t{1} << num_qubits)),
             num_qubits, args...) {}

  static uint64_t MinimumRawSize(uint64_t raw_size) {
    return std::max(uint64_t{16}, raw_size);
  }

  bool InternalToNormalOrder(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    if (Base::num_qubits_ == 1) {
      fp_type* s = state.get();

      s[2] = s[1];
      s[1] = s[8];
      s[3] = s[9];

      for (uint64_t i = 4; i < 16; ++i) {
        s[i] = 0;
      }
    } else if (Base::num_qubits_ == 2) {
      fp_type* s = state.get();

      s[6] = s[3];
      s[4] = s[2];
      s[2] = s[1];
      s[1] = s[8];
      s[3] = s[9];
      s[5] = s[10];
      s[7] = s[11];

      for (uint64_t i = 8; i < 16; ++i) {
        s[i] = 0;
      }
    } else {
      auto f = [](unsigned n, unsigned m, uint64_t i, fp_type* p) {
        fp_type* s = p + 16 * i;

        fp_type re[7];
        fp_type im[7];

        for (uint64_t i = 0; i < 7; ++i) {
          re[i] = s[i + 1];
          im[i] = s[i + 8];
        }

        for (uint64_t i = 0; i < 7; ++i) {
          s[2 * i + 1] = im[i];
          s[2 * i + 2] = re[i];
        }
      };

      Base::for_.Run(Base::raw_size_ / 16, f, state.get());
    }

    return true;
  }

  bool NormalToInternalOrder(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    if (Base::num_qubits_ == 1) {
      fp_type* s = state.get();

      s[8] = s[1];
      s[1] = s[2];
      s[9] = s[3];

      for (uint64_t i = 2; i < 8; ++i) {
        s[i] = 0;
        s[i + 8] = 0;
      }
    } else if (Base::num_qubits_ == 2) {
      fp_type* s = state.get();

      s[8] = s[1];
      s[9] = s[3];
      s[10] = s[5];
      s[11] = s[7];
      s[1] = s[2];
      s[2] = s[4];
      s[3] = s[6];

      for (uint64_t i = 4; i < 8; ++i) {
        s[i] = 0;
        s[i + 8] = 0;
      }
    } else {
      auto f = [](unsigned n, unsigned m, uint64_t i, fp_type* p) {
        fp_type* s = p + 16 * i;

        fp_type re[7];
        fp_type im[7];

        for (uint64_t i = 0; i < 7; ++i) {
          im[i] = s[2 * i + 1];
          re[i] = s[2 * i + 2];
        }

        for (uint64_t i = 0; i < 7; ++i) {
          s[i + 1] = re[i];
          s[i + 8] = im[i];
        }
      };

      Base::for_.Run(Base::raw_size_ / 16, f, state.get());
    }

    return true;
  }

  bool SetAllZeros(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m256 val0 = _mm256_setzero_ps();

    auto f = [](unsigned n, unsigned m, uint64_t i, __m256& val, fp_type* p) {
      _mm256_store_ps(p + 16 * i, val);
      _mm256_store_ps(p + 16 * i + 8, val);
    };

    Base::for_.Run(Base::raw_size_ / 16, f, val0, state.get());

    return true;
  }

  // Uniform superposition.
  bool SetStateUniform(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m256 val0 = _mm256_setzero_ps();
    __m256 valu;

    fp_type v = double{1} / std::sqrt(Base::Size());

    switch (Base::num_qubits_) {
    case 1:
      valu = _mm256_set_ps(0, 0, 0, 0, 0, 0, v, v);
      break;
    case 2:
      valu = _mm256_set_ps(0, 0, 0, 0, v, v, v, v);
      break;
    default:
      valu = _mm256_set1_ps(v);
      break;
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                __m256& val0, __m256 valu, fp_type* p) {
      _mm256_store_ps(p + 16 * i, valu);
      _mm256_store_ps(p + 16 * i + 8, val0);
    };

    Base::for_.Run(Base::raw_size_ / 16, f, val0, valu, state.get());

    return true;
  }

  // |0> state.
  bool SetStateZero(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    SetAllZeros(state);
    state.get()[0] = 1;

    return true;
  }

  static std::complex<fp_type> GetAmpl(const State& state, uint64_t i) {
    uint64_t k = (16 * (i / 8)) + (i % 8);
    return std::complex<fp_type>(state.get()[k], state.get()[k + 8]);
  }

  static void SetAmpl(
      State& state, uint64_t i, const std::complex<fp_type>& ampl) {
    uint64_t k = (16 * (i / 8)) + (i % 8);
    state.get()[k] = std::real(ampl);
    state.get()[k + 8] = std::imag(ampl);
  }

  static void SetAmpl(State& state, uint64_t i, fp_type re, fp_type im) {
    uint64_t k = (16 * (i / 8)) + (i % 8);
    state.get()[k] = re;
    state.get()[k + 8] = im;
  }

  // Does the equivalent of dest += src elementwise.
  bool AddState(const State& src, State& dest) const {
    if (src.size() != Base::raw_size_ || dest.size() != Base::raw_size_) {
      return false;
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, fp_type* p2) {
      __m256 re1 = _mm256_load_ps(p1 + 16 * i);
      __m256 im1 = _mm256_load_ps(p1 + 16 * i + 8);
      __m256 re2 = _mm256_load_ps(p2 + 16 * i);
      __m256 im2 = _mm256_load_ps(p2 + 16 * i + 8);

      _mm256_store_ps(p2 + 16 * i, _mm256_add_ps(re1, re2));
      _mm256_store_ps(p2 + 16 * i + 8, _mm256_add_ps(im1, im2));
    };

    Base::for_.Run(Base::raw_size_ / 16, f, src.get(), dest.get());

    return true;
  }

  // Does the equivalent of state *= a elementwise.
  bool Multiply(fp_type a, State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m256 r = _mm256_set1_ps(a);

    auto f = [](unsigned n, unsigned m, uint64_t i, __m256 r, fp_type* p) {
      __m256 re = _mm256_load_ps(p + 16 * i);
      __m256 im = _mm256_load_ps(p + 16 * i + 8);

      re = _mm256_mul_ps(re, r);
      im = _mm256_mul_ps(im, r);

      _mm256_store_ps(p + 16 * i, re);
      _mm256_store_ps(p + 16 * i + 8, im);
    };

    Base::for_.Run(Base::raw_size_ / 16, f, r, state.get());

    return true;
  }

  std::complex<double> InnerProduct(
      const State& state1, const State& state2) const {
    if (state1.size() != Base::raw_size_ || state2.size() != Base::raw_size_) {
      return std::nan("");
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, const fp_type* p2) -> std::complex<double> {
      __m256 re1 = _mm256_load_ps(p1 + 16 * i);
      __m256 im1 = _mm256_load_ps(p1 + 16 * i + 8);
      __m256 re2 = _mm256_load_ps(p2 + 16 * i);
      __m256 im2 = _mm256_load_ps(p2 + 16 * i + 8);

      __m256 ip_re = _mm256_fmadd_ps(im1, im2, _mm256_mul_ps(re1, re2));
      __m256 ip_im = _mm256_fnmadd_ps(im1, re2, _mm256_mul_ps(re1, im2));

      double re = detail::HorizontalSumAVX(ip_re);
      double im = detail::HorizontalSumAVX(ip_im);

      return std::complex<double>{re, im};
    };

    using Op = std::plus<std::complex<double>>;
    return Base::for_.RunReduce(Base::raw_size_ / 16, f,
                                Op(), state1.get(), state2.get());
  }

  double RealInnerProduct(const State& state1, const State& state2) const {
    if (state1.size() != Base::raw_size_ || state2.size() != Base::raw_size_) {
      return std::nan("");
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, const fp_type* p2) -> double {
      __m256 re1 = _mm256_load_ps(p1 + 16 * i);
      __m256 im1 = _mm256_load_ps(p1 + 16 * i + 8);
      __m256 re2 = _mm256_load_ps(p2 + 16 * i);
      __m256 im2 = _mm256_load_ps(p2 + 16 * i + 8);

      __m256 ip_re = _mm256_fmadd_ps(im1, im2, _mm256_mul_ps(re1, re2));

      return detail::HorizontalSumAVX(ip_re);
    };

    using Op = std::plus<double>;
    return Base::for_.RunReduce(Base::raw_size_ / 16, f,
                                Op(), state1.get(), state2.get());
  }

  template <typename DistrRealType = double>
  std::vector<uint64_t> Sample(
      const State& state, uint64_t num_samples, unsigned seed) const {
    std::vector<uint64_t> bitstrings;

    if (state.size() == Base::raw_size_ && num_samples > 0) {
      double norm = 0;
      uint64_t size = Base::raw_size_ / 16;
      const fp_type* p = state.get();

      for (uint64_t k = 0; k < size; ++k) {
        for (unsigned j = 0; j < 8; ++j) {
          auto re = p[16 * k + j];
          auto im = p[16 * k + 8 + j];
          norm += re * re + im * im;
        }
      }

      auto rs = GenerateRandomValues<DistrRealType>(num_samples, seed, norm);

      uint64_t m = 0;
      double csum = 0;
      bitstrings.reserve(num_samples);

      for (uint64_t k = 0; k < size; ++k) {
        for (unsigned j = 0; j < 8; ++j) {
          auto re = p[16 * k + j];
          auto im = p[16 * k + 8 + j];
          csum += re * re + im * im;
          while (rs[m] < csum && m < num_samples) {
            bitstrings.emplace_back(8 * k + j);
            ++m;
          }
        }
      }
    }

    return bitstrings;
  }

  using MeasurementResult = typename Base::MeasurementResult;

  bool CollapseState(const MeasurementResult& mr, State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    auto f1 = [](unsigned n, unsigned m, uint64_t i,
                 uint64_t mask, uint64_t bits, const fp_type* p) -> double {
      __m256i ml = detail::GetZeroMaskAVX(8 * i, mask, bits);

      __m256 re = _mm256_maskload_ps(p + 16 * i, ml);
      __m256 im = _mm256_maskload_ps(p + 16 * i + 8, ml);
      __m256 s1 = _mm256_fmadd_ps(im, im, _mm256_mul_ps(re, re));

      return detail::HorizontalSumAVX(s1);
    };

    using Op = std::plus<double>;
    double norm = Base::for_.RunReduce(Base::raw_size_ / 16, f1,
                                       Op(), mr.mask, mr.bits, state.get());

    __m256 renorm = _mm256_set1_ps(1.0 / std::sqrt(norm));

    auto f2 = [](unsigned n, unsigned m, uint64_t i,
                 uint64_t mask, uint64_t bits, __m256 renorm, fp_type* p) {
      __m256i ml = detail::GetZeroMaskAVX(8 * i, mask, bits);

      __m256 re = _mm256_maskload_ps(p + 16 * i, ml);
      __m256 im = _mm256_maskload_ps(p + 16 * i + 8, ml);

      re = _mm256_mul_ps(re, renorm);
      im = _mm256_mul_ps(im, renorm);

      _mm256_store_ps(p + 16 * i, re);
      _mm256_store_ps(p + 16 * i + 8, im);
    };

    Base::for_.Run(
        Base::raw_size_ / 16, f2, mr.mask, mr.bits, renorm, state.get());

    return true;
  }

  std::vector<double> PartialNorms(const State& state) const {
    if (state.size() != Base::raw_size_) {
      return {};
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p) -> double {
      __m256 re = _mm256_load_ps(p + 16 * i);
      __m256 im = _mm256_load_ps(p + 16 * i + 8);
      __m256 s1 = _mm256_fmadd_ps(im, im, _mm256_mul_ps(re, re));

      return detail::HorizontalSumAVX(s1);
    };

    using Op = std::plus<double>;
    return Base::for_.RunReduceP(Base::raw_size_ / 16, f, Op(), state.get());
  }

  uint64_t FindMeasuredBits(
      unsigned m, double r, uint64_t mask, const State& state) const {
    if (state.size() != Base::raw_size_) {
      return -1;
    }

    double csum = 0;

    uint64_t k0 = Base::for_.GetIndex0(Base::raw_size_ / 16, m);
    uint64_t k1 = Base::for_.GetIndex1(Base::raw_size_ / 16, m);

    const fp_type* p = state.get();

    for (uint64_t k = k0; k < k1; ++k) {
      for (uint64_t j = 0; j < 8; ++j) {
        auto re = p[16 * k + j];
        auto im = p[16 * k + j + 8];
        csum += re * re + im * im;
        if (r < csum) {
          return (8 * k + j) & mask;
        }
      }
    }

    return -1;
  }
};

}  // namespace qsim

#endif  // STATESPACE_AVX_H_
