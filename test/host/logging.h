#pragma once
#include <cstdio>
#define HOST_LOG(module, ...) do { std::fprintf(stderr,"[%s] ",module); std::fprintf(stderr,__VA_ARGS__); std::fputc('\n',stderr); } while (0)
#define LOGI(module, ...) HOST_LOG(module,__VA_ARGS__)
#define LOGW(module, ...) HOST_LOG(module,__VA_ARGS__)
#define LOGE(module, ...) HOST_LOG(module,__VA_ARGS__)
