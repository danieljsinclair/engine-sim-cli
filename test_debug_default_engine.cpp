#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstdlib>

volatile sig_atomic_t g_running = 1;

void signalHandler(int signal) {
    std::cout << "Signal received: " << signal << std::endl;
    g_running = 0;
}

int main() {
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
    
    std::cout << "Starting test..." << std::endl;
    
    // Run the CLI with default engine
    int result = system("./build/engine-sim-cli --default-engine --duration 0.1 --silent > /dev/null 2>&1");
    
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    std::cout << "CLI exit code: " << exitCode << std::endl;
    
    return exitCode;
}
