#!/bin/sh
# TayebOS AI Edition - pick the largest Ollama model (Q4_K_M) that
# fits comfortably in this machine's RAM, leaving room for the OS.
# Usage: ./select-model.sh [ram_mb_override]
# Prints just the model name on stdout.

RAM_MB="${1:-}"
if [ -z "$RAM_MB" ]; then
    KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    RAM_MB=$((KB / 1024))
fi

if   [ "$RAM_MB" -lt 3072 ];  then MODEL="qwen2.5:0.5b"
elif [ "$RAM_MB" -lt 5120 ];  then MODEL="llama3.2:1b"
elif [ "$RAM_MB" -lt 7168 ];  then MODEL="llama3.2:3b"
elif [ "$RAM_MB" -lt 10240 ]; then MODEL="phi3:mini"
elif [ "$RAM_MB" -lt 14336 ]; then MODEL="mistral"
elif [ "$RAM_MB" -lt 24576 ]; then MODEL="llama3.1:8b"
elif [ "$RAM_MB" -lt 49152 ]; then MODEL="qwen2.5:14b"
else                               MODEL="qwen2.5:32b"
fi

echo "$MODEL"
