#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NO_COLOR='\033[0m'

MODULE_NAME="rootkit"

test_hide_unhide_module() {
    echo "===== Hide/Unhide Module Test =========="
    # Ensure toggle_module_visibility exists
    if [ -f "./toggle_module_visibility" ]; then
        echo "Hiding module..."
        sudo ./toggle_module_visibility > /dev/null

        # 確認 lsmod 看不到該模組
        if lsmod | grep "$MODULE_NAME" &> /dev/null ; then
            echo "Module $MODULE_NAME is still visible after toggle. Something went wrong."
            rmmod "$MODULE_NAME"
            return 1
        fi

        # 再次執行 toggle_module_visibility
        echo "Unhiding module..."
        sudo ./toggle_module_visibility > /dev/null

        # 確認 lsmod 又重新能夠看到該模組
        if ! lsmod | grep "$MODULE_NAME" &> /dev/null ; then
            echo "Module $MODULE_NAME is still hidden after second toggle. Something went wrong."
            return 1
        fi
    else
        echo "toggle_module_visibility file does not exist. Please compile it first."
        return 1
    fi

    echo -e "${GREEN}Passed${NO_COLOR}"
    return 0
}

test_masquerade() {
    echo "===== Masquerade Test ================="
    # Ensure masquerade exists
    if ! [ -f "./masquerade" ]; then
        echo "./masquerade file does not exist. Please compile it first."
        return 1
    else
        echo "Creating vim process..."
        vim > /dev/null 2>&1 &
        disown
        VIM_PID=$!

        echo "Masquerading vim to vi..."
        sudo ./masquerade vim vi > /dev/null

        # Get the new process name
        NEW_PROC_NAME=$(ps -p $VIM_PID -o comm=)

        # Check if the process name is changed
        if [ "$NEW_PROC_NAME" != "vi" ]; then
            echo "Process name is not changed after masquerade. Something went wrong."
            kill $VIM_PID
            return 1
        fi

        kill $VIM_PID

        # ========================================

        echo "Creating vim process..."
        vim > /dev/null 2>&1 &
        disown
        VIM_PID=$!

        echo "Masquerading vim to omg..."
        sudo ./masquerade vim omg > /dev/null

        # Get the new process name
        NEW_PROC_NAME=$(ps -p $VIM_PID -o comm=)

        # Ensure the process name is not changed
        if [ "$NEW_PROC_NAME" != "vim" ]; then
            echo "Process name is changed after masquerade. Something went wrong."
            kill $VIM_PID
            return 1
        fi

        kill $VIM_PID
    fi

    echo -e "${GREEN}Passed${NO_COLOR}"
    return 0
}

test_syscall_filter() {
    echo "===== Syscall Filter Test =============="
    # Ensure syscall_filter exists
    if ! [ -f "./syscall_filter" ]; then
        echo "./syscall_filter file does not exist. Please compile it first."
        return 1
    fi

    ./syscall_filter
    ret=$?
    if [ $ret -eq 0 ]; then
        echo -e "${GREEN}Passed${NO_COLOR}"
    fi
    return $ret
}

# Set pwd to the directory of this script
echo "===== Assignment 1 Test ================"
cd "$(dirname "$0")"
echo "Testing directory: $(pwd)"

echo "===== Build Test Programs =============="
make

if lsmod | grep "$MODULE_NAME" &> /dev/null ; then
    echo "Module $MODULE_NAME is loaded."
else
    echo "Module $MODULE_NAME is not loaded."
    exit 1
fi

test_hide_unhide_module
TEST1_RESULT=$?

test_masquerade
TEST2_RESULT=$?

test_syscall_filter
TEST3_RESULT=$?

echo "===== Result ==========================="

if [ $TEST1_RESULT -eq 0 ]; then
    echo -e "${GREEN}Hide/Unhide module test passed.${NO_COLOR}"
else
    echo -e "${RED}Hide/Unhide module test failed.${NO_COLOR}"
fi

if [ $TEST2_RESULT -eq 0 ]; then
    echo -e "${GREEN}Masquerade test passed.${NO_COLOR}"
else
    echo -e "${RED}Masquerade test failed.${NO_COLOR}"
fi

if [ $TEST3_RESULT -eq 0 ]; then
    echo -e "${GREEN}Syscall filter test passed.${NO_COLOR}"
else
    echo -e "${RED}Syscall filter test failed.${NO_COLOR}"
fi

exit 0