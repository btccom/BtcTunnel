/*
 MIT License

 Copyright (c) 2016 BTC.COM

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <fstream>
#include <streambuf>

#include <glog/logging.h>

#include "Client.h"
#include "utilities_js.hpp"

Client *gClient = nullptr;

void handler(int sig) {
  if (gClient) {
    gClient->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\ttclient -c \"tclient_conf.json\" -l \"log_tclient\"\n");
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'l':
        optLogDir = optarg;
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  FLAGS_log_dir         = string(optLogDir);
  // Log messages at a level >= this flag are automatically sent to
  // stderr in addition to log files.
  FLAGS_stderrthreshold = 3;    // 3: FATAL
  FLAGS_max_log_size    = 100;  // max log file size 100 MB
  FLAGS_logbuflevel     = -1;   // don't buffer logs
  FLAGS_stop_logging_if_full_disk = true;

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    JsonNode j;  // conf json
    // parse xxxx.json
    std::ifstream agentConf(optConf);
    string agentJsonStr((std::istreambuf_iterator<char>(agentConf)),
                        std::istreambuf_iterator<char>());
    if (!JsonNode::parse(agentJsonStr.c_str(),
                         agentJsonStr.c_str() + agentJsonStr.length(), j)) {
      LOG(ERROR) << "json decode failure";
      exit(EXIT_FAILURE);
    }

    gClient = new Client(j["upstream_udp_host"].str(),  j["upstream_udp_port"].uint16(),
                         j["listen_tcp_ip"].str(),      j["listen_tcp_port"].uint16(),
                         j["tcp_read_timeout"].int32(), j["tcp_write_timeout"].int32());

    if (!gClient->setup()) {
      LOG(ERROR) << "setup failure";
    } else {
      gClient->run();
    }
    delete gClient;
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}
