# Simple user-mode shell — runs under init via SYS_SPAWN.
# Reads commands from stdin (fd 0) and spawns programs.

  .text
  .globl _start
_start:
prompt:
  /* SYS_WRITE: print "$ " */
  li    a7, 1           /* SYS_WRITE */
  li    a0, 1           /* stdout */
  .Lpa: auipc a1, %pcrel_hi(prompt_str)
  addi  a1, a1, %pcrel_lo(.Lpa)
  li    a2, 2           /* len */
  ecall

  /* Read one character from stdin (fd 0) */
  li    a7, 7           /* SYS_READ */
  li    a0, 0           /* stdin */
  .Lra: auipc a1, %pcrel_hi(inbuf)
  addi  a1, a1, %pcrel_lo(.Lra)
  li    a2, 1           /* 1 byte */
  ecall
  /* a0 = bytes read */

  /* If no data, skip */
  beqz  a0, prompt

  /* Read rest of line until newline or more data */
  li    t0, 0           /* total read */
1:
  addi  t0, t0, 1       /* one char already read */
  .Lrb: auipc a1, %pcrel_hi(inbuf)
  addi  a1, a1, %pcrel_lo(.Lrb)
  add   a1, a1, t0      /* buf + offset */
  li    a7, 7           /* SYS_READ */
  li    a0, 0           /* stdin */
  li    a2, 1           /* 1 byte */
  ecall
  beqz  a0, 2f          /* no more data -> process */
  /* Check for newline */
  .Lcb: auipc t1, %pcrel_hi(inbuf)
  addi  t1, t1, %pcrel_lo(.Lcb)
  add   t1, t1, t0
  lbu   t2, 0(t1)
  li    t3, 10          /* '\n' */
  beq   t2, t3, 2f      /* newline -> process */
  /* Check buffer limit */
  li    t3, 63
  bge   t0, t3, 2f      /* full -> process */
  j     1b

2:
  /* Null-terminate */
  .Lta: auipc t1, %pcrel_hi(inbuf)
  addi  t1, t1, %pcrel_lo(.Lta)
  add   t1, t1, t0
  sb    x0, 0(t1)       /* inbuf[t0] = 0 */

  /* Try to match "hello" */
  .Lca: auipc a0, %pcrel_hi(inbuf)
  addi  a0, a0, %pcrel_lo(.Lca)
  .Lha: auipc a1, %pcrel_hi(hello_cmd)
  addi  a1, a1, %pcrel_lo(.Lha)
  jal   ra, strcmp
  bnez  a0, not_found
  /* Spawn hello */
  li    a7, 8           /* SYS_SPAWN */
  .Lda: auipc a0, %pcrel_hi(hello_path)
  addi  a0, a0, %pcrel_lo(.Lda)
  ecall
  j     prompt

not_found:
  /* Print "not found\n" */
  li    a7, 1           /* SYS_WRITE */
  li    a0, 1
  .Lna: auipc a1, %pcrel_hi(not_found_str)
  addi  a1, a1, %pcrel_lo(.Lna)
  li    a2, 11
  ecall
  j     prompt

# strcmp(s1, s2) -> 0 if equal, non-zero if not
strcmp:
  lbu   t0, 0(a0)
  lbu   t1, 0(a1)
  bne   t0, t1, 1f
  beqz  t0, 2f             /* both null -> equal */
  addi  a0, a0, 1
  addi  a1, a1, 1
  j     strcmp
1:
  li    a0, 1              /* not equal */
  ret
2:
  li    a0, 0              /* equal */
  ret

  .balign 4
prompt_str:
  .asciz "$ "

not_found_str:
  .asciz "not found\n\r"

hello_cmd:
  .asciz "hello"

hello_path:
  .asciz "/bin/hello"

  .balign 16
inbuf:
  .space 64               /* input buffer */
