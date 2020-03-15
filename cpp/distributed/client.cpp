#include "../distributed/client.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/httplib/httplib.h"

using namespace std;

Client::RunParameters Client::getRunParameters() {
  RunParameters runParams;
  runParams.runId = "testrun";
  runParams.dataBoardLen = 19;
  runParams.inputsVersion = 7;
  runParams.maxSearchThreadsAllowed = 8;
  return runParams;
}


Client::Task Client::getNextTask(Logger& logger, const string& baseDir) {
  httplib::Client client("localhost", 3000);
  client.set_basic_auth("test", "katago123");

  auto res = client.Get("/api/users/");
  logger.write(res->body);

  Task task;
  task.taskId = "test";
  task.taskGroup = "testgroup";
  task.runId = "testrun";
  task.modelNameBlack = "g170-b10c128-s197428736-d67404019";
  task.modelNameWhite = "g170-b10c128-s197428736-d67404019";
  task.doWriteTrainingData = true;
  task.isEvaluationGame = false;

  string config = Global::readFile(baseDir + "/" + "testDistributedConfig.cfg");
  task.config = config;
  return task;
}

string Client::getModelPath(const string& modelName, const string& modelDir) {
  return modelDir + "/" + modelName + ".bin.gz";
}

void Client::downloadModelIfNotPresent(const string& modelName, const string& modelDir) {
  string path = getModelPath(modelName, modelDir);
  ifstream test(path.c_str());
  if(!test.good()) {
    throw StringError("Currently for testing, " + path + " is expected to be a valid KataGo model file");
  }
}

void Client::uploadTrainingData(const Task& task, const string& filePath) {
  cout << "UPLOAD TRAINING DATA " << task.taskId << " " << task.taskGroup << " " << task.runId << " " << filePath << endl;
}

void Client::uploadSGF(const Task& task, const string& filePath) {
  cout << "UPLOAD SGF " << task.taskId << " " << task.taskGroup << " " << task.runId << " " << filePath << endl;
}

Client::Connection::Connection(const std::string& url) {
  std::regex url_regex (R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)", std::regex::extended);
  std::smatch url_match_result;

  if (std::regex_match(url, url_match_result, url_regex)) {
    auto scheme = url_match_result.str(2);
    auto domain = url_match_result.str(4);

    if (url_match_result.str(1) == "https") {
      client = new httplib::SSLClient(domain)
    }
  } else {
    throw StringError("Either could not load latest neural net or access/write appropriate directories");
  }
}
