#ifndef FUZZ_UTILS_H
#define FUZZ_UTILS_H

#include <random>
#include <string>
#include <vector>

std::vector<std::string> split_cmdline(const std::string& s);
std::string join_path(const std::string& a, const std::string& b);
std::string now_iso8601();
uint64_t now_mono_ms();
int mktemp_file(std::string& path, const std::string& prefix);
uint64_t seed_from_os();

#endif //FUZZ_UTILS_H
