#!/bin/bash
lldb -o run -o "thread backtrace" -o quit -- "$@"