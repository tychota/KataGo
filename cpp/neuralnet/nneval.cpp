
#include "../neuralnet/nneval.h"

//-------------------------------------------------------------------------------------

NNResultBuf::NNResultBuf()
  :clientWaitingForResult(),resultMutex(),hasResult(false),includeOwnerMap(false),result(nullptr),errorLogLockout(false)
{}

NNResultBuf::~NNResultBuf()
{}

//-------------------------------------------------------------------------------------

NNServerBuf::NNServerBuf(const NNEvaluator& nnEval, const LoadedModel* model)
  :inputBuffers(NULL),
   resultBufs(NULL)
{
  int maxNumRows = nnEval.getMaxBatchSize();
  if(model != NULL)
    inputBuffers = NeuralNet::createInputBuffers(model,maxNumRows);
  resultBufs = new NNResultBuf*[maxNumRows];
  for(int i = 0; i < maxNumRows; i++)
    resultBufs[i] = NULL;
}

NNServerBuf::~NNServerBuf() {
  if(inputBuffers != NULL)
    NeuralNet::freeInputBuffers(inputBuffers);
  inputBuffers = NULL;
  //Pointers inside here don't need to be deleted, they simply point to the clients waiting for results
  delete[] resultBufs;
  resultBufs = NULL;
}

//-------------------------------------------------------------------------------------

NNEvaluator::NNEvaluator(
  const string& pbModelFile,
  int modelFileIdx,
  int maxBatchSize,
  int pLen,
  bool iUseNHWC,
  int nnCacheSizePowerOfTwo,
  bool skipNeuralNet
)
  :modelFileName(pbModelFile),
   posLen(pLen),
   policySize(NNPos::getPolicySize(pLen)),
   inputsUseNHWC(iUseNHWC),
   loadedModel(NULL),
   nnCacheTable(NULL),
   debugSkipNeuralNet(skipNeuralNet),
   serverThreads(),
   clientWaitingForRow(),serverWaitingForBatchStart(),serverWaitingForBatchFinish(),
   bufferMutex(),
   isKilled(false),
   serverTryingToGrabBatch(false),
   maxNumRows(maxBatchSize),
   m_numRowsStarted(0),
   m_numRowsFinished(0),
   m_numRowsProcessed(0),
   m_numBatchesProcessed(0),
   m_inputBuffers(NULL),
   m_resultBufs(NULL)
{
  if(posLen > NNPos::MAX_BOARD_LEN)
    throw StringError("Maximum supported nnEval board size is " + Global::intToString(NNPos::MAX_BOARD_LEN));

  if(nnCacheSizePowerOfTwo >= 0)
    nnCacheTable = new NNCacheTable(nnCacheSizePowerOfTwo);

  if(!debugSkipNeuralNet) {
    loadedModel = NeuralNet::loadModelFile(pbModelFile, modelFileIdx);
    m_inputBuffers = NeuralNet::createInputBuffers(loadedModel,maxBatchSize);
    modelVersion = NeuralNet::getModelVersion(loadedModel);
    inputsVersion = NNModelVersion::getInputsVersion(modelVersion);
  }
  else {
    modelVersion = NNModelVersion::latestModelVersionImplemented;
    inputsVersion = NNModelVersion::getInputsVersion(modelVersion);
  }

  m_resultBufs = new NNResultBuf*[maxBatchSize];
  for(int i = 0; i < maxBatchSize; i++)
    m_resultBufs[i] = NULL;
}

NNEvaluator::~NNEvaluator()
{
  killServerThreads();
  assert(!serverTryingToGrabBatch);

  if(m_inputBuffers != NULL)
    NeuralNet::freeInputBuffers(m_inputBuffers);
  m_inputBuffers = NULL;

  //Pointers inside here don't need to be deleted, they simply point to the clients waiting for results
  delete[] m_resultBufs;
  m_resultBufs = NULL;

  if(loadedModel != NULL)
    NeuralNet::freeLoadedModel(loadedModel);
  loadedModel = NULL;

  delete nnCacheTable;
}

string NNEvaluator::getModelFileName() const {
  return modelFileName;
}
int NNEvaluator::getMaxBatchSize() const {
  return maxNumRows;
}
int NNEvaluator::getPosLen() const {
  return posLen;
}

uint64_t NNEvaluator::numRowsProcessed() const {
  return m_numRowsProcessed.load(std::memory_order_relaxed);
}
uint64_t NNEvaluator::numBatchesProcessed() const {
  return m_numBatchesProcessed.load(std::memory_order_relaxed);
}
double NNEvaluator::averageProcessedBatchSize() const {
  return (double)numRowsProcessed() / (double)numBatchesProcessed();
}

void NNEvaluator::clearStats() {
  m_numRowsProcessed.store(0);
  m_numBatchesProcessed.store(0);
}

void NNEvaluator::clearCache() {
  if(nnCacheTable != NULL)
    nnCacheTable->clear();
}

static void serveEvals(
  int threadIdx, bool doRandomize, string randSeed, int defaultSymmetry, Logger* logger,
  NNEvaluator* nnEval, const LoadedModel* loadedModel,
  int cudaGpuIdxForThisThread,
  bool cudaUseFP16,
  bool cudaUseNHWC
) {
  NNServerBuf* buf = new NNServerBuf(*nnEval,loadedModel);
  Rand rand(randSeed + ":NNEvalServerThread:" + Global::intToString(threadIdx));
  ostream* logStream = logger->createOStream();
  try {
    nnEval->serve(*buf,rand,logger,doRandomize,defaultSymmetry,cudaGpuIdxForThisThread,cudaUseFP16,cudaUseNHWC);
  }
  catch(const exception& e) {
    (*logStream) << "ERROR: NNEval Server Thread " << threadIdx << " failed: " << e.what() << endl;
  }
  catch(const string& e) {
    (*logStream) << "ERROR: NNEval Server Thread " << threadIdx << " failed: " << e << endl;
  }
  catch(...) {
    (*logStream) << "ERROR: NNEval Server Thread " << threadIdx << " failed with unexpected throw" << endl;
  }
  delete logStream;
  delete buf;
}

void NNEvaluator::spawnServerThreads(
  int numThreads,
  bool doRandomize,
  string randSeed,
  int defaultSymmetry,
  Logger& logger,
  vector<int> cudaGpuIdxByServerThread,
  bool cudaUseFP16,
  bool cudaUseNHWC
) {
  if(serverThreads.size() != 0)
    throw StringError("NNEvaluator::spawnServerThreads called when threads were already running!");
  if(cudaGpuIdxByServerThread.size() != numThreads)
    throw StringError("cudaGpuIdxByServerThread.size() != numThreads");

  for(int i = 0; i<numThreads; i++) {
    int cudaGpuIdxForThisThread = cudaGpuIdxByServerThread[i];
    std::thread* thread = new std::thread(
      &serveEvals,i,doRandomize,randSeed,defaultSymmetry,&logger,this,loadedModel,cudaGpuIdxForThisThread,cudaUseFP16,cudaUseNHWC
    );
    serverThreads.push_back(thread);
  }
}

void NNEvaluator::killServerThreads() {
  unique_lock<std::mutex> lock(bufferMutex);
  isKilled = true;
  lock.unlock();
  serverWaitingForBatchStart.notify_all();
  serverWaitingForBatchFinish.notify_all();

  for(size_t i = 0; i<serverThreads.size(); i++)
    serverThreads[i]->join();
  for(size_t i = 0; i<serverThreads.size(); i++)
    delete serverThreads[i];
  serverThreads.clear();

  //Can unset now that threads are dead
  isKilled = false;
}

void NNEvaluator::serve(
  NNServerBuf& buf, Rand& rand, Logger* logger, bool doRandomize, int defaultSymmetry,
  int cudaGpuIdxForThisThread, bool cudaUseFP16, bool cudaUseNHWC
) {

  LocalGpuHandle* gpuHandle = NULL;
  if(loadedModel != NULL)
    gpuHandle = NeuralNet::createLocalGpuHandle(loadedModel, logger, maxNumRows, posLen, inputsUseNHWC, cudaGpuIdxForThisThread, cudaUseFP16, cudaUseNHWC);
  
  vector<NNOutput*> outputBuf;

  unique_lock<std::mutex> lock(bufferMutex,std::defer_lock);
  while(true) {
    lock.lock();
    while((m_numRowsStarted <= 0 || serverTryingToGrabBatch) && !isKilled)
      serverWaitingForBatchStart.wait(lock);

    if(isKilled)
      break;

    serverTryingToGrabBatch = true;
    while(m_numRowsFinished < m_numRowsStarted && !isKilled)
      serverWaitingForBatchFinish.wait(lock);

    if(isKilled)
      break;

    //It should only be possible for one thread to make it through to here
    assert(serverTryingToGrabBatch);
    assert(m_numRowsFinished > 0);

    int numRows = m_numRowsFinished;
    if(m_inputBuffers != NULL)
      std::swap(m_inputBuffers,buf.inputBuffers);
    else
      assert(debugSkipNeuralNet);
    
    std::swap(m_resultBufs,buf.resultBufs);

    m_numRowsStarted = 0;
    m_numRowsFinished = 0;
    serverTryingToGrabBatch = false;
    clientWaitingForRow.notify_all();
    lock.unlock();

    if(debugSkipNeuralNet) {
      for(int row = 0; row < numRows; row++) {
        assert(buf.resultBufs[row] != NULL);
        NNResultBuf* resultBuf = buf.resultBufs[row];
        buf.resultBufs[row] = NULL;

        unique_lock<std::mutex> resultLock(resultBuf->resultMutex);
        assert(resultBuf->hasResult == false);
        resultBuf->result = std::make_shared<NNOutput>();
        float* policyProbs = resultBuf->result->policyProbs;
        //At this point, these aren't probabilities, since this is before the postprocessing
        //that happens for each result. These just need to be unnormalized log probabilities.
        //Illegal move filtering happens later.
        for(int i = 0; i<policySize; i++)
          policyProbs[i] = rand.nextGaussian();
        for(int i = policySize; i<NNPos::MAX_NN_POLICY_SIZE; i++)
          policyProbs[i] = 0;

        resultBuf->result->posLen = posLen;
        if(resultBuf->includeOwnerMap) {
          float* ownerMap = new float[posLen*posLen];
          for(int i = 0; i<posLen*posLen; i++)
            ownerMap[i] = rand.nextGaussian() * 0.20;
          resultBuf->result->ownerMap = ownerMap;
        }
        else {
          resultBuf->result->ownerMap = NULL;
        }

        //These aren't really probabilities. Win/Loss/NoResult will get softmaxed later
        //(or in the case of model version 2, it will only just pay attention to the value of whiteWinProb and tanh it)
        double whiteWinProb = 0.0 + rand.nextGaussian() * 0.20;
        double whiteLossProb = 0.0 + rand.nextGaussian() * 0.20;
        double whiteScoreValue = 0.0 + rand.nextGaussian() * 0.20;
        double whiteNoResultProb = 0.0 + rand.nextGaussian() * 0.20;
        resultBuf->result->whiteWinProb = whiteWinProb;
        resultBuf->result->whiteLossProb = whiteLossProb;
        resultBuf->result->whiteNoResultProb = whiteNoResultProb;
        resultBuf->result->whiteScoreValue = whiteScoreValue;
        resultBuf->hasResult = true;
        resultBuf->clientWaitingForResult.notify_all();
        resultLock.unlock();
      }
      continue;
    }

    int symmetry = defaultSymmetry;
    if(doRandomize)
      symmetry = rand.nextUInt(NNInputs::NUM_SYMMETRY_COMBINATIONS);
    bool* symmetriesBuffer = NeuralNet::getSymmetriesInplace(buf.inputBuffers);
    symmetriesBuffer[0] = (symmetry & 0x1) != 0;
    symmetriesBuffer[1] = (symmetry & 0x2) != 0;
    symmetriesBuffer[2] = (symmetry & 0x4) != 0;

    outputBuf.clear();
    for(int row = 0; row<numRows; row++) {
      NNOutput* emptyOutput = new NNOutput();
      assert(buf.resultBufs[row] != NULL);
      emptyOutput->posLen = posLen;
      if(buf.resultBufs[row]->includeOwnerMap)
        emptyOutput->ownerMap = new float[posLen*posLen];
      else
        emptyOutput->ownerMap = NULL;
      outputBuf.push_back(emptyOutput);
    }

    NeuralNet::getOutput(gpuHandle, buf.inputBuffers, numRows, outputBuf);
    assert(outputBuf.size() == numRows);

    for(int row = 0; row < numRows; row++) {
      assert(buf.resultBufs[row] != NULL);
      NNResultBuf* resultBuf = buf.resultBufs[row];
      buf.resultBufs[row] = NULL;

      unique_lock<std::mutex> resultLock(resultBuf->resultMutex);
      assert(resultBuf->hasResult == false);
      resultBuf->result = std::shared_ptr<NNOutput>(outputBuf[row]);
      resultBuf->hasResult = true;
      resultBuf->clientWaitingForResult.notify_all();
      resultLock.unlock();
    }

    m_numRowsProcessed.fetch_add(numRows, std::memory_order_relaxed);
    m_numBatchesProcessed.fetch_add(1, std::memory_order_relaxed);
    continue;
  }

  NeuralNet::freeLocalGpuHandle(gpuHandle);
}

void NNEvaluator::evaluate(
  Board& board,
  const BoardHistory& history,
  Player nextPlayer,
  NNResultBuf& buf,
  ostream* logStream,
  bool skipCache,
  bool includeOwnerMap
) {
  assert(!isKilled);
  buf.hasResult = false;

  if(board.x_size > posLen || board.y_size > posLen)
    throw StringError("NNEvaluator was configured with posLen = " + Global::intToString(posLen) + " but was asked to evaluate board with larger x or y size");

  Hash128 nnHash;
  if(inputsVersion == 1)
    nnHash = NNInputs::getHashV1(board, history, nextPlayer);
  else if(inputsVersion == 2)
    nnHash = NNInputs::getHashV2(board, history, nextPlayer);
  else
    assert(false);

  if(nnCacheTable != NULL && !skipCache && nnCacheTable->get(nnHash,buf.result)) {
    if(!(includeOwnerMap && buf.result->ownerMap == NULL))
    {
      buf.hasResult = true;
      return;
    }
  }
  buf.includeOwnerMap = includeOwnerMap;

  unique_lock<std::mutex> lock(bufferMutex);
  while(m_numRowsStarted >= maxNumRows || serverTryingToGrabBatch)
    clientWaitingForRow.wait(lock);

  int rowIdx = m_numRowsStarted;
  m_numRowsStarted += 1;

  if(!debugSkipNeuralNet) {
    assert(m_inputBuffers != NULL);
    float* rowInput = NeuralNet::getRowInplace(m_inputBuffers,rowIdx);

    if(m_numRowsStarted == 1)
      serverWaitingForBatchStart.notify_one();
    lock.unlock();

    if(inputsVersion == 1)
      NNInputs::fillRowV1(board, history, nextPlayer, posLen, inputsUseNHWC, rowInput);
    else if(inputsVersion == 2)
      NNInputs::fillRowV2(board, history, nextPlayer, posLen, inputsUseNHWC, rowInput);
    else
      assert(false);

    lock.lock();
  }
  else {
    if(m_numRowsStarted == 1)
      serverWaitingForBatchStart.notify_one();    
  }
  
  m_resultBufs[rowIdx] = &buf;
  m_numRowsFinished += 1;
  if(m_numRowsFinished >= m_numRowsStarted)
    serverWaitingForBatchFinish.notify_all();
  lock.unlock();

  unique_lock<std::mutex> resultLock(buf.resultMutex);
  while(!buf.hasResult)
    buf.clientWaitingForResult.wait(resultLock);
  resultLock.unlock();

  //Perform postprocessing on the result - turn the nn output into probabilities
  float* policy = buf.result->policyProbs;

  int xSize = board.x_size;
  int ySize = board.y_size;

  float maxPolicy = -1e25f;
  bool isLegal[policySize];
  int legalCount = 0;
  for(int i = 0; i<policySize; i++) {
    Loc loc = NNPos::posToLoc(i,xSize,ySize,posLen);
    isLegal[i] = history.isLegal(board,loc,nextPlayer);

    float policyValue;
    if(isLegal[i]) {
      legalCount += 1;
      policyValue = policy[i];
    }
    else {
      policyValue = -1e30f;
      policy[i] = policyValue;
    }

    if(policyValue > maxPolicy)
      maxPolicy = policyValue;
  }

  assert(legalCount > 0);

  float policySum = 0.0f;
  for(int i = 0; i<policySize; i++) {
    policy[i] = exp(policy[i] - maxPolicy);
    policySum += policy[i];
  }

  //Somehow all legal moves rounded to 0 probability
  if(policySum <= 0.0) {
    if(!buf.errorLogLockout && logStream != NULL) {
      buf.errorLogLockout = true;
      (*logStream) << "Warning: all legal moves rounded to 0 probability for " << modelFileName << " in position " << board << endl;
    }
    float uniform = 1.0f / legalCount;
    for(int i = 0; i<policySize; i++) {
      policy[i] = isLegal[i] ? uniform : -1.0f;
    }
  }
  //Normal case
  else {
    for(int i = 0; i<policySize; i++)
      policy[i] = isLegal[i] ? (policy[i] / policySum) : -1.0f;
  }

  //Fill everything out-of-bounds too, for robustness.
  for(int i = policySize; i<NNPos::MAX_NN_POLICY_SIZE; i++)
    policy[i] = -1.0f;

  //Fix up the value as well. Note that the neural net gives us back the value from the perspective
  //of the player so we need to negate that to make it the white value.
  //For model version 2 and less, we only have single value output that returns tanh, stuffed
  //ad-hocly into the whiteWinProb field.

  if(modelVersion <= 2) {
    double winProb = 0.5 * tanh(buf.result->whiteWinProb) + 0.5;
    if(nextPlayer == P_WHITE) {
      buf.result->whiteWinProb = winProb;
      buf.result->whiteLossProb = 1.0 - winProb;
      buf.result->whiteNoResultProb = 0.0;
      buf.result->whiteScoreValue = 0.0;
    }
    else {
      buf.result->whiteWinProb = 1.0 - winProb;
      buf.result->whiteLossProb = winProb;
      buf.result->whiteNoResultProb = 0.0;
      buf.result->whiteScoreValue = 0.0;
    }
  }
  else {
    throw StringError("NNEval value postprocessing not implemented for model version");
  }

  //And record the nnHash in the result and put it into the table
  buf.result->nnHash = nnHash;
  if(nnCacheTable != NULL)
    nnCacheTable->set(buf.result);

}



NNCacheTable::Entry::Entry()
  :ptr(nullptr),spinLock(ATOMIC_FLAG_INIT)
{}
NNCacheTable::Entry::~Entry()
{}

NNCacheTable::NNCacheTable(int sizePowerOfTwo) {
  if(sizePowerOfTwo < 0 || sizePowerOfTwo > 63)
    throw StringError("NNCacheTable: Invalid sizePowerOfTwo: " + Global::intToString(sizePowerOfTwo));
  tableSize = ((uint64_t)1) << sizePowerOfTwo;
  tableMask = tableSize-1;
  entries = new Entry[tableSize];
}
NNCacheTable::~NNCacheTable() {
  delete[] entries;
}

bool NNCacheTable::get(Hash128 nnHash, shared_ptr<NNOutput>& ret) {
  uint64_t idx = nnHash.hash0 & tableMask;
  Entry& entry = entries[idx];
  while(entry.spinLock.test_and_set(std::memory_order_acquire));
  bool found = false;
  if(entry.ptr != nullptr && entry.ptr->nnHash == nnHash) {
    ret = entry.ptr;
    found = true;
  }
  entry.spinLock.clear(std::memory_order_release);
  return found;
}

void NNCacheTable::set(const shared_ptr<NNOutput>& p) {
  uint64_t idx = p->nnHash.hash0 & tableMask;
  Entry& entry = entries[idx];
  while(entry.spinLock.test_and_set(std::memory_order_acquire));
  entry.ptr = p;
  entry.spinLock.clear(std::memory_order_release);
}

void NNCacheTable::clear() {
  for(size_t idx = 0; idx<tableSize; idx++) {
    Entry& entry = entries[idx];
    while(entry.spinLock.test_and_set(std::memory_order_acquire));
    entry.ptr = nullptr;
    entry.spinLock.clear(std::memory_order_release);
  }
}

