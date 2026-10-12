#!/usr/bin/env bash
# ============================================================
# uCore RISC-V 实验环境一键配置脚本（适用于 Ubuntu 22.04 / 24.04）
#
# 用法：在 Ubuntu 虚拟机终端里执行
#       bash setup_riscv.sh
#
# 会安装：RISC-V 交叉编译器、QEMU、GDB、以及 make/git/tmux 等基础工具
# ============================================================
set -euo pipefail

echo "==> [1/5] 更新软件源并安装基础工具"
sudo apt update
sudo apt install -y build-essential git wget curl tmux vim ctags nano

echo "==> [2/5] 安装 RISC-V 交叉编译器 (riscv64-unknown-elf-gcc)"
sudo apt install -y gcc-riscv64-unknown-elf

echo "==> [3/5] 安装 QEMU (qemu-system-riscv64)"
sudo apt install -y qemu-system-misc

echo "==> [4/5] 配置 GDB（用 gdb-multiarch 充当 riscv64-unknown-elf-gdb）"
sudo apt install -y gdb-multiarch
sudo ln -sf /usr/bin/gdb-multiarch /usr/local/bin/riscv64-unknown-elf-gdb

echo "==> [5/5] 验证安装"
echo "--- riscv64-unknown-elf-gcc ---"
riscv64-unknown-elf-gcc --version | head -n 1
echo "--- qemu-system-riscv64 ---"
qemu-system-riscv64 --version | head -n 1
echo "--- riscv64-unknown-elf-gdb ---"
riscv64-unknown-elf-gdb --version | head -n 1

echo ""
echo "✅ 环境配置完成！"
echo "接下来：cd 到 lab1 目录，执行 make qemu 即可运行内核（退出 QEMU：先 Ctrl+A 再按 x）。"
