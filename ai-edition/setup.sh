#!/bin/sh
# TayebOS AI Edition - setup on Alpine Linux (run as root).
# Installs the lightest serious stack: Ollama (on-demand),
# labwc + foot, NetSurf (+ Falkon on request), lite-xl + micro,
# and the RAM-saving tuning from AI_GOAL.md.
set -e

echo "== TayebOS AI Edition setup (Alpine) =="

# --- packages ---
apk update
apk add labwc foot seatd xwayland \
        netsurf lite-xl micro \
        curl zram-init openrc

# Heavier modern-sites browser, only if you want it:
# apk add falkon

# --- Ollama: Alpine community package, else the official script ---
if ! apk add ollama 2>/dev/null; then
    echo "No Alpine ollama package - using the official installer"
    curl -fsSL https://ollama.com/install.sh | sh
fi

# --- Ollama tuning: one model, one request, unload after 2 min ---
mkdir -p /etc/profile.d
cat > /etc/profile.d/tayebos-ai.sh <<'EOF'
export OLLAMA_KEEP_ALIVE=2m
export OLLAMA_MAX_LOADED_MODELS=1
export OLLAMA_NUM_PARALLEL=1
export OLLAMA_FLASH_ATTENTION=1
EOF

# --- zram: compress half the RAM as swap (lz4) ---
cat > /etc/conf.d/zram-init <<'EOF'
load_on_start=yes
unload_on_stop=yes
num_devices=1
type0=swap
flag0="lz4"
size0=50%
EOF
rc-update add zram-init boot || true

# --- the `ai` command + model picker on PATH ---
install -m755 select-model.sh /usr/local/bin/select-model.sh
install -m755 ai /usr/local/bin/ai

echo "Done. Reboot, then type: ai"
echo "Model for this machine: $(select-model.sh)"
