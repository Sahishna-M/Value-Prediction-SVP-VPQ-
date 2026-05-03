**Overview**
This project implements a Stride Value Predictor (SVP) with a Value Prediction Queue (VPQ) inside a cycle-accurate out-of-order superscalar
processor simulator (721sim). Value prediction breaks true data dependencies by predicting the output of an instruction before it executes, 
allowing dependent instructions to issue immediately without waiting for the producer to complete.
The project also includes a research and competition phase where we analyzed structural hazards, characterized VR-1 recovery overhead across
15 SPEC CPU benchmarks, and designed and partially implemented a VR-5 checkpoint-based recovery mechanism to address the high cost of VR-1's 
head-of-Active-List drain policy.

**What Was Implemented**
**Phase 1: SVP + VPQ**

**Eligibility and prediction injection (Rename stage)**
An instruction is eligible for value prediction if it has a valid destination register and belongs to a configured instruction class (integer ALU, floating point ALU, or load). At rename, the SVP is queried using the instruction PC. If a confident prediction exists, the predicted value is written directly into the Physical Register File and the destination register's ready bit is set to true. This breaks the data dependency so that consumer instructions can issue immediately from the Issue Queue without waiting for the producer to execute.

**Stride Value Predictor (SVP)**
The SVP is a table indexed by PC bits. Each entry stores a tag, a confidence counter, a retired value, a stride, and an instance counter. A prediction is generated as retired value + instance x stride. Confidence is set when the tag matches and the confidence counter is at its maximum. Signed 64-bit integers are used for stride computation. The number of entries, tag bits, and confidence threshold are all configurable via command-line arguments.

**Value Prediction Queue (VPQ)**
The VPQ is a FIFO queue that tracks all in-flight value-predicted instructions. It is used to train the SVP in-order and non-speculatively at retirement, to initialize instance counters on SVP entry replacement, and to repair instance counters during rollbacks caused by branch mispredictions, load violations, exceptions, and value mispredictions. The Rename stage stalls if there are not enough free VPQ entries for all eligible instructions in the rename bundle.

**Misprediction detection and recovery (VR-1)**
When a value-predicted instruction executes, its computed value is compared against its predicted value. If they differ, a value misprediction flag is posted in the Active List. When the mispredicted instruction reaches the head of the Active List, VR-1 recovery is triggered: the instruction retires and a full pipeline squash is performed. The VPQ repairs all instance counters during the backward walk.

**Prediction outcome measurements**
Retired instructions are classified into: ineligible, no prediction available (SVP miss), confident and correct, confident and incorrect, unconfident and correct, and unconfident and incorrect. Storage cost of the SVP is computed and printed accurately based on configuration.

**Modes supported**
Perfect value prediction mode uses the functional simulator's known-correct value as the prediction. Oracle confidence mode uses the SVP's real prediction but overrides confidence using the functional simulator to discard all incorrect predictions. Real value prediction with real confidence is the production mode used for all competition runs.
Command-line interface
--vp-svp=<VPQsize>,<oracleconf>,<#indexbits>,<#tagbits>,<confmax>
--vp-eligible=<predINTALU>,<predFPALU>,<predLOAD>

**Phase 2: Competition Research**
**Structural hazard analysis**
Cycle-accurate stall counters were added to isolate stalls caused by insufficient Active List, Issue Queue, Load/Store Queue, and VPQ capacity. Experiments were run across 15 SPEC CPU benchmarks spanning integer, floating point, and memory-intensive workloads (approximately 105 simulation runs). Key finding: LSQ and VPQ stalls are negligible. The Active List is the primary bottleneck.

**AL and IQ design space exploration**
Empirical sweeps over AL and IQ sizes identified the point of diminishing returns. Doubling the AL from 256 to 512 entries improved IPC by approximately 14 to 18%, with the best observed configuration (AL=512, IQ=256) reaching an IPC of 2.396. Increasing IQ size alone from 64 to 256 entries slightly hurt IPC (2.02 to 1.967), indicating scheduling overhead outweighs dispatch benefit beyond a threshold. This mirrors Amdahl's Law: beyond a certain structural threshold, the remaining excess cycles are compulsory latencies inherent to value prediction that structural scaling cannot eliminate.

**VR-1 recovery overhead characterization**
VR-1 stall cycles and squashed instruction counts were measured per benchmark. Key findings:

The average squash of approximately 255 to 256 instructions per recovery event directly mirrors the baseline AL size of 256, 
confirming that VR-1 waits until the entire AL is drained before recovering regardless of misprediction severity.

**VR-5 checkpoint-based recovery design and implementation**
To address VR-1's fundamental inefficiency, a VR-5 early squash recovery mechanism was designed and implemented. VR-5 allocates a checkpoint at prediction time (Rename stage) and recovers at the Execute stage, rolling back only instructions younger than the mispredicted instruction rather than draining the entire Active List.
The 9-step VR-5 recovery sequence implemented in 721sim:

1. Redirect Fetch by rolling the Branch Queue back to the last-good branch
2. Mark the value-predicted instruction complete in the Active List before resolve() is called
3. Restore the Renamer: RMT, Free List head, Global Branch Mask, and AL tail pointer
4. Restore the Load/Store Unit tail pointers from the checkpoint snapshot
5. Repair the VPQ via backward walk, removing younger entries and decrementing SVP instance counters
6. Squash the backend by clearing the checkpoint bit from all pipeline stage registers
7. Rollback the payload buffer for all instructions younger than the mispredicted instruction
8. Reset SVP confidence for the mispredicting PC to zero
9. Set recovery coordination flags across pipeline stages

**Cascading recovery problem and solution**
A key challenge encountered was cascading VR-5 recoveries: multiple value-predicted instructions executing simultaneously across different lanes could each mispredict and fire VR-5 in successive cycles, causing the second recovery to overwrite the RMT, Free List, and Global Branch Mask restored by the first, resulting in register mapping corruption and pipeline deadlock. The solution implemented was a vr5_active guard combined with a VR-1 fallback: if a second misprediction is detected while a VR-5 recovery is already in progress, the second instruction falls back to VR-1 recovery (flagged in the Active List), ensuring clean separation of recoveries with no state corruption.

**Projected VR-5 gains**
VR-5 is projected to reduce the average recovery stall penalty from 100 to 167 cycles (VR-1) to approximately 10 to 20 cycles, an estimated 80 to 85% reduction in recovery overhead. For the most severely affected benchmarks (456.hmmer and 482.sphinx3), VR-5 is projected to meaningfully recover lost IPC beyond the 2.396 ceiling observed under optimal structural scaling.

Files
1. vpu.h / vpu.cc: SVP and VPQ implementation
2. renamer.h / renamer.cc: Physical register file, Active List, Free List, RMT management
3. rename.cc: Rename stage: VP eligibility check, SVP query, VPQ allocation, stall logic
4. dispatch.cc: Dispatch stage: predicted value injection into PRF, ready bit set
5. execute.cc: Execute stage: value misprediction detection, VR-5 trigger
6. writeback.cc: Writeback stage: VP check handling for replayed loads
7. retire.cc: Retire stage: SVP training via VPQ, measurement tallying
8. squash.cc: VR-1 and VR-5 squash and branch resolve logic
9. pipeline.cc / pipeline.h: Top-level pipeline with VP command-line argument integration
10. ECE721_proj_4_team_37.pptx: Competition phase research presentation

Tools
Simulator: 721sim (cycle-accurate out-of-order superscalar processor simulator)
Language: C++
Benchmarks: 15 SPEC CPU benchmarks run on NCSU HPC cluster
