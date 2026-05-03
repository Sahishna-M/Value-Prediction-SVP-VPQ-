#include "renamer.h"
#include<cassert>

//**************************** CONSTRUCTOR DEFINITIONS ***************************/

// *********** RENAMER  ***********
renamer::renamer(uint64_t n_log_regs,uint64_t n_phys_regs,uint64_t n_branches,uint64_t n_active){

	/* Inputs we have with instantiation :
		1] logical registers - n_logs_reg
		2] physical registers - n_phys_regs -> OK
		3] unresolved branches - n_branches (max 64)
		4] active instructions - n_active (max active list size) -> OK


		Things to remember :
		1] Logical Registers are renamed to Physical registers.
		2] No. of Physical Regs > No. of Logical Regs
		3] Any reg will always have one committed vers and other speculative vers.

	*/

	// Local Variables
	m_n_log_regs = n_log_regs;    // rmt , amt
	m_n_phys_regs = n_phys_regs;  // al , fl size
	m_n_branches = n_branches;
	m_n_active = n_active ;


	// Initialisation of AMT -> AMT size = logical regs as committed vers stored in AMT
	m_amt = new uint64_t[m_n_log_regs];
	for (int i = 0 ; i < m_n_log_regs ; i++){
		m_amt[i] = i;
	}


	// Initialisation of RMT -> RMT size = logical regs
	// For empty pipeline; RMT reflects AMT
	m_rmt = new uint64_t[m_n_log_regs];
	for (int i = 0; i < m_n_log_regs ; i++){
		m_rmt[i] = m_amt[i];
	}


	// Initialise Free List as FULL -> length = phys regs - log regs 
	// Head and Tail ptr at same position(0) & diff phase bits resp
	//CORRECTION DONE :replaced m_n_active with m_n_log_regs
	m_fl_length = m_n_phys_regs - m_n_log_regs;			
	m_fl_fifo = new uint64_t[m_fl_length];
	m_fl_head_ptr = 0;
	m_fl_tail_ptr = 0;
	m_fl_head_phase = false;
	m_fl_tail_phase = true;
	for(int i = 0 ; i < m_fl_length ; i++){
		m_fl_fifo[i] = i + m_n_log_regs;
	}


	// Initialise Active List as EMPTY -> No speculative vers (empty pipeline)
	// Head and Tail ptr at same position(0) & same phase bits as empty
	m_al_fifo = new active_list_entry_t[m_n_active];
	m_al_head_ptr = 0;
	m_al_tail_ptr = 0;
	m_al_head_phase = false;
	m_al_tail_phase = false;
	m_al_length = m_n_active;


	// Initialise PRF as EMPTY -> empty pipeline no mapping yet
	m_prf = new uint64_t[m_n_phys_regs];
	// CORRECTION DONE : i< m_n_active replaced with m_n_phys_regs
	for(int i = 0 ; i < m_n_phys_regs ; i++){ 
		m_prf[i] = 0;
	}

	// Initialise PRF Ready Bit Array -> 1 as all versions are committed
	m_prf_ready_bit_arr = new bool[m_n_phys_regs];
	for (int i = 0 ; i < m_n_log_regs ; i++){			// phys_reg nahi log_regs set honge 
		m_prf_ready_bit_arr[i] = true ;
	}

	// Initialise GBM
	GBM = 0;

	// Initialise Branch Checkpoints Size -> unresolved branches
	m_branch_checkpoints = new branch_checkpt_entry_t[m_n_branches];
	for (int i = 0 ; i < m_n_branches ; i++){
		m_branch_checkpoints[i].m_smt = new uint64_t[m_n_log_regs];
		m_branch_checkpoints[i].m_cp_fl_head_phase = false;
		m_branch_checkpoints[i].m_cp_fl_head_ptr = 0;
		m_branch_checkpoints[i].m_cp_GBM = 0;
	}

}

// *********** DESTRUCTOR (EMPTY FUNCTION) ***********
renamer::~renamer(){

}

//**************************** FUNCTIONS DEFINITIONS ***************************/

// *********************************************************
// *********** FUNCTIONS RELATED TO RENAME STAGE ***********
// *********************************************************

bool renamer::stall_reg(uint64_t bundle_dst){
/*
	The Rename Stage must stall if there aren't enough free physical
	registers available for renaming all logical destination registers
	in the current rename bundle.

	stall_reg functionality :
	-> current fl length < bundle_dst = true (stall)
	-> current fl length >= bundle_dst = false (do not stall)
	-> free between head and tail, occupied everything.

*/
	// Step-1 : Calculate the length of current free list
	uint64_t m_fl_curr_length = 0;

	// CORRECTION DONE : Compared tail phase with itself 
	if(m_fl_tail_phase == m_fl_head_phase){
		m_fl_curr_length = m_fl_tail_ptr - m_fl_head_ptr;
	}
	else if(m_fl_tail_phase != m_fl_head_phase){
		m_fl_curr_length = m_fl_length - (m_fl_head_ptr - m_fl_tail_ptr);
	}

	// Step-2 : Compare with bundle_dst
	bool stall = (m_fl_curr_length >= bundle_dst) ? false : true;
	return stall;
}

bool renamer::stall_branch(uint64_t bundle_branch){

/*
	The Rename Stage must stall if there aren't enough free
	checkpoints for all branches in the current rename bundle.

	-> bundle_branch = no. of branches in rename bundle
	-> no. of GBM bits = max. no. of unresolved branches
	-> GBM ith bit = 0 -> free
	-> GBM ith bit = 1 -> allocated

	stall_branch functionality:
	-> free_GBM_bits >= bundle_branch -> do not stall
	-> free_GBM_bits < bundle_branch -> stall
*/

	// Step-1 : Create a copy of GBM
	uint64_t gbm_copy = GBM;

	// Step-2 : Initialise the var for free checkpoints
	uint64_t num_free_checkpoints = 0;

	// Step-3 : Iterate to find the no. of free checkpoints
	for(uint64_t i = 0 ; i < m_n_branches ; i++){

		// Boolean AND to find free cehckpoint bits
		if((gbm_copy & 0x1ull) == 0){
			num_free_checkpoints++;
		}
		// Right shift the copy by 1
		gbm_copy = gbm_copy >> 1;
	}

	// Step-4 : Compare with bundle branch
	bool stall = (num_free_checkpoints < bundle_branch) ? true : false;
	return stall;
}

uint64_t renamer::get_branch_mask(){
	// This function is used to get the branch mask for an instruction.

	// Step-1 : Return GBM
	return GBM;
}

uint64_t renamer::rename_rsrc(uint64_t log_reg){
	// This function is used to rename a single source register.

	// Step-1 : Return the phys reg val for the log reg
	uint64_t src_phys_regs_assigned = m_rmt[log_reg];
	return src_phys_regs_assigned;
}

uint64_t renamer::rename_rdst(uint64_t log_reg){
	//This function is used to rename a single destination register.

	// Step-1 : Pop the head from free list to allocate a phys reg
	uint64_t phys_reg_alloc = helper_fl_pop();

	// Step-2 : Allocate the logs reg to phys reg
	m_rmt[log_reg] = phys_reg_alloc;
	return phys_reg_alloc;
}

uint64_t renamer::checkpoint(){

/*
	This function creates a new branch checkpoint.

	-> Returns Branch ID
	
	checkpoint functionality:
	-> find free entry in GBM i.e. (==0)
	-> store position of the bit as branch id and make it 1
	-> checkpoint the branch at branch id:
			1] copy checkpointed GBM 
			2] copy fl head ptr and its pahse
			3] copy rmt as smt 

*/

	// Step-1 : Find free entry in GBM 
	uint64_t gbm_copy = GBM;
	uint64_t branch_id;

	for(uint64_t i = 0 ; i < m_n_branches ; i++){
		if((gbm_copy & 0x1ull) == 0){
		// Step-2 : Store position of bit as branch id
			branch_id = i;
		break;
		}
		gbm_copy = (gbm_copy >> 1);
	}

	// Step-4 : Checkpoint the branch 
	for(uint64_t i = 0 ; i < m_n_log_regs ; i++){
		m_branch_checkpoints[branch_id].m_smt[i] = m_rmt[i];
	}

	m_branch_checkpoints[branch_id].m_cp_GBM = GBM;
	m_branch_checkpoints[branch_id].m_cp_fl_head_phase = m_fl_head_phase; 
	m_branch_checkpoints[branch_id].m_cp_fl_head_ptr = m_fl_head_ptr;

	// Step-3 : Make the ith bit 1 in GBM 
	GBM = (GBM | (0x1ull << branch_id));
	return branch_id;
}

// **********************************************************
// *********** FUNCTIONS RELATED TO DISPATCH STAGE **********
// **********************************************************

bool renamer::stall_dispatch(uint64_t bundle_inst){
/*
	The Dispatch Stage must stall if there are not enough free
	entries in the Active List for all instructions in the current
	dispatch bundle.

	-> bundle_inst : contains all the active instructions

	stall_dispatch functionality :
	-> bundle_inst > free_al_entries -> stall
	-> bundle_inst <= free_al_entries -> do not stall
	-> occupied between head and tail, free everything else

*/
	// Step-1 : Calculate the number of free entries in AL
	uint64_t m_al_free_entries = 0;

	//NO CORRECTION m_al_length initialised as 0 from stale value. 
	if(m_al_tail_phase == m_al_head_phase){
		m_al_free_entries = m_al_length - (m_al_tail_ptr - m_al_head_ptr);
	}
	else if(m_al_tail_phase != m_al_head_phase){
		m_al_free_entries = m_al_head_ptr - m_al_tail_ptr;
	}

	// Step-2 : Compare with bundle_inst
	bool stall = (m_al_free_entries <  bundle_inst) ? true : false;
	return stall;
}

uint64_t renamer::dispatch_inst(bool dest_valid, uint64_t log_reg,uint64_t phys_reg,bool load,bool store,bool branch,bool amo,bool csr,uint64_t PC){
/*
	This function dispatches a single instruction into the Active List.

	-> dest_valid : true -> dest and src reg exists
				  : false -> ignore log_reg and phys_reg

	-> log_reg : dest_log_reg
	-> phys_reg : dest_phy_reg
	-> load , store , branch , amo , csr : bool functions
	-> PC : PC on instruction

	dispatch_inst functionality :
	-> call helper push function and store the value returned
	-> return the stored index value
*/

	uint64_t inst_al_index = helper_al_push(dest_valid,log_reg, phys_reg, load, store, branch, amo, csr, PC);
	return inst_al_index;
}

bool renamer::is_ready(uint64_t phys_reg){

	// Test the ready bit of the indicated physical register
	bool is_ready = m_prf_ready_bit_arr[phys_reg] ;
	return is_ready;
}

void renamer::clear_ready(uint64_t phys_reg){

	// Clear the ready bit of the indicated physical register.
	m_prf_ready_bit_arr[phys_reg] = false;
}

// ************************************************************************
// *********** FUNCTIONS RELATED TO REG READ AND EXECUTE STAGES ***********
// ************************************************************************


uint64_t renamer::read(uint64_t phys_reg){

	// Return the contents (value) of the indicated physical register.
	uint64_t value = m_prf[phys_reg];
	return value;
}

void renamer::set_ready(uint64_t phys_reg){

	// Set the ready bit of the indicated physical register.
	m_prf_ready_bit_arr[phys_reg] = true;
}

// *************************************************************
// *********** FUNCTIONS RELATED TO WRITEBACK STAGES ***********
// *************************************************************

void renamer::write(uint64_t phys_reg, uint64_t value){

	// Write a value into the indicated physical register.
	m_prf[phys_reg] = value;
}

void renamer::set_complete(uint64_t AL_index){

	// Set the completed bit of the indicated entry in the Active List.
	m_al_fifo[AL_index].m_al_completed_bit = true;
}

void renamer::resolve(uint64_t AL_index,uint64_t branch_ID, bool correct){
/*
	This function is for handling branch resolution.

	-> Inputs : 1] AL_index -> branch index in al
				2] Branch_ID -> unique identification of branch
				3] Correct -> true : correctly branch predicted 
							  false : mispredicted & recovery req
	
	->  Outputs : None 

	resolve functionality :
	1] correct prediction : 
		-> clear branch's bit in the gbm 
		-> clear branch's bit in all checkpointed GBMs
		
	2] Misprediction :
		-> restore GBM from branch's checkpoint -> clear mispred branch bit in restored GBM
		-> restore the RMT using branch's checkpoint
		-> restore fl_head_ptr and fl_head_phase_bit using branch's checkpoint
		-> restore al_tail_pointer and al_tail_phase_bit corresponding to entry after branch's entry
*/	

	// 1] Correct Prediction 
	if(correct){

		// Step-1 : clear branch's bit in the GBM 
		GBM = ((~(0x1ull << branch_ID)) & GBM);

		// Step-2 : clear branch's bit in all checkpointed GBMs
		for(uint64_t i = 0 ; i < m_n_branches ; i++){
			m_branch_checkpoints[i].m_cp_GBM = ((~(0x1ull << branch_ID)) & m_branch_checkpoints[i].m_cp_GBM);
		}
	}
	else{

		// 2] Misprediction 

		// Step-1 : restore GBM from branch's checkpoint 
		GBM = m_branch_checkpoints[branch_ID].m_cp_GBM;

		// Step-2 : restore RMT using branch's checkpoint
		for(uint64_t i = 0 ; i < m_n_log_regs ; i++){
			m_rmt[i] = m_branch_checkpoints[branch_ID].m_smt[i];
		}

		// Step-3 : restore fl_head_ptr and fl_phase_bit using branch's checkpoint 
		m_fl_head_ptr = m_branch_checkpoints[branch_ID].m_cp_fl_head_ptr;
		m_fl_head_phase = m_branch_checkpoints[branch_ID].m_cp_fl_head_phase;

		// Step-4 : restore al_tail_ptr and al_tail_phase corresponding to entry after branch's entry
		m_al_tail_ptr = AL_index + 1;
		
		// check for wrap around condition for tail ptr -> if wraps to head 
		if(m_al_tail_ptr == m_al_length){
			m_al_tail_ptr = 0;
		}

		// for tail phase bit recovery 
		/*
			1] h_phase == t_phase 
				-> empty - not possible as there will always be branch 
				-> h_ptr < t_ptr 
			
			2] h_phase != t_phase 
				-> full - not possible as roll back 
				-> t_ptr > h_ptr 
		*/
		if (m_al_head_ptr < m_al_tail_ptr){
			m_al_tail_phase = m_al_head_phase ;
		}
		else if (m_al_head_ptr >= m_al_tail_ptr){
			m_al_tail_phase = (!m_al_head_phase) ;
		}
	}
}

// **********************************************************
// *********** FUNCTIONS RELATED TO RETIRE STAGES ***********
// **********************************************************

bool renamer::precommit(bool &completed,bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	               bool &load, bool &store, bool &branch, bool &amo, bool &csr,uint64_t &PC){

/*
   This function allows the caller to examine the instruction at the head of the Active List.

   precommit functionality :
   1] (!helper_al_empty) i.e. al not empty :
   					-> return first element vals in argument
					-> return true

   2] (helper_al_empty) i.e. empty :
   					-> return dont care for the return values
					-> return false

*/

	// Step-1 : Check and store the bool value for al empty or not empty condition
	bool al_empty = helper_al_empty();

	// Step-2 : Return Arguments
	// condition : al is not empty
	if(!al_empty){

		// Allocate for completion bit
		completed = m_al_fifo[m_al_head_ptr].m_al_completed_bit ;

		// Allocate for offending instr bits
		exception = m_al_fifo[m_al_head_ptr].m_al_exception_bit;
		load_viol = m_al_fifo[m_al_head_ptr].m_al_load_violation_bit;
		br_misp = m_al_fifo[m_al_head_ptr].m_al_branch_mispred_bit;
		val_misp = m_al_fifo[m_al_head_ptr].m_al_value_mispred_bit;

		// Allocate for special instr bits
		load = m_al_fifo[m_al_head_ptr].m_al_load_flag;
		store = m_al_fifo[m_al_head_ptr].m_al_store_flag;
		branch = m_al_fifo[m_al_head_ptr].m_al_branch_flag;
		amo = m_al_fifo[m_al_head_ptr].m_al_amo_flag;
		csr = m_al_fifo[m_al_head_ptr].m_al_csr_flag;

		// Allocate for PC instruction
		PC = m_al_fifo[m_al_head_ptr].m_al_pc;

	}
	return !al_empty;
}

void renamer::commit(){
/*
	This function commits the instruction at the head of the Active List.

	commit functionality :
	-> call the pop helper function
*/

	// Step-1 : Assert Checks :
	// There is a head instruction (the active list isn't empty)
	assert(!helper_al_empty());

	// The head instruction is completed
	assert(m_al_fifo[m_al_head_ptr].m_al_completed_bit == true);

	// The head instruction is not marked as an exception
	assert(!m_al_fifo[m_al_head_ptr].m_al_exception_bit);

	// The head instruction is not marked as a load violation
	assert(!m_al_fifo[m_al_head_ptr].m_al_load_violation_bit);

	// Committing and Freeing Process
	// Step-2 : Valid Destination Reg Check
	if(m_al_fifo[m_al_head_ptr].m_al_dest_reg){

		// Step-3 : Store val of old phys reg at that log reg position
		uint64_t dest_log_reg = m_al_fifo[m_al_head_ptr].m_al_dest_log_reg_num;
		uint64_t dest_phys_reg = m_al_fifo[m_al_head_ptr].m_al_dest_phy_reg_num;
		uint64_t prev_phys_reg = m_amt[dest_log_reg];

		// Step-4 : FREE -> Push the prev phys reg value in free list
		helper_fl_push(prev_phys_reg);

		// Step-5 : COMMIT -> Allocate the current phys reg value in the amt
		m_amt[dest_log_reg] = dest_phys_reg;
	}

	// Step-6 : Pop the head of the the active list
	active_list_entry_t pop_return_value = helper_al_pop();
}

void renamer::squash(){
/*
	Squash the renamer class.

	squash functionality :
	-> clear the pipelines and rmt and amt mappings become same
*/

	// Step-1 : RMT Restore -> Make the AMT and RMT Mappings same
	for (uint64_t i = 0 ; i < m_n_log_regs ; i++){
			m_rmt[i] = m_amt[i] ;
	}

	// Step-2 : Active List Restore -> Make active list empty
	m_al_tail_ptr = m_al_head_ptr ;
	m_al_tail_phase = m_al_head_phase ;

	// Step-3 : Free List Restore -> Make free list full
	// CORRECTION DONE
	m_fl_head_ptr = m_fl_tail_ptr; 		// Used == instead of = 
	m_fl_head_phase = (!m_fl_tail_phase); // used != instead of = (!condition)

	// Step-4 : Ready Bit Array Restore -> Initiate the committed ones as true
	for (int i = 0 ; i < m_n_log_regs ; i++){
		uint64_t phys_reg = m_amt[i];
		m_prf_ready_bit_arr[phys_reg] = true;
	}

	// Step-5 : GB restore 
	GBM = 0 ;
}

// *******************************************************
// *********** FUNCTIONS RELATED TO FREE LIST  ***********
// *******************************************************

bool renamer::helper_fl_full(){
	bool full = (m_fl_head_ptr == m_fl_tail_ptr) && (m_fl_head_phase != m_fl_tail_phase) ;
	return full;
}

bool renamer::helper_fl_empty(){
	bool empty = (m_fl_head_ptr == m_fl_tail_ptr) && (m_fl_head_phase == m_fl_tail_phase);
	return empty;
}

uint64_t renamer::helper_fl_pop(){
	assert(!helper_fl_empty());

	// Step-1 : Store the phys reg value
	uint64_t reg_val_popped = m_fl_fifo[m_fl_head_ptr];

	// Step-2 : Increement Head pointer
	m_fl_head_ptr++;

	// Step-3 : Check Wrap Around Condition
	if(m_fl_head_ptr == m_fl_length){
		m_fl_head_ptr = 0;
		m_fl_head_phase = !(m_fl_head_phase);
	}

	// Step-4 : Return the phys reg to be popped
	return reg_val_popped;

}

void renamer::helper_fl_push(uint64_t m_push_reg){
	assert(!helper_fl_full());

	// Step-1 : Push phys reg at tail
	m_fl_fifo[m_fl_tail_ptr] = m_push_reg;

	// Step-2 : Increement Tail pointer
	m_fl_tail_ptr++;

	// Step-3 : Check Wrap Around Condition
	if(m_fl_tail_ptr == m_fl_length){
			m_fl_tail_ptr = 0;
			m_fl_tail_phase = !(m_fl_tail_phase);
	}
}

// *******************************************************
// *********** FUNCTIONS RELATED TO ACTIVE LIST  *********
// *******************************************************

bool renamer::helper_al_empty(){
	bool empty = (m_al_head_ptr == m_al_tail_ptr) && (m_al_head_phase == m_al_tail_phase);
	return empty;
}

bool renamer::helper_al_full(){
	bool full = (m_al_head_ptr == m_al_tail_ptr) && (m_al_head_phase != m_al_tail_phase) ;
	return full;
}

active_list_entry_t renamer::helper_al_pop(){
	assert(!helper_al_empty());

	// Step-1 : Store the phys reg and the log reg value
	active_list_entry_t al_pop_entry = m_al_fifo[m_al_head_ptr];

	// Step-2 : Increement Head Pointer
	m_al_head_ptr++;

	// Step-3 : Check Wrap Around Condition
	if(m_al_head_ptr == m_al_length){
		m_al_head_ptr = 0;
		m_al_head_phase = !(m_al_head_phase);
	}

	// Step-4 : Return the Active List Entry
	return al_pop_entry;
}

uint64_t renamer::helper_al_push(bool dest_valid,
	                       uint64_t log_reg,
	                       uint64_t phys_reg,
	                       bool load,
	                       bool store,
	                       bool branch,
	                       bool amo,
	                       bool csr,
	                       uint64_t PC){

	assert(!helper_al_full());

	// Step-1 : Increement Tail Pointer
	uint64_t m_inst_tindex = m_al_tail_ptr ;
	m_al_tail_ptr++;


	// Step-2 : Check Wrap Around Condition
	if(m_al_tail_ptr == m_al_length){
			m_al_tail_ptr = 0;
			m_al_tail_phase = !(m_al_tail_phase);  		// CORRECTION DONE : fl instead of al 
	}


	// Step-3 : Instantiate all the arguments at the inst entry
	// Insert PC value
	m_al_fifo[m_inst_tindex].m_al_pc = PC;

	// Insert fields related to destination register
	m_al_fifo[m_inst_tindex].m_al_dest_reg = dest_valid;
	if(dest_valid){
				m_al_fifo[m_inst_tindex].m_al_dest_log_reg_num = log_reg;
				m_al_fifo[m_inst_tindex].m_al_dest_phy_reg_num = phys_reg;
	}

	// Insert fields indicating special instruction types
	m_al_fifo[m_inst_tindex].m_al_load_flag = load;
	m_al_fifo[m_inst_tindex].m_al_store_flag = store;
	m_al_fifo[m_inst_tindex].m_al_branch_flag = branch;
	m_al_fifo[m_inst_tindex].m_al_amo_flag = amo;
	m_al_fifo[m_inst_tindex].m_al_csr_flag = csr;

	// Instantiate completed bit as false
	m_al_fifo[m_inst_tindex].m_al_completed_bit = false;

	// Instantiate offending signals as false
	m_al_fifo[m_inst_tindex].m_al_exception_bit = false;
	m_al_fifo[m_inst_tindex].m_al_load_violation_bit = false;
	m_al_fifo[m_inst_tindex].m_al_branch_mispred_bit = false;
	m_al_fifo[m_inst_tindex].m_al_value_mispred_bit = false;

	return m_inst_tindex;
}


// ****************************************************************
// *********** FUNCTIONS NOT TIED TO ANY SPECIFIC STAGE ***********
// ****************************************************************

void renamer::set_exception(uint64_t AL_index){
	m_al_fifo[AL_index].m_al_exception_bit = true;
}

void renamer::set_load_violation(uint64_t AL_index){
	m_al_fifo[AL_index].m_al_load_violation_bit = true;
}

void renamer::set_branch_misprediction(uint64_t AL_index){
	m_al_fifo[AL_index].m_al_branch_mispred_bit = true;
}

void renamer::set_value_misprediction(uint64_t AL_index){
	m_al_fifo[AL_index].m_al_value_mispred_bit = true;
}


// *********** QUERY THE EXCEPTION BIT ***********
bool renamer::get_exception(uint64_t AL_index){
    bool exception_bit = m_al_fifo[AL_index].m_al_exception_bit;
	return exception_bit;
}
