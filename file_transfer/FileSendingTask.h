#pragma once

#include <string>
#include <filesystem>
#include "FileSender.h"

void fileSendingTask(const std::string &server, int port,
                     bool use_tls = false, const std::string &ca_cert = "");
