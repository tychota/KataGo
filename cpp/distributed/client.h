#ifndef DISTRIBUTED_CLIENT_H_
#define DISTRIBUTED_CLIENT_H_

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/httplib/httplib.h"

#include "../core/logger.h"

namespace Client {

  class Connection {
  public:
    Connection(const std::string& url);
    ~Connection();

    void login(std::string& username, std::string& password);



    Connection(const Connection& other) = delete;
    Connection& operator=(const Connection& other) = delete;

  private:
    std::shared_ptr<httplib::Client> client;
  };

  struct RunParameters {
    std::string runId;
    int dataBoardLen;
    int inputsVersion;
    int maxSearchThreadsAllowed;
  };

  struct Task {
    std::string taskId;
    std::string taskGroup;
    std::string runId;

    std::string modelNameBlack;
    std::string modelNameWhite;

    std::string config;
    bool doWriteTrainingData;
    bool isEvaluationGame;
  };

  RunParameters getRunParameters();
  Task getNextTask(Logger& logger, const std::string& baseDir);
  std::string getModelPath(const std::string& modelName, const std::string& modelDir);
  void downloadModelIfNotPresent(const std::string& modelName, const std::string& modelDir);

  void uploadTrainingData(const Task& task, const std::string& filePath);
  void uploadSGF(const Task& task, const std::string& filePath);
}

#endif //DISTRIBUTED_CLIENT_H_
