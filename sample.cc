#include "program_options.hpp"

int main(int argc, char* argv[]) {
    namespace PO = program_options;
    PO::parser parser;

    parser.add<std::string>("host", 'h', "host name", true, "")
        .add<int>("port", 'p', "port number", false, 80, PO::range(1, 65535))
        .add<std::string>("type", 't', "protocol type", false, "http",
                          PO::oneof<std::string>("http", "https", "ssh", "ftp"))
        .add("gzip", 0, "gzip when transfer")
        .add("help", 0, "print this message");
    parser.set_footer("filename ...");
    parser.set_program_name("sample");

    parser.parse_check(argc, argv);

    std::cout << parser.get<std::string>("type") << "://"
              << parser.get<std::string>("host") << ":"
              << parser.get<int>("port") << std::endl;
    if (parser.exist("gzip")) std::cout << "gzip" << std::endl;
    for (auto& item : parser.rest()) {
        std::cout << "- " << item << std::endl;
    }

    return 0;
}
