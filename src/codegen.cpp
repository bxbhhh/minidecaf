#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "codegen.h"

static bool debug_ = false;
const char* FUNC_EXIT = "function_exit";
const char* ELSE = "else";
const char* THEN = "then";
const char* EXIT = "exit";
const char* BEGIN = "begin";
const char* CONTINUE = "continue";
static FNPtr hot_func;
static NDPtr last_node;
static int label_seq, brk_seq, cont_seq;

void debug(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if(debug_)
        printf(fmt, ap);
}

void push(const char* reg) {
    debug("PUSH %s\n", reg);
    printf("  addi sp, sp, -%d\n", POINTER_WIDTH);
    printf("  sw %s, 0(sp)\n", reg);
}

void pop(const char* reg) {
    debug("POP %s\n", reg);
    printf("  lw %s, 0(sp)\n", reg);
    printf("  addi sp, sp, %d\n", POINTER_WIDTH);
}

inline int var_offset(VarPtr var) {
    return var->is_arg ? var->offset : -(var->offset + 3);
}

inline void store(const char* reg, int offset) {
    printf("  sw %s, %d(fp)\n", reg, offset * POINTER_WIDTH);
}

inline void load(const char* reg, int offset) {
    printf("  lw %s, %d(fp)\n", reg, offset * POINTER_WIDTH);
}

void store_var(const char* reg, VarPtr var) {
    store(reg, var_offset(var));
}

void load_var(const char* reg, VarPtr var) {
    load(reg, var_offset(var));
}

inline void label(const char* type, int seq) {
    printf(".L.%s.%d:\n", type, seq);
}

inline void jmp(const char* type, int seq) {
    printf("  j .L.%s.%d\n", type, seq);
}

inline void beqz(const char* reg, const char* type, int seq) {
    printf("  beqz %s, .L.%s.%d\n", reg, type, seq);
}

inline void bnez(const char* reg, const char* type, int seq) {
    printf("  bnez %s, .L.%s.%d\n", reg, type, seq);
}

inline bool check_lvalue(NDPtr node) {
    return node->kind == ND_VAR;
}

void gen(NDPtr node);

void gen_binary(NDPtr node) {
    debug("BINARY\n");
    gen(node->lexpr);
    gen(node->rexpr);
    pop("t1");
    pop("t0");
    switch (node->kind) {
    case ND_ADD:
        printf("  add t0, t0, t1\n");
        break;
    case ND_SUB:
        printf("  sub t0, t0, t1\n");
        break;
    case ND_MUL:
        printf("  mul t0, t0, t1\n");
        break;
    case ND_DIV:
        printf("  div t0, t0, t1\n");
        break;
    case ND_MOD:
        printf("  rem t0, t0, t1\n");
        break;
    case ND_LT:
        printf("  slt t0, t0, t1\n");
        break;        
    case ND_LTE:
        printf("  sgt t0, t0, t1\n");
        printf("  seqz t0, t0\n");
        break;        
    case ND_EQ:
        printf("  sub t0, t0, t1\n");
        printf("  seqz t0, t0\n");
        break;        
    case ND_NEQ: 
        printf("  sub t0, t0, t1\n");
        printf("  snez t0, t0\n");
        break;       
    case ND_LOGAND:
        printf("  snez t0, t0\n");
        printf("  snez t1, t1\n");
        printf("  and t0, t0, t1\n");
        break;
    case ND_LOGOR:
        printf("  or t0, t0, t1\n");
        printf("  snez t0, t0\n");
        break;
    default:
        assert(false);
    }
    push("t0");
    debug("BINARY END\n");
}

void gen(NDPtr node) {
    if(!node) 
        return;
    switch (node->kind) {
    // Statement
    case ND_RETURN:
        debug("RETURN\n");
        gen(node->lexpr);
        pop("a0");
        printf("  j .L.%s.%s\n", FUNC_EXIT, hot_func->name);
        break;
    case ND_DECL:
        debug("DECL\n");
        assert(node->var);
        if(node->var->init) {
            gen(node->var->init);
            pop("t0");
            store_var("t0", node->var);
        }
        break;
    case ND_UNUSED_EXPR:    
        debug("UNUSED\n");
        if(node->lexpr) {
            gen(node->lexpr);
            pop("t0");
        }
        break;
    case ND_IF: {
        debug("ND_IF");
        int seq = label_seq++;
        gen(node->cond);
        pop("t0");
        if (node->els) {
            beqz("t0", ELSE, seq);
            gen(node->then);
            jmp(EXIT, seq);
            label(ELSE, seq);
            gen(node->els);
        } else {
            beqz("t0", EXIT, seq);
            gen(node->then);
        }
        label(EXIT, seq);
        break;
    }
    case ND_BLOCK:
        for (auto n = node->body.begin(); n != node->body.end(); ++n)
            gen(*n);
        break;
    case ND_FOR: {
        int seq = label_seq++;
        // 使用局部变量备份　brk_seq, cont_seq, 便于后续恢复
        int brk = brk_seq;
        int cont = cont_seq;
        // brk_seq／cont_seq　总表示当前 break/continue 语句需要跳转到的序号
        // 在进入一个需要处理 break/continue 的结构时，需要更新。在上一步做了备份。
        brk_seq = cont_seq = seq;
        // 约定 for 语句结构如下
        // for (<init>; <condition>; <inc>)
        //     <statement>
        gen(node->init);
        // begin label，位于判断之前，inc 之后回到这里
        label(BEGIN, seq);
        // 如果没有条件语句，直接跳过这一部分，相当于 true
        if (node->cond) {
            gen(node->cond);
            pop("t0");
            // 判断为否则跳转到出口
            beqz("t0", EXIT, seq);
        }
        gen(node->then);
        // continue label, 位于 inc 语句之前, continue 跳转到这里
        label(CONTINUE, seq);
        if (node->inc)
            gen(node->inc);
        jmp(BEGIN, seq);
        label(EXIT, seq);
        // 恢复　brk_seq, cont_seq，使其仍表示当前 break/continue 语句需要跳转到的序号
        brk_seq = brk;
        cont_seq = cont;
        break;
    }
    case ND_WHILEDO: {
        int seq = label_seq++;
        int brk = brk_seq;
        int cont = cont_seq;
        brk_seq = cont_seq = seq;
        label(CONTINUE, seq);
        gen(node->cond);
        pop("t0");
        beqz("t0", EXIT, seq);
        gen(node->then);
        jmp(CONTINUE, seq);
        label(EXIT, seq);
        brk_seq = brk;
        cont_seq = cont;
        break;
    }
    case ND_DOWHILE: {
        int seq, brk, cont;
        seq = label_seq++;
        brk = brk_seq;
        cont = cont_seq;
        brk_seq = cont_seq = seq;
        label(CONTINUE, seq);
        gen(node->then);
        gen(node->cond);
        pop("t0");
        bnez("t0", CONTINUE, seq);
        brk_seq = brk;
        cont_seq = cont;
        break;
    }
    case ND_BREAK:
        jmp(EXIT, brk_seq);
        break;
    case ND_CONTINUE:
        jmp(CONTINUE, cont_seq);
        break;
    // Expression
    case ND_NUM:
        debug("NUM\n");
        printf("  li t0, %d\n", node->val);
        push("t0");
        break;
    case ND_NOT:
        debug("NOT\n");
        gen(node->lexpr);
        pop("t0");
        printf("  seqz t0, t0\n");
        push("t0");
        break;
    case ND_BITNOT:
        debug("BITNOT\n");
        gen(node->lexpr);
        pop("t0");
        printf("  not t0, t0\n");
        push("t0");
        break;
    case ND_NEG:
        debug("NEG\n");
        gen(node->lexpr);
        pop("t0");
        printf("  neg t0, t0\n");
        push("t0");
        break;
    case ND_VAR:
        debug("VAR\n");
        assert(node->var);
        load_var("t0", node->var);
        push("t0");
        break;
    case ND_ASSIGN:
        debug("ASSIGN\n");
        assert(check_lvalue(node->lexpr));
        // Left expr must be a variable
        gen(node->rexpr);
        pop("t0");
        store_var("t0", node->lexpr->var);
        push("t0");
        break;
    case ND_TERNARY: {
        debug("TERNARY\n");
        int seq = label_seq++;
        gen(node->cond);
        pop("t0");
        beqz("t0", ELSE, seq);
        gen(node->then);
        jmp(EXIT, seq);
        label(ELSE, seq);
        gen(node->els);
        label(EXIT, seq);
        break;
    }
    case ND_FUNC_CALL: {
        debug("FUNC CALL\n");
        assert(node->func_call);
        int nargs = 0;
        // 计算参数值，注意计算顺序
        // 为了简单将参数全部储存在栈上，所以不需要后续处理
        for (auto a = node->func_call->args.rbegin(); a != node->func_call->args.rend(); ++a) {
            gen(*a);
            ++nargs;
        }
        // 调用目标函数
        printf("  call %s\n", node->func_call->name);
        // 将在栈中的参数弹出
        if(nargs > 0)
            printf("  addi sp, sp, %d\n", nargs * POINTER_WIDTH);
        // 返回值压栈
        push("a0");
        break;
    }
    default:
        gen_binary(node);
    }
    last_node = node;
}

void gen_text(std::list<FNPtr> &func) {
    printf("  .text\n");
    for (auto f = func.begin(); f != func.end(); ++f) {
        FNPtr fn = hot_func = *f;
        last_node = NULL;
        printf("  .global %s\n", fn->name);
        printf("%s:\n", fn->name);
        // Prelogue
        printf("  addi sp, sp, -%d\n", fn->stack_size);
        printf("  sw ra, %d-4(sp)\n", fn->stack_size);
        printf("  sw fp, %d-8(sp)\n", fn->stack_size);
        printf("  addi fp, sp, %d\n", fn->stack_size);

        for (auto n = fn->stmts->body.begin(); n != fn->stmts->body.end(); ++n)
            gen(*n);
        
        // Missing return
        if(!strncmp(fn->name, "main", 4) && (!last_node || last_node->kind != ND_RETURN)) {
            printf("  li a0, 0\n");
        }
        // Epilogue
        printf(".L.%s.%s:\n", FUNC_EXIT, fn->name);
        printf("  lw fp, %d-8(sp)\n", fn->stack_size);
        printf("  lw ra, %d-4(sp)\n", fn->stack_size);
        printf("  addi sp, sp, %d\n", fn->stack_size);
        printf("  ret\n");
    }
}

void codegen(Program* prog, bool debug) {
    debug_ = debug;
    gen_text(prog->funcs);
}
