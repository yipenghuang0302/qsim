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

from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

from cirq import (
  circuits,
  linalg,
  ops,
  protocols,
  sim,
  study,
  value,
  SimulatesAmplitudes,
  SimulatesFinalState,
  SimulatesSamples,
)

import numpy as np

from qsimcirq import qsim
import qsimcirq.qsim_circuit as qsimc


class QSimSimulatorState(sim.StateVectorSimulatorState):

    def __init__(self,
                 qsim_data: np.ndarray,
                 qubit_map: Dict[ops.Qid, int]):
      state_vector = qsim_data.view(np.complex64)
      super().__init__(state_vector=state_vector, qubit_map=qubit_map)


class QSimSimulatorTrialResult(sim.StateVectorTrialResult):

    def __init__(self,
                 params: study.ParamResolver,
                 measurements: Dict[str, np.ndarray],
                 final_simulator_state: QSimSimulatorState):
      super().__init__(params=params,
                       measurements=measurements,
                       final_simulator_state=final_simulator_state)


class QSimSimulator(SimulatesSamples, SimulatesAmplitudes, SimulatesFinalState):

  def __init__(self, qsim_options: dict = {},
               seed: value.RANDOM_STATE_OR_SEED_LIKE = None):
    if any(k in qsim_options for k in ('c', 'i')):
      raise ValueError(
          'Keys "c" & "i" are reserved for internal use and cannot be used in QSimCircuit instantiation.'
      )
    self._prng = value.parse_random_state(seed)
    self.qsim_options = {'t': 1, 'f': 2, 'v': 0}
    self.qsim_options.update(qsim_options)

  def get_seed(self):
    # Limit seed size to 32-bit integer for C++ conversion.
    return self._prng.randint(2 ** 31 - 1)

  def _run(
        self,
        circuit: circuits.Circuit,
        param_resolver: study.ParamResolver,
        repetitions: int
  ) -> Dict[str, np.ndarray]:
    """Run a simulation, mimicking quantum hardware.

    Args:
        program: The circuit to simulate.
        param_resolver: Parameters to run with the program.
        repetitions: Number of times to repeat the run.

    Returns:
        A dictionary from measurement gate key to measurement
        results.
    """
    param_resolver = param_resolver or study.ParamResolver({})
    solved_circuit = protocols.resolve_parameters(circuit, param_resolver)

    return self._sample_measure_results(solved_circuit, repetitions)

  def _sample_measure_results(
    self,
    program: circuits.Circuit,
    repetitions: int = 1,
  ) -> Dict[str, np.ndarray]:
    """Samples from measurement gates in the circuit.

    Note that this will execute the circuit 'repetitions' times.

    Args:
        program: The circuit to sample from.
        repetitions: The number of samples to take.

    Returns:
        A dictionary from measurement gate key to measurement
        results. Measurement results are stored in a 2-dimensional
        numpy array, the first dimension corresponding to the repetition
        and the second to the actual boolean measurement results (ordered
        by the qubits being measured.)

    Raises:
        ValueError: If there are multiple MeasurementGates with the same key,
            or if repetitions is negative.
    """
    if not isinstance(program, qsimc.QSimCircuit):
      program = qsimc.QSimCircuit(program, device=program.device)

    # Compute indices of measured qubits
    ordered_qubits = ops.QubitOrder.DEFAULT.order_for(program.all_qubits())

    qubit_map = {
      qubit: index for index, qubit in enumerate(ordered_qubits)
    }

    # Computes
    # - the list of qubits to be measured
    # - the start (inclusive) and end (exclusive) indices of each measurement
    # - a mapping from measurement key to measurement gate
    measurement_ops = [
      op for _, op, _ in program.findall_operations_with_gate_type(ops.MeasurementGate)
    ]
    measured_qubits = []  # type: List[ops.Qid]
    bounds = {}  # type: Dict[str, Tuple]
    meas_ops = {}  # type: Dict[str, cirq.GateOperation]
    current_index = 0
    for op in measurement_ops:
      gate = op.gate
      key = protocols.measurement_key(gate)
      meas_ops[key] = op
      if key in bounds:
        raise ValueError("Duplicate MeasurementGate with key {}".format(key))
      bounds[key] = (current_index, current_index + len(op.qubits))
      measured_qubits.extend(op.qubits)
      current_index += len(op.qubits)

    # Set qsim options
    options = {}
    options.update(self.qsim_options)

    results = {}
    for key, bound in bounds.items():
      results[key] = np.ndarray(shape=(repetitions, bound[1]-bound[0]),
                                dtype=int)


    if program.are_all_measurements_terminal() and repetitions > 1:
      print('Provided circuit has no intermediate measurements. ' +
            'Sampling repeatedly from final state vector.')
      # Measurements must be replaced with identity gates to sample properly.
      # Simply removing them may omit qubits from the circuit.
      for i in range(len(program.moments)):
        program.moments[i] = ops.Moment(
          op if not isinstance(op.gate, ops.MeasurementGate)
          else [ops.IdentityGate(1).on(q) for q in op.qubits]
          for op in program.moments[i]
        )
      options['c'] = program.translate_cirq_to_qsim(ops.QubitOrder.DEFAULT)
      options['s'] = self.get_seed()
      final_state = qsim.qsim_simulate_fullstate(options, 0)
      full_results = sim.sample_state_vector(
        final_state.view(np.complex64), range(len(ordered_qubits)),
        repetitions=repetitions, seed=self._prng)

      for i in range(repetitions):
        for key, op in meas_ops.items():
          meas_indices = [qubit_map[qubit] for qubit in op.qubits]
          for j, q in enumerate(meas_indices):
            results[key][i][j] = full_results[i][q]
    else:
      options['c'] = program.translate_cirq_to_qsim(ops.QubitOrder.DEFAULT)
      for i in range(repetitions):
        options['s'] = self.get_seed()
        measurements = qsim.qsim_sample(options)
        for key, bound in bounds.items():
          for j in range(bound[1]-bound[0]):
            results[key][i][j] = int(measurements[bound[0]+j])

    return results


  def compute_amplitudes_sweep(
      self,
      program: circuits.Circuit,
      bitstrings: Sequence[int],
      params: study.Sweepable,
      qubit_order: ops.QubitOrderOrList = ops.QubitOrder.DEFAULT,
  ) -> Sequence[Sequence[complex]]:
    """Computes the desired amplitudes using qsim.

      The initial state is assumed to be the all zeros state.

      Args:
          program: The circuit to simulate.
          bitstrings: The bitstrings whose amplitudes are desired, input as an
            string array where each string is formed from measured qubit values
            according to `qubit_order` from most to least significant qubit,
            i.e. in big-endian ordering.
          param_resolver: Parameters to run with the program.
          qubit_order: Determines the canonical ordering of the qubits. This is
            often used in specifying the initial state, i.e. the ordering of the
            computational basis states.

      Returns:
          List of amplitudes.
      """
    if not isinstance(program, qsimc.QSimCircuit):
      program = qsimc.QSimCircuit(program, device=program.device)

    n_qubits = len(program.all_qubits())
    # qsim numbers qubits in reverse order from cirq
    bitstrings = [format(bitstring, 'b').zfill(n_qubits)[::-1]
                  for bitstring in bitstrings]

    options = {'i': '\n'.join(bitstrings)}
    options.update(self.qsim_options)

    param_resolvers = study.to_resolvers(params)

    trials_results = []
    for prs in param_resolvers:

      solved_circuit = protocols.resolve_parameters(program, prs)

      options['c'] = solved_circuit.translate_cirq_to_qsim(qubit_order)
      options['s'] = self.get_seed()

      amplitudes = qsim.qsim_simulate(options)
      trials_results.append(amplitudes)

    return trials_results

  def simulate_sweep(
      self,
      program: circuits.Circuit,
      params: study.Sweepable,
      qubit_order: ops.QubitOrderOrList = ops.QubitOrder.DEFAULT,
      initial_state: Optional[Union[int, np.ndarray]] = None,
  ) -> List['SimulationTrialResult']:
    """Simulates the supplied Circuit.

      This method returns a result which allows access to the entire
      wave function. In contrast to simulate, this allows for sweeping
      over different parameter values.

      Args:
          program: The circuit to simulate.
          params: Parameters to run with the program.
          qubit_order: Determines the canonical ordering of the qubits. This is
            often used in specifying the initial state, i.e. the ordering of the
            computational basis states.
          initial_state: The initial state for the simulation. This can either
            be an integer representing a pure state (e.g. 11010) or a numpy
            array containing the full state vector. If none is provided, this
            is assumed to be the all-zeros state.

      Returns:
          List of SimulationTrialResults for this run, one for each
          possible parameter resolver.

      Raises:
          TypeError: if an invalid initial_state is provided.
      """
    if initial_state is None:
      initial_state = 0
    if not isinstance(initial_state, (int, np.ndarray)):
      raise TypeError('initial_state must be an int or state vector.')
    if not isinstance(program, qsimc.QSimCircuit):
      program = qsimc.QSimCircuit(program, device=program.device)

    options = {}
    options.update(self.qsim_options)

    param_resolvers = study.to_resolvers(params)
    num_qubits = len(program.all_qubits())
    if isinstance(initial_state, np.ndarray):
      if initial_state.dtype != np.complex64:
        raise TypeError(f'initial_state vector must have dtype np.complex64.')
      input_vector = initial_state.view(np.float32)
      if len(input_vector) != 2**num_qubits * 2:
        raise ValueError(f'initial_state vector size must match number of qubits.'
          f'Expected: {2**num_qubits * 2} Received: {len(input_vector)}')

    trials_results = []
    for prs in param_resolvers:
      solved_circuit = protocols.resolve_parameters(program, prs)

      options['c'] = solved_circuit.translate_cirq_to_qsim(qubit_order)
      options['s'] = self.get_seed()
      ordered_qubits = ops.QubitOrder.as_qubit_order(qubit_order).order_for(
        solved_circuit.all_qubits())
      # qsim numbers qubits in reverse order from cirq
      ordered_qubits = list(reversed(ordered_qubits))

      qubit_map = {
        qubit: index for index, qubit in enumerate(ordered_qubits)
      }

      if isinstance(initial_state, int):
        qsim_state = qsim.qsim_simulate_fullstate(options, initial_state)
      elif isinstance(initial_state, np.ndarray):
        qsim_state = qsim.qsim_simulate_fullstate(options, input_vector)
      assert qsim_state.dtype == np.float32
      assert qsim_state.ndim == 1
      final_state = QSimSimulatorState(qsim_state, qubit_map)
      # create result for this parameter
      # TODO: We need to support measurements.
      result = QSimSimulatorTrialResult(params=prs,
                                        measurements={},
                                        final_simulator_state=final_state)
      trials_results.append(result)

    return trials_results
