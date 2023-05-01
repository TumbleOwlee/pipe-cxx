#include "UnixPipe.hxx"

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string(argv[1]) == "read") {
        UnixPipe pipe("/tmp/test-pipe", PipeAccess::Read);
        pipe.addCallback("NAMEDPIPE", [](std::string const& msg) {
            std::cout << "Callback: " << msg << std::endl;
        });
        pipe.start();
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    else if (argc > 1 && std::string(argv[1]) == "write") {
        UnixPipe pipe("/tmp/test-pipe", PipeAccess::Write);
        for (size_t idx = 0; idx < 60; ++idx) {
            std::cerr << "Write: " << idx << std::endl;
            pipe.write("NAMEDPIPE", "Some special message " + std::to_string(idx));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return 0;
}
