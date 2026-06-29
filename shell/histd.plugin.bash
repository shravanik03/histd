#Define bash-preexec path
HISTD_PREEXEC_FILE="$HOME/.config/histd/bash-preexec.sh"

#If bash-preexec is not installed, download it
if [[ ! -f "$HISTD_PREEXEC_FILE" ]]; then
    mkdir -p "$HOME/.config/histd"
    curl -sS https://raw.githubusercontent.com/rcaloras/bash-preexec/master/bash-preexec.sh -o "$HISTD_PREEXEC_FILE"
fi

#Source bash-preexec
source "$HISTD_PREEXEC_FILE"

HISTD_SOCKET="$HOME/.local/share/histd/histd.sock"
HISTD_SESSION_ID="bash-$$"
HISTD_CMD=""
HISTD_START_TIME=""

histd_preexec() {
    #Skip empty commands and commands that start with hist
    if [[ -z "$1" ]] || [[ "${1:0:4}" == "hist" ]]; then
        return
    fi
    HISTD_CMD="$1"
    HISTD_START_TIME=$(awk "BEGIN {printf \"%d\", ${EPOCHREALTIME} * 1000}")
}

histd_precmd() {
    local exit_code=$?
    if [[ -z "$HISTD_CMD" ]]; then
        return
    fi
    local end_time=$(awk "BEGIN {printf \"%d\", ${EPOCHREALTIME} * 1000}")
    local duration=$(($end_time - $HISTD_START_TIME))
    local cwd=$(pwd)
    local timestamp=$(date +%s)
    local record_string="${HISTD_CMD}|${cwd}|${exit_code}|${duration}|${timestamp}|${HISTD_SESSION_ID}"

    #send record to socket with nc if socket exists
    if [[ -S "$HISTD_SOCKET" ]]; then
        echo "$record_string" | nc -w 0 -U "$HISTD_SOCKET" 2>/dev/null &
        disown
    fi

    HISTD_CMD=""
    HISTD_START_TIME=""
}

preexec_functions+=(histd_preexec)
precmd_functions+=(histd_precmd)