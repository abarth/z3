/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    seq_decl_plugin.h

Abstract:

    <abstract>

Author:

    Nikolaj Bjorner (nbjorner) 2011-11-14

Revision History:

    Updated to string sequences 2015-12-5

--*/
#ifndef SEQ_DECL_PLUGIN_H_
#define SEQ_DECL_PLUGIN_H_

#include "ast.h"


enum seq_sort_kind {
    SEQ_SORT,
    RE_SORT,
    _STRING_SORT,  // internal only
    _CHAR_SORT     // internal only
};

enum seq_op_kind {
    OP_SEQ_UNIT,
    OP_SEQ_EMPTY,
    OP_SEQ_CONCAT,
    OP_SEQ_PREFIX,
    OP_SEQ_SUFFIX,
    OP_SEQ_CONTAINS,
    OP_SEQ_EXTRACT,
    OP_SEQ_REPLACE,
    OP_SEQ_AT,
    OP_SEQ_LENGTH,    
    OP_SEQ_INDEX,
    OP_SEQ_TO_RE,
    OP_SEQ_IN_RE,

    OP_RE_PLUS,
    OP_RE_STAR,
    OP_RE_OPTION,
    OP_RE_RANGE,
    OP_RE_CONCAT,
    OP_RE_UNION,
    OP_RE_INTERSECT,
    OP_RE_LOOP,
    OP_RE_EMPTY_SET,
    OP_RE_FULL_SET,
    OP_RE_OF_PRED,


    // string specific operators.
    OP_STRING_CONST,
    OP_STRING_ITOS, 
    OP_STRING_STOI, 
    OP_REGEXP_LOOP,    // TBD re-loop: integers as parameters or arguments?
    // internal only operators. Converted to SEQ variants.
    _OP_STRING_STRREPL, 
    _OP_STRING_CONCAT, 
    _OP_STRING_LENGTH, 
    _OP_STRING_STRCTN,
    _OP_STRING_PREFIX, 
    _OP_STRING_SUFFIX, 
    _OP_STRING_IN_REGEXP, 
    _OP_STRING_TO_REGEXP, 
    _OP_STRING_CHARAT, 
    _OP_STRING_SUBSTR,      
    _OP_STRING_STRIDOF, 
    _OP_SEQ_SKOLEM,
    LAST_SEQ_OP
};


class zstring {
public:
    enum encoding {
        ascii, 
        unicode
    };
private:    
    buffer<unsigned> m_buffer;
    encoding         m_encoding;
public:
    zstring(encoding enc = ascii);
    zstring(char const* s, encoding enc = ascii);
    zstring(zstring const& other);
    zstring(unsigned num_bits, bool const* ch);
    zstring(unsigned ch, encoding enc = ascii);
    zstring& operator=(zstring const& other);
    zstring replace(zstring const& src, zstring const& dst) const;
    unsigned num_bits() const { return (m_encoding==ascii)?8:16; }
    std::string encode() const; 
    unsigned length() const { return m_buffer.size(); }
    unsigned operator[](unsigned i) const { return m_buffer[i]; }
    bool empty() const { return m_buffer.empty(); }
    bool suffixof(zstring const& other) const;
    bool prefixof(zstring const& other) const;
    bool contains(zstring const& other) const;
    int  indexof(zstring const& other, int offset) const;
    zstring extract(int lo, int hi) const;
    zstring operator+(zstring const& other) const;
    std::ostream& operator<<(std::ostream& out) const;
};
    
class seq_decl_plugin : public decl_plugin {
    struct psig {
        symbol          m_name;
        unsigned        m_num_params;
        sort_ref_vector m_dom;
        sort_ref        m_range;
        psig(ast_manager& m, char const* name, unsigned n, unsigned dsz, sort* const* dom, sort* rng):
            m_name(name),
            m_num_params(n),
            m_dom(m),
            m_range(rng, m)
        {
            m_dom.append(dsz, dom);
        }
    };

    ptr_vector<psig> m_sigs;
    bool             m_init;
    symbol           m_stringc_sym;
    sort*            m_string;
    sort*            m_char;

    void match(psig& sig, unsigned dsz, sort* const* dom, sort* range, sort_ref& rng);

    void match_left_assoc(psig& sig, unsigned dsz, sort* const* dom, sort* range, sort_ref& rng);

    bool match(ptr_vector<sort>& binding, sort* s, sort* sP);

    sort* apply_binding(ptr_vector<sort> const& binding, sort* s);

    bool is_sort_param(sort* s, unsigned& idx);

    func_decl* mk_seq_fun(decl_kind k, unsigned arity, sort* const* domain, sort* range, decl_kind k_string);
    func_decl* mk_str_fun(decl_kind k, unsigned arity, sort* const* domain, sort* range, decl_kind k_seq);
    func_decl* mk_assoc_fun(decl_kind k, unsigned arity, sort* const* domain, sort* range, decl_kind k_string, decl_kind k_seq);

    void init();

    virtual void set_manager(ast_manager * m, family_id id);

public:
    seq_decl_plugin();

    virtual ~seq_decl_plugin() {}
    virtual void finalize();
   
    virtual decl_plugin * mk_fresh() { return alloc(seq_decl_plugin); }
    
    virtual sort * mk_sort(decl_kind k, unsigned num_parameters, parameter const * parameters);
    
    virtual func_decl * mk_func_decl(decl_kind k, unsigned num_parameters, parameter const * parameters, 
                                     unsigned arity, sort * const * domain, sort * range);
    
    virtual void get_op_names(svector<builtin_name> & op_names, symbol const & logic);
    
    virtual void get_sort_names(svector<builtin_name> & sort_names, symbol const & logic);
    
    virtual bool is_value(app * e) const;

    virtual bool is_unique_value(app * e) const { return is_value(e); }

    bool is_char(ast* a) const { return a == m_char; }

    app* mk_string(symbol const& s);  
    app* mk_string(zstring const& s);  
};

class seq_util {
    ast_manager& m;
    seq_decl_plugin& seq;
    family_id m_fid;
public:

    ast_manager& get_manager() const { return m; }

    bool is_string(sort* s) const { return is_seq(s) && seq.is_char(s->get_parameter(0).get_ast()); }
    bool is_seq(sort* s) const { return is_sort_of(s, m_fid, SEQ_SORT); }
    bool is_re(sort* s) const { return is_sort_of(s, m_fid, RE_SORT); }
    bool is_re(sort* s, sort*& seq) const { return is_sort_of(s, m_fid, RE_SORT)  && (seq = to_sort(s->get_parameter(0).get_ast()), true); }
    bool is_seq(expr* e) const  { return is_seq(m.get_sort(e)); }
    bool is_seq(sort* s, sort*& seq) { return is_seq(s) && (seq = to_sort(s->get_parameter(0).get_ast()), true); }
    bool is_re(expr* e) const { return is_re(m.get_sort(e)); }
    bool is_re(expr* e, sort*& seq) const { return is_re(m.get_sort(e), seq); }

    app* mk_skolem(symbol const& name, unsigned n, expr* const* args, sort* range);
    bool is_skolem(expr const* e) const { return is_app_of(e, m_fid, _OP_SEQ_SKOLEM); }

    class str {
        seq_util&    u;
        ast_manager& m;
        family_id    m_fid;

        app* mk_string(char const* s) { return mk_string(symbol(s)); }
        app* mk_string(std::string const& s) { return mk_string(symbol(s.c_str())); }


    public:
        str(seq_util& u): u(u), m(u.m), m_fid(u.m_fid) {}

        sort* mk_seq(sort* s) { parameter param(s); return m.mk_sort(m_fid, SEQ_SORT, 1, &param); }
        app* mk_empty(sort* s) { return m.mk_const(m.mk_func_decl(m_fid, OP_SEQ_EMPTY, 0, 0, 0, (expr*const*)0, s)); }
        app* mk_string(zstring const& s);
        app* mk_string(symbol const& s) { return u.seq.mk_string(s); }
        app* mk_concat(expr* a, expr* b) { expr* es[2] = { a, b }; return m.mk_app(m_fid, OP_SEQ_CONCAT, 2, es); }
        app* mk_concat(expr* a, expr* b, expr* c) {
            return mk_concat(mk_concat(a, b), c);
        }
        expr* mk_concat(unsigned n, expr* const* es) { if (n == 1) return es[0]; SASSERT(n > 1); return m.mk_app(m_fid, OP_SEQ_CONCAT, n, es); }
        app* mk_length(expr* a) { return m.mk_app(m_fid, OP_SEQ_LENGTH, 1, &a); }
        app* mk_substr(expr* a, expr* b, expr* c) { expr* es[3] = { a, b, c }; return m.mk_app(m_fid, OP_SEQ_EXTRACT, 3, es); }
        app* mk_contains(expr* a, expr* b) { expr* es[2] = { a, b }; return m.mk_app(m_fid, OP_SEQ_CONTAINS, 2, es); }
        app* mk_prefix(expr* a, expr* b) { expr* es[2] = { a, b }; return m.mk_app(m_fid, OP_SEQ_PREFIX, 2, es); }
        app* mk_suffix(expr* a, expr* b) { expr* es[2] = { a, b }; return m.mk_app(m_fid, OP_SEQ_SUFFIX, 2, es); }
        app* mk_index(expr* a, expr* b, expr* i) { expr* es[3] = { a, b, i}; return m.mk_app(m_fid, OP_SEQ_INDEX, 3, es); }
        app* mk_unit(expr* u) { return m.mk_app(m_fid, OP_SEQ_UNIT, 1, &u); }
        app* mk_char(zstring const& s, unsigned idx);



        bool is_string(expr const * n) const { return is_app_of(n, m_fid, OP_STRING_CONST); }

        bool is_string(expr const* n, symbol& s) const {
            return is_string(n) && (s = to_app(n)->get_decl()->get_parameter(0).get_symbol(), true);
        }
        
        bool is_string(expr const* n, zstring& s) const;

        bool is_empty(expr const* n) const { symbol s; 
            return is_app_of(n, m_fid, OP_SEQ_EMPTY) || (is_string(n, s) && !s.is_numerical() && *s.bare_str() == 0); 
        }
        bool is_concat(expr const* n)   const { return is_app_of(n, m_fid, OP_SEQ_CONCAT); }
        bool is_length(expr const* n)   const { return is_app_of(n, m_fid, OP_SEQ_LENGTH); }
        bool is_extract(expr const* n)  const { return is_app_of(n, m_fid, OP_SEQ_EXTRACT); }
        bool is_contains(expr const* n) const { return is_app_of(n, m_fid, OP_SEQ_CONTAINS); }
        bool is_at(expr const* n)       const { return is_app_of(n, m_fid, OP_SEQ_AT); }
        bool is_index(expr const* n)    const { return is_app_of(n, m_fid, OP_SEQ_INDEX); }
        bool is_replace(expr const* n)  const { return is_app_of(n, m_fid, OP_SEQ_REPLACE); }
        bool is_prefix(expr const* n)   const { return is_app_of(n, m_fid, OP_SEQ_PREFIX); }
        bool is_suffix(expr const* n)   const { return is_app_of(n, m_fid, OP_SEQ_SUFFIX); }
        bool is_itos(expr const* n)     const { return is_app_of(n, m_fid, OP_STRING_ITOS); }
        bool is_stoi(expr const* n)     const { return is_app_of(n, m_fid, OP_STRING_STOI); }
        bool is_in_re(expr const* n)    const { return is_app_of(n, m_fid, OP_SEQ_IN_RE); }
        bool is_unit(expr const* n)     const { return is_app_of(n, m_fid, OP_SEQ_UNIT); }

        
        MATCH_BINARY(is_concat);
        MATCH_UNARY(is_length);
        MATCH_TERNARY(is_extract);
        MATCH_BINARY(is_contains);
        MATCH_BINARY(is_at);
        MATCH_TERNARY(is_index);
        MATCH_TERNARY(is_replace);
        MATCH_BINARY(is_prefix);
        MATCH_BINARY(is_suffix);
        MATCH_UNARY(is_itos);
        MATCH_UNARY(is_stoi);
        MATCH_BINARY(is_in_re);        
        MATCH_UNARY(is_unit);

        void get_concat(expr* e, ptr_vector<expr>& es) const;
        expr* get_leftmost_concat(expr* e) const { expr* e1, *e2; while (is_concat(e, e1, e2)) e = e1; return e; }
    };

    class re {
        ast_manager& m;
        family_id    m_fid;
    public:
        re(seq_util& u): m(u.m), m_fid(u.m_fid) {}

        app* mk_to_re(expr* s) { return m.mk_app(m_fid, OP_SEQ_TO_RE, 1, &s); }
        app* mk_in_re(expr* s, expr* r) { return m.mk_app(m_fid, OP_SEQ_IN_RE, s, r); }
        app* mk_concat(expr* r1, expr* r2) { return m.mk_app(m_fid, OP_RE_CONCAT, r1, r2); }
        app* mk_union(expr* r1, expr* r2) { return m.mk_app(m_fid, OP_RE_UNION, r1, r2); }
        app* mk_inter(expr* r1, expr* r2) { return m.mk_app(m_fid, OP_RE_INTERSECT, r1, r2); }
        app* mk_star(expr* r) { return m.mk_app(m_fid, OP_RE_STAR, r); }
        app* mk_plus(expr* r) { return m.mk_app(m_fid, OP_RE_PLUS, r); }
        app* mk_opt(expr* r) { return m.mk_app(m_fid, OP_RE_OPTION, r); }        

        bool is_to_re(expr const* n)    const { return is_app_of(n, m_fid, OP_SEQ_TO_RE); }
        bool is_concat(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_CONCAT); }
        bool is_union(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_UNION); }
        bool is_inter(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_INTERSECT); }
        bool is_star(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_STAR); }
        bool is_plus(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_PLUS); }
        bool is_opt(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_OPTION); }
        bool is_range(expr const* n)    const { return is_app_of(n, m_fid, OP_RE_RANGE); }
        bool is_loop(expr const* n)    const { return is_app_of(n, m_fid, OP_REGEXP_LOOP); }
       
        MATCH_UNARY(is_to_re);
        MATCH_BINARY(is_concat);
        MATCH_BINARY(is_union);
        MATCH_BINARY(is_inter);
        MATCH_UNARY(is_star);
        MATCH_UNARY(is_plus);
        MATCH_UNARY(is_opt);
        
    };
    str str;
    re  re;

    seq_util(ast_manager& m): 
        m(m), 
        seq(*static_cast<seq_decl_plugin*>(m.get_plugin(m.mk_family_id("seq")))),
        m_fid(seq.get_family_id()),
        str(*this),
        re(*this) {        
    }

    ~seq_util() {}

    family_id get_family_id() const { return m_fid; }
    
};

#endif /* SEQ_DECL_PLUGIN_H_ */

