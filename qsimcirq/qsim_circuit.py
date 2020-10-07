# Copyright 2019 Google LLC. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import numpy as np

import cirq
from qsimcirq import qsim


def _cirq_gate_kind(gate):
  if isinstance(gate, cirq.ops.identity.IdentityGate):
    if gate.num_qubits() == 1:
      return qsim.kI
    if gate.num_qubits() == 2:
      return qsim.kI2
    raise NotImplementedError(
      f'Received identity on {gate.num_qubits()} qubits; '
      + 'only 1- or 2-qubit gates are supported.')
  if isinstance(gate, cirq.ops.XPowGate):
    # cirq.rx also uses this path.
    if gate.exponent == 1:
      return qsim.kX
    return qsim.kXPowGate
  if isinstance(gate, cirq.ops.YPowGate):
    # cirq.ry also uses this path.
    if gate.exponent == 1:
      return qsim.kY
    return qsim.kYPowGate
  if isinstance(gate, cirq.ops.ZPowGate):
    # cirq.rz also uses this path.
    if gate.exponent == 1:
      return qsim.kZ
    if gate.exponent == 0.5:
      return qsim.kS
    if gate.exponent == 0.25:
      return qsim.kT
    return qsim.kZPowGate
  if isinstance(gate, cirq.ops.HPowGate):
    if gate.exponent == 1:
      return qsim.kH
    return qsim.kHPowGate
  if isinstance(gate, cirq.ops.CZPowGate):
    if gate.exponent == 1:
      return qsim.kCZ
    return qsim.kCZPowGate
  if isinstance(gate, cirq.ops.CXPowGate):
    if gate.exponent == 1:
      return qsim.kCX
    return qsim.kCXPowGate
  if isinstance(gate, cirq.ops.PhasedXPowGate):
    return qsim.kPhasedXPowGate
  if isinstance(gate, cirq.ops.PhasedXZGate):
    return qsim.kPhasedXZGate
  if isinstance(gate, cirq.ops.XXPowGate):
    if gate.exponent == 1:
      return qsim.kXX
    return qsim.kXXPowGate
  if isinstance(gate, cirq.ops.YYPowGate):
    if gate.exponent == 1:
      return qsim.kYY
    return qsim.kYYPowGate
  if isinstance(gate, cirq.ops.ZZPowGate):
    if gate.exponent == 1:
      return qsim.kZZ
    return qsim.kZZPowGate
  if isinstance(gate, cirq.ops.SwapPowGate):
    if gate.exponent == 1:
      return qsim.kSWAP
    return qsim.kSwapPowGate
  if isinstance(gate, cirq.ops.ISwapPowGate):
    # cirq.riswap also uses this path.
    if gate.exponent == 1:
      return qsim.kISWAP
    return qsim.kISwapPowGate
  if isinstance(gate, cirq.ops.PhasedISwapPowGate):
    # cirq.givens also uses this path.
    return qsim.kPhasedISwapPowGate
  if isinstance(gate, cirq.ops.FSimGate):
    return qsim.kFSimGate
  if isinstance(gate, cirq.ops.MatrixGate):
    if gate.num_qubits() == 1:
      return qsim.kMatrixGate1
    if gate.num_qubits() == 2:
      return qsim.kMatrixGate2
    raise NotImplementedError(
      f'Received matrix on {gate.num_qubits()} qubits; '
      + 'only 1- or 2-qubit gates are supported.')
  if isinstance(gate, cirq.ops.MeasurementGate):
    # needed to inherit SimulatesSamples in sims
    return qsim.kMeasurement
  # Unrecognized gates will be decomposed.
  return None


class QSimCircuit(cirq.Circuit):

  def __init__(self,
               cirq_circuit: cirq.Circuit,
               device: cirq.devices = cirq.devices.UNCONSTRAINED_DEVICE,
               allow_decomposition: bool = False):

    if allow_decomposition:
      super().__init__([], device=device)
      for moment in cirq_circuit:
        for op in moment:
          # This should call decompose on the gates
          self.append(op)
    else:
      super().__init__(cirq_circuit, device=device)

  def __eq__(self, other):
    if not isinstance(other, QSimCircuit):
      return False
    # equality is tested, for the moment, for cirq.Circuit
    return super().__eq__(other)

  def _resolve_parameters_(self, param_resolver: cirq.study.ParamResolver):
    return QSimCircuit(
      super()._resolve_parameters_(param_resolver), device=self.device)

  def translate_cirq_to_qsim(
      self,
      qubit_order: cirq.ops.QubitOrderOrList = cirq.ops.QubitOrder.DEFAULT
  ) -> qsim.Circuit:
    """
        Translates this Cirq circuit to the qsim representation.
        :qubit_order: Ordering of qubits
        :return: a C++ qsim Circuit object
        """

    qsim_circuit = qsim.Circuit()
    qsim_circuit.num_qubits = len(self.all_qubits())
    ordered_qubits = cirq.ops.QubitOrder.as_qubit_order(qubit_order).order_for(
        self.all_qubits())

    # qsim numbers qubits in reverse order from cirq
    ordered_qubits = list(reversed(ordered_qubits))

    qubit_to_index_dict = {q: i for i, q in enumerate(ordered_qubits)}
    time_offset = 0
    for moment in self:
# <<<<<<< HEAD
#       moment_length = 1
#       for op in moment:
#         if isinstance(op.gate, cirq.ops.MeasurementGate):
#           continue
#         qsim_ops = cirq.decompose(
#           op, keep=lambda x: _cirq_gate_kind(x.gate) != None)
#         moment_length = max(moment_length, len(qsim_ops))
#
#         for gi, qsim_op in enumerate(qsim_ops):
# =======
      ops_by_gate = [
        cirq.decompose(op, keep=lambda x: _cirq_gate_kind(x.gate) != None)
        for op in moment
      ]
      moment_length = max(len(gate_ops) for gate_ops in ops_by_gate)

      # Gates must be added in time order.
      for gi in range(moment_length):
        for gate_ops in ops_by_gate:
          if gi >= len(gate_ops):
            continue
          qsim_op = gate_ops[gi]
# >>>>>>> upstream/master
          gate_kind = _cirq_gate_kind(qsim_op.gate)
          time = time_offset + gi
          qubits = [qubit_to_index_dict[q] for q in qsim_op.qubits]
          params = {
            p.strip('_'): val for p, val in vars(qsim_op.gate).items()
            if isinstance(val, float)
          }
          if gate_kind == qsim.kMatrixGate1:
            qsim.add_matrix1(time, qubits,
                             cirq.unitary(qsim_op.gate).tolist(),
                             qsim_circuit)
          elif gate_kind == qsim.kMatrixGate2:
            qsim.add_matrix2(time, qubits,
                             cirq.unitary(qsim_op.gate).tolist(),
                             qsim_circuit)
          else:
            qsim.add_gate(gate_kind, time, qubits, params, qsim_circuit)
      time_offset += moment_length

# <<<<<<< HEAD
#         qub_str = ""
#         for qub in op.qubits:
#           qub_str += "{} ".format(qubit_to_index_dict[qub])
#
#         qsim_gate = ""
#         qsim_params = ""
#         if isinstance(op.gate, cirq.ops.HPowGate)\
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "h"
#         elif isinstance(op.gate, cirq.ops.ZPowGate) \
#                 and op.gate.exponent == 0.25:
#           qsim_gate = "t"
#         elif isinstance(op.gate, cirq.ops.XPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "x"
#         elif isinstance(op.gate, cirq.ops.YPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "y"
#         elif isinstance(op.gate, cirq.ops.ZPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "z"
#         elif isinstance(op.gate, cirq.ops.XPowGate) \
#                 and op.gate.exponent == 0.5:
#           qsim_gate = "x_1_2"
#         elif isinstance(op.gate, cirq.ops.YPowGate) \
#                 and op.gate.exponent == 0.5:
#           qsim_gate = "y_1_2"
#         elif isinstance(op.gate, cirq.ops.XPowGate):
#           qsim_gate = "rx"
#           qsim_params = str(op.gate.exponent*np.pi)
#         elif isinstance(op.gate, cirq.ops.YPowGate):
#           qsim_gate = "ry"
#           qsim_params = str(op.gate.exponent*np.pi)
#         elif isinstance(op.gate, cirq.ops.ZPowGate):
#           qsim_gate = "rz"
#           qsim_params = str(op.gate.exponent*np.pi)
#         elif isinstance(op.gate, cirq.ops.CZPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "cz"
#         elif isinstance(op.gate, cirq.ops.CNotPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "cnot"
#         elif isinstance(op.gate, cirq.ops.ISwapPowGate) \
#                 and op.gate.exponent == 1.0:
#           qsim_gate = "is"
#         elif isinstance(op.gate, cirq.ops.CZPowGate):
#           qsim_gate = "cp"
#           qsim_params = str(op.gate.exponent*np.pi)
#         elif isinstance(op.gate, cirq.ops.FSimGate):
#           qsim_gate = "fs"
#           qsim_params = "{} {}".format(op.gate.theta, op.gate.phi)
#         elif isinstance(op.gate, cirq.ops.identity.IdentityGate) \
#                 and op.gate.num_qubits() == 1:
#           qsim_gate = "id1"
#         elif isinstance(op.gate, cirq.ops.identity.IdentityGate) \
#                 and op.gate.num_qubits() == 2:
#           qsim_gate = "id2"
#         elif isinstance(op.gate, cirq.ops.MeasurementGate):
#           continue
#         else:
#           raise ValueError("{!r} No translation for ".format(op))
#
#         # The moment is missing
#         qsim_gate = "{} {} {} {}".format(mi, qsim_gate, qub_str.strip(),
#                                          qsim_params.strip())
#         circuit_data.append(qsim_gate.strip())
#
#     circuit_data.insert(0, str(len(ordered_qubits)))
#     return "\n".join(circuit_data)
# =======
    return qsim_circuit
# >>>>>>> upstream/master
