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

#ifndef FUSER_BASIC_H_
#define FUSER_BASIC_H_

#include <map>
#include <utility>
#include <vector>

#include "gate.h"
#include "fuser.h"

namespace qsim {

template <typename IO, typename Gate>
struct BasicGateFuser final {
  using GateFused = qsim::GateFused<Gate>;

  /**
   * Stores ordered sets of gates, each acting on two qubits, that can be
   * applied together. Note that gates fused with this method are not
   * multiplied together until ApplyFusedGate is called on the output.
   * To respect specific time boundaries while fusing gates, use the other
   * version of this method below.
   * @param num_qubits The number of qubits acted on by 'gates'.
   * @param gates The gates to be fused.
   * @return A vector of fused gate objects. Each element is a set of gates
   *   acting on a specific pair of qubits which can be applied as a group.
   */
  static std::vector<GateFused> FuseGates(
      unsigned num_qubits, const std::vector<Gate>& gates) {
    return FuseGates(num_qubits, gates.cbegin(), gates.cend(), {});
  }

  /**
   * Stores ordered sets of gates, each acting on two qubits, that can be
   * applied together. Note that gates fused with this method are not
   * multiplied together until ApplyFusedGate is called on the output.
   * @param num_qubits The number of qubits acted on by 'gates'.
   * @param gates The gates to be fused. Gate times should be ordered.
   * @param times_to_split_at Ordered list of time steps at which to separate
   *   fused gates. Each element of the output will contain gates from a single
   *   'window' in this list.
   * @return A vector of fused gate objects. Each element is a set of gates
   *   acting on a specific pair of qubits which can be applied as a group.
   */
  static std::vector<GateFused> FuseGates(
      unsigned num_qubits, const std::vector<Gate>& gates,
      const std::vector<unsigned>& times_to_split_at) {
    return
        FuseGates(num_qubits, gates.cbegin(), gates.cend(), times_to_split_at);
  }

  /**
   * Stores ordered sets of gates, each acting on two qubits, that can be
   * applied together. Note that gates fused with this method are not
   * multiplied together until ApplyFusedGate is called on the output.
   * To respect specific time boundaries while fusing gates, use the other
   * version of this method below.
   * @param num_qubits The number of qubits acted on by gates.
   * @param gfirst, glast The iterator range [gfirst, glast) to fuse gates in.
   *   Gate times should be ordered.
   * @return A vector of fused gate objects. Each element is a set of gates
   *   acting on a specific pair of qubits which can be applied as a group.
   */
  static std::vector<GateFused> FuseGates(
      unsigned num_qubits,
      typename std::vector<Gate>::const_iterator gfirst,
      typename std::vector<Gate>::const_iterator glast) {
    return FuseGates(num_qubits, gfirst, glast, {});
  }

  /**
   * Stores ordered sets of gates, each acting on two qubits, that can be
   * applied together. Note that gates fused with this method are not
   * multiplied together until ApplyFusedGate is called on the output.
   * @param num_qubits The number of qubits acted on by gates.
   * @param gfirst, glast The iterator range [gfirst, glast) to fuse gates in.
   *   Gate times should be ordered.
   * @param times_to_split_at Ordered list of time steps at which to separate
   *   fused gates. Each element of the output will contain gates from a single
   *   'window' in this list.
   * @return A vector of fused gate objects. Each element is a set of gates
   *   acting on a specific pair of qubits which can be applied as a group.
   */
  static std::vector<GateFused> FuseGates(
      unsigned num_qubits,
      typename std::vector<Gate>::const_iterator gfirst,
      typename std::vector<Gate>::const_iterator glast,
      const std::vector<unsigned>& times_to_split_at) {
    std::vector<GateFused> gates_fused;

    if (gfirst >= glast) return gates_fused;

    std::size_t num_gates = glast - gfirst;

    gates_fused.reserve(num_gates);

    // Merge with measurement gate times to separate fused gates at.
    auto times = MergeWithMeasurementTimes(gfirst, glast, times_to_split_at);

    // Map to keep track of measurement gates with equal times.
    std::map<unsigned, std::vector<const Gate*>> measurement_gates;

    // Sequence of top level gates the other gates get fused to.
    std::vector<const Gate*> gates_seq;

    // Lattice of gates: qubits "hyperplane" and time direction.
    std::vector<std::vector<const Gate*>> gates_lat(num_qubits);

    // Current unfused gate.
    auto gate_it = gfirst;

    for (std::size_t l = 0; l < times.size(); ++l) {
      gates_seq.resize(0);
      gates_seq.reserve(num_gates);

      for (unsigned k = 0; k < num_qubits; ++k) {
        gates_lat[k].resize(0);
        gates_lat[k].reserve(128);
      }

      auto prev_time = gate_it->time;

      // Fill gates_seq and gates_lat in.
      for (; gate_it < glast; ++gate_it) {
        const auto& gate = *gate_it;

        if (gate.time > times[l]) break;

        if (gate.time < prev_time) {
          // This function assumes that gate times are ordered.
          // Just stop silently if this is not the case.
          IO::errorf("gate times should be ordered.\n");
          gates_fused.resize(0);
          return gates_fused;
        }

        prev_time = gate.time;

        if (gate.kind == gate::kMeasurement) {
          auto& mea_gates_at_time = measurement_gates[gate.time];
          if (mea_gates_at_time.size() == 0) {
            gates_seq.push_back(&gate);
            mea_gates_at_time.reserve(num_qubits);
          }

          mea_gates_at_time.push_back(&gate);
        } else if (gate.num_qubits == 1) {
          gates_lat[gate.qubits[0]].push_back(&gate);
          if (gate.unfusible) {
            gates_seq.push_back(&gate);
          }
        } else if (gate.num_qubits == 2) {
          gates_lat[gate.qubits[0]].push_back(&gate);
          gates_lat[gate.qubits[1]].push_back(&gate);
          gates_seq.push_back(&gate);
        }
      }

      std::vector<unsigned> last(num_qubits, 0);

      const Gate* delayed_measurement_gate = nullptr;

      // Fuse gates.
      for (auto pgate : gates_seq) {
        if (pgate->kind == gate::kMeasurement) {
          delayed_measurement_gate = pgate;
        } else if (pgate->num_qubits == 1) {
          unsigned q0 = pgate->qubits[0];

          GateFused gate_f = {pgate->kind, pgate->time, 1, {q0}, pgate, {}};

          last[q0] = Advance(last[q0], gates_lat[q0], gate_f.gates);
          gate_f.gates.push_back(gates_lat[q0][last[q0]]);
          last[q0] = Advance(last[q0] + 1, gates_lat[q0], gate_f.gates);

          gates_fused.push_back(std::move(gate_f));
        } else if (pgate->num_qubits == 2) {
          unsigned q0 = pgate->qubits[0];
          unsigned q1 = pgate->qubits[1];

          if (Done(last[q0], pgate->time, gates_lat[q0])) continue;

          GateFused gate_f = {pgate->kind, pgate->time, 2, {q0, q1}, pgate, {}};

          do {
            last[q0] = Advance(last[q0], gates_lat[q0], gate_f.gates);
            last[q1] = Advance(last[q1], gates_lat[q1], gate_f.gates);
            // Here gates_lat[q0][last[q0]] == gates_lat[q1][last[q1]].

            gate_f.gates.push_back(gates_lat[q0][last[q0]]);

            last[q0] = Advance(last[q0] + 1, gates_lat[q0], gate_f.gates);
            last[q1] = Advance(last[q1] + 1, gates_lat[q1], gate_f.gates);
          } while (NextGate(last[q0], gates_lat[q0], last[q1], gates_lat[q1]));

          gates_fused.push_back(std::move(gate_f));
        }
      }

      for (unsigned q = 0; q < num_qubits; ++q) {
        auto l = last[q];
        if (l == gates_lat[q].size()) continue;

        // Orphaned qubit.

        auto pgate = gates_lat[q][l];

        GateFused gate_f = {pgate->kind, pgate->time, 1, {q}, pgate, {}};
        gate_f.gates.push_back(gates_lat[q][l]);

        l = Advance(l + 1, gates_lat[q], gate_f.gates);
        // Here l == gates_lat[q].size().

        gates_fused.push_back(std::move(gate_f));
      }

      if (delayed_measurement_gate != nullptr) {
        auto pgate = delayed_measurement_gate;

        const auto& mea_gates_at_time = measurement_gates[pgate->time];

        GateFused gate_f = {pgate->kind, pgate->time, 0, {}, pgate, {}};

        // Fuse measurement gates with equal times.

        for (const auto* pgate : mea_gates_at_time) {
          gate_f.num_qubits += pgate->num_qubits;
          gate_f.qubits.insert(gate_f.qubits.end(),
                               pgate->qubits.begin(), pgate->qubits.end());
        }

        gates_fused.push_back(std::move(gate_f));
      }

      if (gate_it == glast) break;
    }

    return gates_fused;
  }

 private:
  static std::vector<unsigned> MergeWithMeasurementTimes(
      typename std::vector<Gate>::const_iterator gfirst,
      typename std::vector<Gate>::const_iterator glast,
      const std::vector<unsigned>& times) {
    std::vector<unsigned> times2;
    times2.reserve(glast - gfirst + times.size());

    std::size_t last = 0;

    for (auto gate_it = gfirst; gate_it < glast; ++gate_it) {
      const auto& gate = *gate_it;

      if (gate.kind == gate::kMeasurement
          && (times2.size() == 0 || times2.back() < gate.time)) {
        times2.push_back(gate.time);
      }

      if (last < times.size() && gate.time > times[last]) {
        while (last < times.size() && times[last] <= gate.time) {
          unsigned prev = times[last++];
          times2.push_back(prev);
          while (last < times.size() && times[last] <= prev) ++last;
        }
      }
    }

    if (times2.size() == 0 || times2.back() < (glast - 1)->time) {
      times2.push_back((glast - 1)->time);
    }

    return times2;
  }

  static unsigned Advance(unsigned k, const std::vector<const Gate*>& wl,
                          std::vector<const Gate*>& gates) {
    while (k < wl.size() && wl[k]->num_qubits == 1 && !wl[k]->unfusible) {
      gates.push_back(wl[k++]);
    }

    return k;
  }

  static bool Done(
      unsigned k, unsigned t, const std::vector<const Gate*>& wl) {
    return k >= wl.size() || wl[k]->time > t;
  }

  static bool NextGate(unsigned k1, const std::vector<const Gate*>& wl1,
                       unsigned k2, const std::vector<const Gate*>& wl2) {
    return k1 < wl1.size() && k2 < wl2.size() && wl1[k1] == wl2[k2];
  }
};

}  // namespace qsim

#endif  // FUSER_BASIC_H_
