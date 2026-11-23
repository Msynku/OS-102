.text
.globl kernel_thread_entry
kernel_thread_entry:        # 新线程从 sret 返回后进入这里
	/* trapframe 已恢复 s0=线程函数、s1=参数；a0 是函数的第一个参数。 */
	move a0, s1       /* 将保存的 arg 放到 a0，作为入口函数的第一个参数 */
	jalr s0           /* 跳转到 s0 中的函数地址，即调用 fn(arg) */

	/* 函数返回后进入 do_exit；本实验中 do_exit 仍是 panic 占位。 */
	jal do_exit
