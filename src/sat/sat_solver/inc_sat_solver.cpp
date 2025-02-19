/*++
Copyright (c) 2014 Microsoft Corporation

Module Name:

    inc_sat_solver.cpp

Abstract:

    incremental solver based on SAT core.

Author:

    Nikolaj Bjorner (nbjorner) 2014-7-30

Notes:

--*/

#include "solver.h"
#include "tactical.h"
#include "sat_solver.h"
#include "tactic2solver.h"
#include "aig_tactic.h"
#include "propagate_values_tactic.h"
#include "max_bv_sharing_tactic.h"
#include "card2bv_tactic.h"
#include "bit_blaster_tactic.h"
#include "simplify_tactic.h"
#include "goal2sat.h"
#include "ast_pp.h"
#include "model_smt2_pp.h"
#include "filter_model_converter.h"
#include "bit_blaster_model_converter.h"
#include "ast_translation.h"

// incremental SAT solver.
class inc_sat_solver : public solver {
    ast_manager&    m;
    sat::solver     m_solver;
    goal2sat        m_goal2sat;
    params_ref      m_params;
    bool            m_optimize_model; // parameter
    expr_ref_vector m_fmls;
    expr_ref_vector m_asmsf;
    unsigned_vector m_fmls_lim;
    unsigned_vector m_asms_lim;
    unsigned_vector m_fmls_head_lim;
    unsigned            m_fmls_head;
    expr_ref_vector     m_core;
    atom2bool_var       m_map;
    model_ref           m_model;
    model_converter_ref m_mc;  
    bit_blaster_rewriter m_bb_rewriter; 
    tactic_ref          m_preprocess;
    unsigned            m_num_scopes;
    sat::literal_vector m_asms;
    goal_ref_buffer     m_subgoals;
    proof_converter_ref m_pc;   
    model_converter_ref m_mc2;   
    expr_dependency_ref m_dep_core;
    svector<double>     m_weights;

    typedef obj_map<expr, sat::literal> dep2asm_t;
public:
    inc_sat_solver(ast_manager& m, params_ref const& p):
        m(m), m_solver(p, m.limit(), 0), 
        m_params(p), m_optimize_model(false), 
        m_fmls(m), 
        m_asmsf(m),
        m_fmls_head(0),
        m_core(m), 
        m_map(m),
        m_bb_rewriter(m, p),
        m_num_scopes(0), 
        m_dep_core(m) {
        m_params.set_bool("elim_vars", false);
        m_solver.updt_params(m_params);
        params_ref simp2_p = p;
        simp2_p.set_bool("som", true);
        simp2_p.set_bool("pull_cheap_ite", true);
        simp2_p.set_bool("push_ite_bv", false);
        simp2_p.set_bool("local_ctx", true);
        simp2_p.set_uint("local_ctx_limit", 10000000);
        simp2_p.set_bool("flat", true); // required by som
        simp2_p.set_bool("hoist_mul", false); // required by som
        simp2_p.set_bool("elim_and", true);
        m_preprocess = 
            and_then(mk_card2bv_tactic(m, m_params),
                     using_params(mk_simplify_tactic(m), simp2_p),
                     mk_max_bv_sharing_tactic(m),
                     mk_bit_blaster_tactic(m, &m_bb_rewriter), 
                     //mk_aig_tactic(),
                     using_params(mk_simplify_tactic(m), simp2_p));               
    }
    
    virtual ~inc_sat_solver() {}
   
    virtual solver* translate(ast_manager& dst_m, params_ref const& p) {
        ast_translation tr(m, dst_m);
        if (m_num_scopes > 0) {
            throw default_exception("Cannot translate sat solver at non-base level");
        }
        inc_sat_solver* result = alloc(inc_sat_solver, dst_m, p);
        expr_ref fml(dst_m);
        for (unsigned i = 0; i < m_fmls.size(); ++i) {
            fml = tr(m_fmls[i].get());
            result->m_fmls.push_back(fml);
        }
        for (unsigned i = 0; i < m_asmsf.size(); ++i) {
            fml = tr(m_asmsf[i].get());
            result->m_asmsf.push_back(fml);
        }
        return result;
    }

    virtual void set_progress_callback(progress_callback * callback) {}

    virtual lbool check_sat(unsigned num_assumptions, expr * const * assumptions) { 
        return check_sat(num_assumptions, assumptions, 0, 0);
    }

    void display_weighted(std::ostream& out, unsigned sz, expr * const * assumptions, unsigned const* weights) {
        m_weights.reset();
        if (weights != 0) {
            for (unsigned i = 0; i < sz; ++i) m_weights.push_back(weights[i]);
        }
        m_solver.pop_to_base_level();
        dep2asm_t dep2asm;
        VERIFY(l_true == internalize_formulas());
        VERIFY(l_true == internalize_assumptions(sz, assumptions, dep2asm));
        svector<unsigned> nweights;
        for (unsigned i = 0; i < m_asms.size(); ++i) {
            nweights.push_back((unsigned) m_weights[i]);
        }
        m_solver.display_wcnf(out, m_asms.size(), m_asms.c_ptr(), nweights.c_ptr());
    }
 
    lbool check_sat(unsigned sz, expr * const * assumptions, double const* weights, double max_weight) {       
        m_weights.reset();
        if (weights != 0) {
            m_weights.append(sz, weights);
        }
        SASSERT(m_weights.empty() == (m_weights.c_ptr() == 0));
        m_solver.pop_to_base_level();
        dep2asm_t dep2asm;
        m_model = 0;
        lbool r = internalize_formulas();
        if (r != l_true) return r;
        r = internalize_assumptions(sz, assumptions, dep2asm);
        if (r != l_true) return r;

        r = m_solver.check(m_asms.size(), m_asms.c_ptr(), m_weights.c_ptr(), max_weight);
        switch (r) {
        case l_true:
            if (sz > 0 && !weights) {
                check_assumptions(dep2asm);
            }
            break;
        case l_false:
            // TBD: expr_dependency core is not accounted for.
            if (sz > 0) {
                extract_core(dep2asm);
            }
            break;
        default:
            break;
        }
        return r;
    }
    virtual void push() {
        internalize_formulas();
        m_solver.user_push();
        ++m_num_scopes;
        m_fmls_lim.push_back(m_fmls.size());
        m_asms_lim.push_back(m_asmsf.size());
        m_fmls_head_lim.push_back(m_fmls_head);
        m_bb_rewriter.push();
        m_map.push();
    }
    virtual void pop(unsigned n) {
        if (n < m_num_scopes) {   // allow inc_sat_solver to 
            n = m_num_scopes;     // take over for another solver.
        }
        m_bb_rewriter.pop(n);
        m_map.pop(n);
        SASSERT(n >= m_num_scopes);
        m_solver.user_pop(n);        
        m_num_scopes -= n;
        while (n > 0) {
            m_fmls_head = m_fmls_head_lim.back();
            m_fmls.resize(m_fmls_lim.back());
            m_fmls_lim.pop_back();
            m_fmls_head_lim.pop_back();
            m_asmsf.resize(m_asms_lim.back());
            m_asms_lim.pop_back();
            --n;
        }
    }
    virtual unsigned get_scope_level() const {
        return m_num_scopes;
    }
    virtual void assert_expr(expr * t, expr * a) {
        if (a) {
            m_asmsf.push_back(a);
            assert_expr(m.mk_implies(a, t));
        }
        else {
            assert_expr(t);
        }
    }
    virtual ast_manager& get_manager() { return m; }
    virtual void assert_expr(expr * t) {
        TRACE("sat", tout << mk_pp(t, m) << "\n";);
        m_fmls.push_back(t);
    }
    virtual void set_produce_models(bool f) {}
    virtual void collect_param_descrs(param_descrs & r) {
        goal2sat::collect_param_descrs(r);
        sat::solver::collect_param_descrs(r);
    }
    virtual void updt_params(params_ref const & p) {
        m_params = p;
        m_params.set_bool("elim_vars", false);
        m_solver.updt_params(m_params);
        m_optimize_model = m_params.get_bool("optimize_model", false);
    }    
    virtual void collect_statistics(statistics & st) const {
        m_preprocess->collect_statistics(st);
        m_solver.collect_statistics(st);
    }
    virtual void get_unsat_core(ptr_vector<expr> & r) {
        r.reset();
        r.append(m_core.size(), m_core.c_ptr());
    }
    virtual void get_model(model_ref & mdl) {
        if (!m_model.get()) {
            extract_model();
        }
        mdl = m_model;
    }
    virtual proof * get_proof() {
        UNREACHABLE();
        return 0;
    }
    virtual std::string reason_unknown() const {
        return "no reason given";
    }
    virtual void get_labels(svector<symbol> & r) {
    }
    virtual unsigned get_num_assertions() const {
        return m_fmls.size();
    }
    virtual expr * get_assertion(unsigned idx) const {
        return m_fmls[idx];
    }
    virtual unsigned get_num_assumptions() const {
        return m_asmsf.size();
    }
    virtual expr * get_assumption(unsigned idx) const {
        return m_asmsf[idx];
    }

private:


    lbool internalize_goal(goal_ref& g, dep2asm_t& dep2asm) {
        m_mc2.reset();
        m_pc.reset();
        m_dep_core.reset();
        m_subgoals.reset();
        m_preprocess->reset();
        SASSERT(g->models_enabled());
        SASSERT(!g->proofs_enabled());
        TRACE("sat", g->display(tout););        
        try {                   
            (*m_preprocess)(g, m_subgoals, m_mc2, m_pc, m_dep_core);
        }
        catch (tactic_exception & ex) {
            IF_VERBOSE(0, verbose_stream() << "exception in tactic " << ex.msg() << "\n";);
            return l_undef;                    
        }
        if (m_subgoals.size() != 1) {
            IF_VERBOSE(0, verbose_stream() << "size of subgoals is not 1, it is: " << m_subgoals.size() << "\n";);
            return l_undef;
        }
        CTRACE("sat", m_mc.get(), m_mc->display(tout); );
        g = m_subgoals[0];
        TRACE("sat", g->display_with_dependencies(tout););
        m_goal2sat(*g, m_params, m_solver, m_map, dep2asm, true);
        return l_true;
    }

    lbool internalize_assumptions(unsigned sz, expr* const* asms, dep2asm_t& dep2asm) {
        if (sz == 0) {
            return l_true;
        }
        goal_ref g = alloc(goal, m, true, true); // models and cores are enabled.
        for (unsigned i = 0; i < sz; ++i) {
            g->assert_expr(asms[i], m.mk_leaf(asms[i]));
        }
        lbool res = internalize_goal(g, dep2asm);
        if (res == l_true) {
            extract_assumptions(sz, asms, dep2asm);
        }        
        return res;
    }

    lbool internalize_formulas() {
        if (m_fmls_head == m_fmls.size()) {
            return l_true;
        }
        dep2asm_t dep2asm;
        goal_ref g = alloc(goal, m, true, false); // models, maybe cores are enabled
        for (; m_fmls_head < m_fmls.size(); ++m_fmls_head) {
            g->assert_expr(m_fmls[m_fmls_head].get());
        }
        return internalize_goal(g, dep2asm);
    }

    void extract_assumptions(unsigned sz, expr* const* asms, dep2asm_t& dep2asm) {
        m_asms.reset();
        unsigned j = 0;
        sat::literal lit;
        for (unsigned i = 0; i < sz; ++i) {
            if (dep2asm.find(asms[i], lit)) {
                m_asms.push_back(lit);
                if (i != j && !m_weights.empty()) {
                    m_weights[j] = m_weights[i];
                }
                ++j;
            }
        }
        SASSERT(dep2asm.size() == m_asms.size());
    }

    void extract_core(dep2asm_t& dep2asm) {
        u_map<expr*> asm2dep;
        dep2asm_t::iterator it = dep2asm.begin(), end = dep2asm.end();
        for (; it != end; ++it) {
            expr* e = it->m_key;
            asm2dep.insert(it->m_value.index(), e);
        }
        sat::literal_vector const& core = m_solver.get_core();
        TRACE("sat",
              dep2asm_t::iterator it2 = dep2asm.begin();
              dep2asm_t::iterator end2 = dep2asm.end();
              for (; it2 != end2; ++it2) {
                  tout << mk_pp(it2->m_key, m) << " |-> " << sat::literal(it2->m_value) << "\n";
              }
              tout << "core: ";
              for (unsigned i = 0; i < core.size(); ++i) {
                  tout << core[i] << " ";
              }
              tout << "\n";
              );              

        m_core.reset();
        for (unsigned i = 0; i < core.size(); ++i) {
            expr* e;
            VERIFY(asm2dep.find(core[i].index(), e));
            m_core.push_back(e);
        }
    }

    void check_assumptions(dep2asm_t& dep2asm) {
        sat::model const & ll_m = m_solver.get_model();
        dep2asm_t::iterator it = dep2asm.begin(), end = dep2asm.end();
        for (; it != end; ++it) {
            sat::literal lit = it->m_value;
            if (sat::value_at(lit, ll_m) != l_true) {
                IF_VERBOSE(0, verbose_stream() << mk_pp(it->m_key, m) << " does not evaluate to true\n";
                           verbose_stream() << m_asms << "\n";
                           m_solver.display_assignment(verbose_stream());
                           m_solver.display(verbose_stream()););
                throw default_exception("bad state");
            }
        }
    }

    void extract_model() {
        TRACE("sat", tout << "retrieve model\n";);
        if (!m_solver.model_is_current()) {
            m_model = 0;
            return;
        }
        sat::model const & ll_m = m_solver.get_model();
        model_ref md = alloc(model, m);
        atom2bool_var::iterator it  = m_map.begin();
        atom2bool_var::iterator end = m_map.end();
        for (; it != end; ++it) {
            expr * n   = it->m_key;
            if (is_app(n) && to_app(n)->get_num_args() > 0) {
                continue;
            }
            sat::bool_var v = it->m_value;
            switch (sat::value_at(v, ll_m)) {
            case l_true: 
                md->register_decl(to_app(n)->get_decl(), m.mk_true()); 
                break;
            case l_false:
                md->register_decl(to_app(n)->get_decl(), m.mk_false());
                break;
            default:
                break;
            }
        }
        m_model = md;
        if (m_mc || !m_bb_rewriter.const2bits().empty()) {
            model_converter_ref mc = m_mc;
            if (!m_bb_rewriter.const2bits().empty()) {
                mc = concat(mc.get(), mk_bit_blaster_model_converter(m, m_bb_rewriter.const2bits())); 
            }
            (*mc)(m_model);
        }
        SASSERT(m_model);

        DEBUG_CODE(
            for (unsigned i = 0; i < m_fmls.size(); ++i) {
                expr_ref tmp(m);
                VERIFY(m_model->eval(m_fmls[i].get(), tmp));                
                CTRACE("sat", !m.is_true(tmp),
                       tout << "Evaluation failed: " << mk_pp(m_fmls[i].get(), m) 
                       << " to " << tmp << "\n";
                       model_smt2_pp(tout, m, *(m_model.get()), 0););
                SASSERT(m.is_true(tmp));
            });
    }
};


solver* mk_inc_sat_solver(ast_manager& m, params_ref const& p) {
    return alloc(inc_sat_solver, m, p);
}


lbool inc_sat_check_sat(solver& _s, unsigned sz, expr*const* soft, rational const* _weights, rational const& max_weight) {
    inc_sat_solver& s = dynamic_cast<inc_sat_solver&>(_s);
    vector<double> weights;
    for (unsigned i = 0; _weights && i < sz; ++i) {
        weights.push_back(_weights[i].get_double());
    }
    return s.check_sat(sz, soft, weights.c_ptr(), max_weight.get_double());
}

void inc_sat_display(std::ostream& out, solver& _s, unsigned sz, expr*const* soft, rational const* _weights) {
    inc_sat_solver& s = dynamic_cast<inc_sat_solver&>(_s);
    vector<unsigned> weights;
    for (unsigned i = 0; _weights && i < sz; ++i) {
        if (!_weights[i].is_unsigned()) {
            throw default_exception("Cannot display weights that are not integers");
        }
        weights.push_back(_weights[i].get_unsigned());
    }
    return s.display_weighted(out, sz, soft, weights.c_ptr());
}

