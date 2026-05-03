#include "pipeline.h"

extern debug_buffer_t *DB;

////////////////////////////////////////////////////////////////////////////////////
// The Rename Stage has two sub-stages:
// rename1: Get the next rename bundle from the FQ.
// rename2: Rename the current rename bundle.
////////////////////////////////////////////////////////////////////////////////////

void pipeline_t::rename1() {
   unsigned int i;
   unsigned int rename1_bundle_width;

   ////////////////////////////////////////////////////////////////////////////////////
   // Try to get the next rename bundle.
   // Two conditions might prevent getting the next rename bundle, either:
   // (1) The current rename bundle is stalled in rename2.
   // (2) The FQ does not have enough instructions for a full rename bundle,
   //     and it's not because the fetch unit is stalled waiting for a
   //     serializing instruction to retire (fetch exception, amo, or csr instruction).
   ////////////////////////////////////////////////////////////////////////////////////

   // Check the first condition. Is the current rename bundle stalled, preventing
   // insertion of the next rename bundle? Check whether or not the pipeline register
   // between rename1 and rename2 still has a rename bundle.

   if (RENAME2[0].valid) { // The current rename bundle is stalled.
      return;
   }

   // Check the second condition.
   // Stall if the fetch unit is active (it's not waiting for a serializing
   // instruction to retire) and the FQ doesn't have enough instructions for a full
   // rename bundle.

   rename1_bundle_width = ((FQ.get_length() < dispatch_width) ? FQ.get_length() : dispatch_width);

   if (FetchUnit->active() && (rename1_bundle_width < dispatch_width)) {
      return;
   }

   // Get the next rename bundle.
   for (i = 0; i < rename1_bundle_width; i++) {
      assert(!RENAME2[i].valid);
      RENAME2[i].valid = true;
      RENAME2[i].index = FQ.pop();
   }
}

void pipeline_t::rename2() {
   unsigned int i;
   unsigned int index;
   unsigned int bundle_dst, bundle_branch , m_bundle_vp_eligible;
   bool m_svp_hit;

   // Stall the rename2 sub-stage if either:
   // (1) There isn't a current rename bundle.
   // (2) The Dispatch Stage is stalled.
   // (3) There aren't enough rename resources for the current rename bundle.

   if (!RENAME2[0].valid || // First stall condition: There isn't a current rename bundle.
       DISPATCH[0].valid) { // Second stall condition: The Dispatch Stage is stalled.
      return;
   }

   // Third stall condition: There aren't enough rename resources for the current rename bundle.
   bundle_dst = 0;
   bundle_branch = 0;
   m_bundle_vp_eligible = 0;
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break; // Not a valid instruction: Reached the end of the rename bundle so exit loop.

      index = RENAME2[i].index;

      // FIX_ME #1
      // Count the number of instructions in the rename bundle that need a checkpoint (most branches).
      // Count the number of instructions in the rename bundle that have a destination register.
      // With these counts, you will be able to query the renamer for resource availability
      // (checkpoints and physical registers).
      //
      // Tips:
      // 1. The loop construct, for iterating through all instructions in the rename bundle (0 to dispatch_width),
      //    is already provided for you, above. Note that this comment is within the loop.
      // 2. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
      // 3. The instruction's payload has all the information you need to count resource needs.
      //    There is a flag in the instruction's payload that *directly* tells you if this instruction needs a checkpoint.
      //    Another field indicates whether or not the instruction has a destination register.

      // FIX_ME #1 BEGIN
      
      // Number of free dst reg resources that are needed 
         if(PAY.buf[index].C_valid){
            bundle_dst++;
         }
      
      // Number of branch checkpoints needed 
      if(PAY.buf[index].checkpoint){
         bundle_branch++;
      }

      // VPU 
      // incrementing count for eligible entries for VP
      if(eligible(&PAY.buf[index])){
         m_bundle_vp_eligible++;
      }

      // FIX_ME #1 END
   }

   // FIX_ME #2
   // Check if the Rename2 Stage must stall due to any of the following conditions:
   // * Not enough free checkpoints.
   // * Not enough free physical registers.
   //
   // If there are not enough resources for the *whole* rename bundle, then stall the Rename2 Stage.
   // Stalling is achieved by returning from this function ('return').
   // If there are enough resources for the *whole* rename bundle, then do not stall the Rename2 Stage.
   // This is achieved by doing nothing and proceeding to the next statements.

   // FIX_ME #2 BEGIN
   // stall to check free checkpoints
   if(REN->stall_branch(bundle_branch)){
      return;
   }

   // stall to check free phys reg 
   if(REN->stall_reg(bundle_dst)){
      return;
   }

   // Fourth Stall Condition
   if(VPU != nullptr && VPU->m_vpq_free_entries() < m_bundle_vp_eligible){
      return;
   }
   // FIX_ME #2 END

   //
   // Sufficient resources are available to rename the rename bundle.
   //
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break; // Not a valid instruction: Reached the end of the rename bundle so exit loop.

      index = RENAME2[i].index;

      // Initialize VP fields to safe defaults for ALL instructions
      PAY.buf[index].vp_eligible  = false;
      PAY.buf[index].vp_predicted = false;
      PAY.buf[index].vp_confident = false;
      PAY.buf[index].vp_value     = 0;
      PAY.buf[index].vpq_index    = -1;
      PAY.buf[index].vp_hit       = false;



      // FIX_ME #3
      // Rename source registers (first) and destination register (second).
      //
      // Tips:
      // 1. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
      // 2. The instruction's payload has all the information you need to rename registers, if they exist. In particular:
      //    * whether or not the instruction has a first source register, and its logical register number
      //    * whether or not the instruction has a second source register, and its logical register number
      //    * whether or not the instruction has a third source register, and its logical register number
      //    * whether or not the instruction has a destination register, and its logical register number
      // 3. When you rename a logical register to a physical register, remember to *update* the instruction's payload with the physical register specifier,
      //    so that the physical register specifier can be used in subsequent pipeline stages.

      // FIX_ME #3 BEGIN
      // rename src reg 1 
      if(PAY.buf[index].A_valid){
         PAY.buf[index].A_phys_reg = REN->rename_rsrc(PAY.buf[index].A_log_reg);
      }

      // rename src reg 2
      if(PAY.buf[index].B_valid){
         PAY.buf[index].B_phys_reg = REN->rename_rsrc(PAY.buf[index].B_log_reg);
      }

      // rename src reg 3
      if(PAY.buf[index].D_valid){
         PAY.buf[index].D_phys_reg = REN->rename_rsrc(PAY.buf[index].D_log_reg);
      }

      // rename dst reg 
      if(PAY.buf[index].C_valid){
         PAY.buf[index].C_phys_reg = REN->rename_rdst(PAY.buf[index].C_log_reg);
      }

      // FIX_ME #3 END

      // FIX_ME #4
      // Get the instruction's branch mask.
      //
      // Tips:
      // 1. Every instruction gets a branch_mask. An instruction needs to know which branches it depends on, for possible squashing.
      // 2. The branch_mask is not held in the instruction's PAY.buf[] entry. Rather, it explicitly moves with the instruction
      //    from one pipeline stage to the next. Normally the branch_mask would be wires at this point in the logic but since we
      //    don't have wires place it temporarily in the RENAME2[] pipeline register alongside the instruction, until it advances
      //    to the DISPATCH[] pipeline register. The required left-hand side of the assignment statement is already provided for you below:
      //    RENAME2[i].branch_mask = ??;

      // FIX_ME #4 BEGIN
      
      // assign branch_mask 
      RENAME2[i].branch_mask = REN->get_branch_mask();

      // FIX_ME #4 END

      // FIX_ME #5
      // If this instruction requires a checkpoint (most branches), then create a checkpoint.
      //
      // Tips:
      // 1. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
      // 2. There is a flag in the instruction's payload that *directly* tells you if this instruction needs a checkpoint.
      // 3. If you create a checkpoint, remember to *update* the instruction's payload with its branch ID
      //    so that the branch ID can be used in subsequent pipeline stages.

      // FIX_ME #5 BEGIN
      if(PAY.buf[index].checkpoint){
         PAY.buf[index].branch_ID = REN->checkpoint();
         
         // save VPQ tail checkpoint for branch misprediction recovery
         if(VPU != nullptr){
            PAY.buf[index].vpq_tail_checkpoint       = VPU->m_vpq_tail;
            PAY.buf[index].vpq_tail_phase_checkpoint = VPU->m_vpq_tail_phase;
         }
      }
      // FIX_ME #5 END

      // VPU
      PAY.buf[index].vp_eligible = eligible(&PAY.buf[index]);
      
      if(VPU != nullptr && PAY.buf[index].vp_eligible){
         uint64_t m_pred_value = 0;
         bool m_confident = false;

         // perf mode 
         if(VPU->perf_mode){
         // only use checker value if instruction is on correct path
               if(PAY.buf[index].good_instruction && valid_debug_index(PAY.buf[index].db_index)){
                  
                  db_t *actual = DB->peek(PAY.buf[index].db_index);
                  m_pred_value = actual->a_rdst[0].value;
                  m_confident  = true;
                  PAY.buf[index].vp_hit = true; 
            }
            else 
            {
                  PAY.buf[index].vp_hit = false; 
            }
         }
         else {   
            //  SVP lookup 
            /*
               SVP MODE 
               1] call the m_predict() and pass variables by refernce (since we want to change the original value )
               2] In oracle confidence mode ; if the pred_value != 0 
                  - check if it is a good instruction and has valid debug index 
                     - *actual will point to the value in the checker
                     - confidence flag = 1 if value in checker matches the predicted value
                  - else confidence = false 
            */

            // SVP mode
            uint32_t m_vpq_copy = 0;
            m_svp_hit = VPU->m_vpu_predict(PAY.buf[index].pc, m_pred_value, m_confident, m_vpq_copy);
            PAY.buf[index].vp_hit = m_svp_hit;

            // oracle confidence override
            if(m_svp_hit && VPU->oracle_conf_mode){

               if(PAY.buf[index].good_instruction && valid_debug_index(PAY.buf[index].db_index)){
                     db_t *actual = DB->peek(PAY.buf[index].db_index);
                     m_confident = (actual->a_rdst[0].value == m_pred_value);
               }
               else{
                     m_confident = false;
               }
            }

         }

         // allocate VPQ tail entry
         uint32_t m_vpq_idx = VPU->m_vpq_tail;
         VPU->m_vpq[m_vpq_idx].m_vpq_pc        = PAY.buf[index].pc;
         VPU->m_vpq[m_vpq_idx].m_vpq_valid     = true;
         VPU->m_vpq[m_vpq_idx].m_vpq_val_ready = false;

         // Advance Tail
         VPU->m_vpq_tail = (VPU->m_vpq_tail + 1);
         if(VPU->m_vpq_tail == VPU->m_vpq_size){
            VPU->m_vpq_tail = 0;
            VPU->m_vpq_tail_phase = !(VPU->m_vpq_tail_phase);
         }

         // store results in payload
         if(VPU->perf_mode){
            PAY.buf[index].vp_predicted = PAY.buf[index].vp_hit;
            PAY.buf[index].vp_confident = m_confident;
         }
         else{
            PAY.buf[index].vp_predicted = (m_svp_hit && m_confident);
            PAY.buf[index].vp_confident = m_confident;
         }
         PAY.buf[index].vp_value   = m_pred_value;
         PAY.buf[index].vpq_index  = m_vpq_idx;
      }  
   }

   //
   // Transfer the rename bundle from the Rename Stage to the Dispatch Stage.
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break; // Not a valid instruction: Reached the end of the rename bundle so exit loop.

      assert(!DISPATCH[i].valid);
      RENAME2[i].valid = false;
      DISPATCH[i].valid = true;
      DISPATCH[i].index = RENAME2[i].index;
      DISPATCH[i].branch_mask = RENAME2[i].branch_mask;
   }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* This function checks value-prediction eligibility predINTALU, predFPALU, and predLOAD are all "bool" types,
and are configured to be true or false based on corresponding simulator arguments being 1 or 0, respectively. */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool pipeline_t::eligible(payload_t *pay){
   if(!pay -> C_valid)
      return (false);                  // Any instruction without a destination register is ineligible

   // If we reached this point, the instruction has a destination register
   if(IS_INTALU(pay->flags))
      return(predINTALU);              // instr. is INTALU type. It is eligible if predINTALU is configured "true".
   else if(IS_FPALU(pay->flags))
      return (predFPALU);              // instr. is FPTALU type. It is eligible if predFPALU is configured "true".
   else if(IS_LOAD(pay->flags) && !IS_AMO(pay->flags))
      return (predLOAD);               // inst. is a normal LOAD (not rare load-with-reserv). It is eligible if predLOAD is configured "true".
   else
      return (false);                  // instr. is none of the above major types, so it is never eligible.
}
