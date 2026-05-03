#include <inttypes.h>
#include "vpu.h"


// **************** CONSTRUCTOR DEFINITIONS **********************

vpu_t::vpu_t(uint64_t vpq_size, uint64_t num_index_bits, 
             uint64_t num_tag_bits, uint64_t conf_max,
             bool perf_mode, bool oracle_conf_mode){

    // VPQ
    m_vpq_size = vpq_size;
    m_vpq = new vpq_entry_t[vpq_size];
    m_vpq_head = 0;
    m_vpq_tail = 0;
    m_vpq_head_phase = false;
    m_vpq_tail_phase = false;

    // measurement counters
    vpmeas_ineligible = 0;
    vpmeas_eligible = 0;
    vpmeas_miss = 0;
    vpmeas_conf_corr = 0;
    vpmeas_conf_incorr = 0;
    vpmeas_unconf_corr = 0;
    vpmeas_unconf_incorr = 0;

    // SVP
    m_svp_size = (1 << num_index_bits);  // 2^(num_index_bits)
    m_svp = new svp_entry_t[m_svp_size];
    m_svp_num_index_bits = num_index_bits;
    m_svp_num_tag_bits = num_tag_bits;
    m_svp_conf_max = conf_max;
    
    // M- VPU
    // Covered the corner case when the tag bits are not there
    // initialize all SVP entries
    for(uint64_t i = 0; i < m_svp_size; i++){
        m_svp[i].m_svp_valid     = (num_tag_bits == 0);  // valid from start if no tags
        m_svp[i].m_svp_tag       = 0;
        m_svp[i].m_svp_conf      = 0;
        m_svp[i].m_svp_retval    = 0;
        m_svp[i].m_svp_stride    = 0;
        m_svp[i].m_svp_instcount = 0;
    }

    this->perf_mode = perf_mode;
    this->oracle_conf_mode = oracle_conf_mode;    
}

uint64_t vpu_t::m_vpq_free_entries(){
    
    // Step-1 : Calculate the number of free entries in vpq
	uint64_t m_vpq_count = 0;

	//NO CORRECTION m_ _length initialised as 0 from stale value. 
	if(m_vpq_tail_phase == m_vpq_head_phase){
		m_vpq_count = m_vpq_size - (m_vpq_tail - m_vpq_head);
	}
	else if(m_vpq_tail_phase != m_vpq_head_phase){
		m_vpq_count = m_vpq_head - m_vpq_tail;
	}

    return m_vpq_count;
}

bool vpu_t::m_vpu_predict(uint64_t pc , uint64_t &pred_val, bool &confident, uint32_t  &vpq_index){

    // Bit Extraction 
    uint64_t pc_index = (pc >> 2) & (m_svp_size - 1 );
    uint64_t pc_tag = (pc >> (2 + m_svp_num_index_bits));

    // pointer to svp entry 
    svp_entry_t *entry = &m_svp[pc_index];

    // Check Hit 
    bool hit = false;
    /*
        CHECK HIT :
        1] (i) svp_tag_bits == 0
                - check if entry at that pc_index is valid/not.

            (ii) svp_tag_bits != 0
                - check if entry at that pc_index is valid 
                    - valid : (i) svp_tag & tag_mask == pc_tag & tag_mask 
        
        2] HIT : - increment the instance counter at that entry 
                 - pred_value = retired_val + (instance_count * stride)
                 - confident = (entry_confidence == conf_max) 

        3] MISS : - pred_value =0 
                  - confident = false 
    */
   
    if(m_svp_num_tag_bits == 0){
        hit = entry->m_svp_valid;
    }
    else{
        uint64_t tag_mask = (1ULL << m_svp_num_tag_bits) - 1;
        hit = entry->m_svp_valid &&
              ((entry->m_svp_tag & tag_mask) == (pc_tag & tag_mask));
    }

    if(hit){
        entry->m_svp_instcount++;
        pred_val  = entry->m_svp_retval +
                    (entry->m_svp_instcount * entry->m_svp_stride);
        confident = (entry->m_svp_conf == m_svp_conf_max);
        return true;
    }
    else{
        pred_val  = 0;
        confident = false;
        return false;
    }
}

void vpu_t::print_storage(FILE *stats_log){
    if(perf_mode) return;

    uint64_t tag_bits      = m_svp_num_tag_bits;
    uint64_t conf_bits     = (uint64_t)ceil(log2((double)(m_svp_conf_max + 1)));
    uint64_t retval_bits   = 64;
    uint64_t stride_bits   = 64;
    uint64_t instance_bits = (uint64_t)ceil(log2((double)m_vpq_size));
    uint64_t entry_bits    = tag_bits + conf_bits + retval_bits + 
                             stride_bits + instance_bits;
    uint64_t total_bits    = entry_bits * m_svp_size;

    fprintf(stats_log, "COST ACCOUNTING\n");
    fprintf(stats_log, "   One SVP entry:\n");
    fprintf(stats_log, "      tag           :  %lu bits\n", tag_bits);
    fprintf(stats_log, "      conf          :   %lu bits\n", conf_bits);
    fprintf(stats_log, "      retired_value :  %lu bits\n", retval_bits);
    fprintf(stats_log, "      stride        :  %lu bits\n", stride_bits);
    fprintf(stats_log, "      instance ctr  :   %lu bits\n", instance_bits);
    fprintf(stats_log, "      -------------------------\n");
    fprintf(stats_log, "      bits/SVP entry: %lu bits\n", entry_bits);
    fprintf(stats_log, "   Total storage cost (bits) = (%lu SVP entries x %lu bits/SVP entry) = %lu bits\n",
            m_svp_size, entry_bits, total_bits);
}