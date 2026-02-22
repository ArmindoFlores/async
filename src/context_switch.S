#if defined(__unix__) && defined(__x86_64)

.global _context_switch
.text
_context_switch:
    # rdi = from
    # rsi = to

    # Save callee-saved registers into *from
    mov %rsp, 0(%rdi)
    mov %rbx, 8(%rdi)
    mov %rbp, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r13, 32(%rdi)
    mov %r14, 40(%rdi)
    mov %r15, 48(%rdi)

    # Load callee-saved registers from *to
    mov 0(%rsi), %rsp
    mov 8(%rsi), %rbx
    mov 16(%rsi), %rbp
    mov 24(%rsi), %r12
    mov 32(%rsi), %r13
    mov 40(%rsi), %r14
    mov 48(%rsi), %r15

    ret

.section .note.GNU-stack,"",@progbits

#endif
