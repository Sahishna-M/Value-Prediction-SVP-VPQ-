#include <inttypes.h>

//************ STRUCTURE DECLARATIONS ************************/
struct active_list_entry_t{
	
	// Fields related to destination register 
	bool m_al_dest_reg;
	uint64_t m_al_dest_log_reg_num;
	uint64_t m_al_dest_phy_reg_num;

	// Fields related to completion status
	bool m_al_completed_bit;

	// Fields for signaling offending instructions
	bool m_al_exception_bit;
	bool m_al_load_violation_bit;
	bool m_al_branch_mispred_bit;
	bool m_al_value_mispred_bit;

	// Fields indicating special instruction types
	bool m_al_load_flag;
	bool m_al_store_flag;
	bool m_al_branch_flag;
	bool m_al_amo_flag;
	bool m_al_csr_flag;

	// Others -> Program Counter of instruction
	uint64_t m_al_pc;
};

struct branch_checkpt_entry_t{
	uint64_t *m_smt;
	uint64_t m_cp_fl_head_ptr;
	bool m_cp_fl_head_phase;
	uint64_t m_cp_GBM;
};
class renamer {
private:
	// Variable Declaration
	uint64_t m_n_log_regs;
	uint64_t m_n_phys_regs;
	uint64_t m_n_branches;
	uint64_t m_n_active;

	// Structure 1: Rename Map Table
	uint64_t *m_rmt;           // 1-D Dynamic Array

	// Structure 2: Architectural Map Table
	uint64_t *m_amt;           // 1-D Dynamic Array 

	// Structure 3: Free List
	uint64_t *m_fl_fifo;      // 1-D  Dynamic Array
	uint64_t m_fl_head_ptr;   // free list head pointer
	uint64_t m_fl_tail_ptr;   // free list tail pointer
	bool m_fl_head_phase;     // head phase bit
	bool m_fl_tail_phase;     // tail phase bit
	uint64_t m_fl_length;     // free list length 

	// Structure 4: Active List
	active_list_entry_t *m_al_fifo;  // 1-D Dynamic Array
	uint64_t m_al_head_ptr;		     // active list head pointer
	uint64_t m_al_tail_ptr;			 // active list tail pointer 
	bool m_al_head_phase;		     // head phase bit
	bool m_al_tail_phase;			 // tail phase bit
	uint64_t m_al_length;			 // active list length  

	// Structure 5: Physical Register File
	uint64_t *m_prf;    			// 1-D Dynamic Array 

	// Structure 6: Physical Register File Ready Bit Array
	bool *m_prf_ready_bit_arr;		// 1-D Dynamic Array	

	// Structure 7: Global Branch Mask (GBM)
	uint64_t GBM;

	// Structure 8: Branch Checkpoints
	branch_checkpt_entry_t *m_branch_checkpoints; // 1-D Dynamic Array

public:
	
		renamer(uint64_t n_log_regs,
		uint64_t n_phys_regs,
		uint64_t n_branches,
		uint64_t n_active);

	~renamer();


	//////////////////////////////////////////
	// Functions related to Rename Stage.   //
	//////////////////////////////////////////

	bool stall_reg(uint64_t bundle_dst);
	bool stall_branch(uint64_t bundle_branch);

	/////////////////////////////////////////////////////////////////////
	// This function is used to get the branch mask for an instruction.
	/////////////////////////////////////////////////////////////////////

	uint64_t get_branch_mask();
	uint64_t rename_rsrc(uint64_t log_reg);
	uint64_t rename_rdst(uint64_t log_reg);
	uint64_t checkpoint();

	//////////////////////////////////////////
	// Functions related to Dispatch Stage. //
	//////////////////////////////////////////

	bool stall_dispatch(uint64_t bundle_inst);
	uint64_t dispatch_inst(bool dest_valid,
	                       uint64_t log_reg,
	                       uint64_t phys_reg,
	                       bool load,
	                       bool store,
	                       bool branch,
	                       bool amo,
	                       bool csr,
	                       uint64_t PC);

	bool is_ready(uint64_t phys_reg);
	void clear_ready(uint64_t phys_reg);


	//////////////////////////////////////////
	// Functions related to the Reg. Read   //
	// and Execute Stages.                  //
	//////////////////////////////////////////

	uint64_t read(uint64_t phys_reg);
	void set_ready(uint64_t phys_reg);

	//////////////////////////////////////////
	// Functions related to Writeback Stage.//
	//////////////////////////////////////////

	void write(uint64_t phys_reg, uint64_t value);
	void set_complete(uint64_t AL_index);
	void resolve(uint64_t AL_index,
		     uint64_t branch_ID,
		     bool correct);

	//////////////////////////////////////////
	// Functions related to Retire Stage.   //
	//////////////////////////////////////////

	bool precommit(bool &completed,
                       bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	               bool &load, bool &store, bool &branch, bool &amo, bool &csr,
		       		uint64_t &PC);

	void commit();
	void squash();
	
	//////////////////////////////////////////
	// 			Helper Functions 		    //
	//////////////////////////////////////////
	uint64_t helper_al_push(bool dest_valid,
	                       uint64_t log_reg,
	                       uint64_t phys_reg,
	                       bool load,
	                       bool store,
	                       bool branch,
	                       bool amo,
	                       bool csr,
	                       uint64_t PC);
	active_list_entry_t helper_al_pop();
	bool helper_al_empty();
	bool helper_al_full();

	void helper_fl_push(uint64_t m_push_reg);
	uint64_t helper_fl_pop();
	bool helper_fl_full();
	bool helper_fl_empty();

	//////////////////////////////////////////
	// Functions not tied to specific stage.//
	//////////////////////////////////////////
	
	void set_exception(uint64_t AL_index);
	void set_load_violation(uint64_t AL_index);
	void set_branch_misprediction(uint64_t AL_index);
	void set_value_misprediction(uint64_t AL_index);

	/////////////////////////////////////////////////////////////////////
	// Query the exception bit of the indicated entry in the Active List.
	/////////////////////////////////////////////////////////////////////
	bool get_exception(uint64_t AL_index);
};