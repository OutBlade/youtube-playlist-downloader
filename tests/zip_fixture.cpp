#include "../src/zip.hpp"
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path folder(argv[1]);
    const auto empty = folder / "empty.txt";
    std::ofstream(empty).close();
    make_zip(folder / "many.zip", std::vector<std::filesystem::path>(65536, empty));
}
