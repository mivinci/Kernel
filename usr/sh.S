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

  /* Read one character from stdin (fd 0) — non-blocking */
1:
  li    a7, 7           /* SYS_READ */
  li    a0, 0           /* stdin */
  .Lra: auipc a1, %pcrel_hi(inbuf)
  addi  a1, a1, %pcrel_lo(.Lra)
  li    a2, 1           /* 1 byte */
  ecall
  /* a0 = bytes read */

  /* If no data, yield and try again */
  bnez  a0, have_char
  li    a7, 3           /* SYS_YIELD */
  ecall
  j     1b

have_char:
  /* Check for CR or LF — ignore empty lines */
  .Lra2: auipc t0, %pcrel_hi(inbuf)
  addi  t0, t0, %pcrel_lo(.Lra2)
  lbu   t1, 0(t0)
  li    t2, 10          /* '\n' */
  beq   t1, t2, prompt
  li    t2, 13          /* '\r' */
  beq   t1, t2, prompt

  /* Read rest of line */
  li    t0, 0           /* chars read (starting fresh since first char is kept) */
  addi  t0, t0, 1       /* count the first char */
  li    t3, 10          /* '\n' for comparison later */

read_loop:
  li    a7, 7           /* SYS_READ */
  li    a0, 0           /* stdin */
  .Lrb: auipc a1, %pcrel_hi(inbuf)
  addi  a1, a1, %pcrel_lo(.Lrb)
  add   a1, a1, t0      /* buf + offset */
  li    a2, 1           /* 1 byte */
  ecall
  bnez  a0, got_char

  /* No data — yield and retry */
  li    a7, 3           /* SYS_YIELD */
  ecall
  j     read_loop

got_char:
  /* Check last char for newline */
  .Lcb: auipc t1, %pcrel_hi(inbuf)
  addi  t1, t1, %pcrel_lo(.Lcb)
  add   t1, t1, t0
  lbu   t4, 0(t1)
  beq   t4, t3, process_line
  /* Check buffer limit */
  li    t4, 63
  addi  t0, t0, 1
  blt   t0, t4, read_loop

process_line:
  /* Null-terminate the command */
  .Lta: auipc t1, %pcrel_hi(inbuf)
  addi  t1, t1, %pcrel_lo(.Lta)
  add   t1, t1, t0
  sb    x0, 0(t1)       /* inbuf[t0] = 0 */

  /* Print the received command for debugging */
  li    a7, 1           /* SYS_WRITE */
  li    a0, 1
  .Lca: auipc a1, %pcrel_hi(inbuf)
  addi  a1, a1, %pcrel_lo(.Lca)
  li    a2, 64
  ecall

  /* Try to match "x" (exit) */
  .Lcx: auipc a0, %pcrel_hi(inbuf)
  addi  a0, a0, %pcrel_lo(.Lcx)
  .Lxa: auipc a1, %pcrel_hi(exit_cmd)
  addi  a1, a1, %pcrel_lo(.Lxa)
  jal   ra, strcmp
  bnez  a0, try_hello
  /* Exit */
  li    a7, 2           /* SYS_EXIT */
  li    a0, 0
  ecall

try_hello:
  /* Try to match "hello" */
  .Lca2: auipc a0, %pcrel_hi(inbuf)
  addi  a0, a0, %pcrel_lo(.Lca2)
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

exit_cmd:
  .asciz "x"

hello_cmd:
  .asciz "hello"

hello_path:
  .asciz "/bin/hello"

  .balign 16
inbuf:
  .space 64               /* input buffer */
