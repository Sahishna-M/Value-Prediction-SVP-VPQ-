// svp_vpq.h
#include <inttypes.h>
#include <cmath>
#include <cstdio>

#ifndef VPU_H
#define VPU_H

struct svp_entry_t{
    uint64_t m_svp_tag;
    uint64_t m_svp_conf;
    uint64_t m_svp_retval;
    int64_t m_svp_stride;                 // keep int as stride can be +/-
    int64_t m_svp_instcount;
    bool m_svp_valid;             // to check if the entry is empty 
};

struct vpq_entry_t{
    uint64_t m_vpq_pc;
    uint64_t m_vpq_val;
    bool m_vpq_val_ready;
    bool m_vpq_valid;
};

class vpu_t{
public :
        // Structure 1 : SVP 
        svp_entry_t *m_svp;
        uint64_t m_svp_size;
        uint64_t m_svp_num_index_bits;
        uint64_t m_svp_num_tag_bits;
        uint64_t m_svp_conf_max;

        // Structure 2 : VPQ  
        vpq_entry_t *m_vpq;
        uint64_t m_vpq_size;
        uint64_t m_vpq_head;
        bool m_vpq_head_phase;
        uint64_t m_vpq_tail;
        bool m_vpq_tail_phase;
        uint64_t m_vpq_free_entries();
        
        // Mode Flags 
        bool perf_mode;
        bool oracle_conf_mode;

        // VPU 
        // measurement counters
        uint64_t vpmeas_ineligible;
        uint64_t vpmeas_eligible;
        uint64_t vpmeas_miss;
        uint64_t vpmeas_conf_corr;
        uint64_t vpmeas_conf_incorr;
        uint64_t vpmeas_unconf_corr;
        uint64_t vpmeas_unconf_incorr;

        // functions 
        bool  m_vpu_predict(uint64_t pc , uint64_t &pred_val, bool &confident, uint32_t  &vpq_index);

        // constructors 
        vpu_t(uint64_t vpq_size, uint64_t num_index_bits, 
             uint64_t num_tag_bits, uint64_t conf_max,
             bool perf_mode, bool oracle_conf_mode);

        // storage accounting
        void print_storage(FILE *stats_log);
};

#endif 