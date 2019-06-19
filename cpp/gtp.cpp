#include "core/global.h"
#include "core/config_parser.h"
#include "core/timer.h"
#include "search/asyncbot.h"
#include "program/setup.h"
#include "program/play.h"
#include "main.h"

using namespace std;

#define TCLAP_NAMESTARTSTRING "-" //Use single dashes for all flags
#include <tclap/CmdLine.h>

static const vector<string> knownCommands = {
  //Basic GTP commands
  "protocol_version",
  "name",
  "version",
  "known_command",
  "list_commands",
  "quit",

  //GTP extension - specify "boardsize X:Y" or "boardsize X Y" for non-square sizes
  //rectangular_boardsize is an alias for boardsize, intended to make it more evident that we have such support
  "boardsize",
  "rectangular_boardsize",

  "clear_board",
  "komi",
  "play",
  "undo",

  "genmove",
  "genmove-debug", //Prints additional info to stderr
  "search-debug", //Prints additional info to stderr, doesn't actually make the move

  //Clears neural net cached evaluations and bot search tree, allows fresh randomization
  "clear-cache",

  "showboard",
  "place_free_handicap",
  "set_free_handicap",
  "time_settings",
  "time_left",
  "final_score",
  "final_status_list",

  //GTP extensions for board analysis
  "lz-analyze",
  "kata-analyze",

  //Stop any ongoing ponder or analyze
  "stop",
};


static bool tryParsePlayer(const string& s, Player& pla) {
  string str = Global::toLower(s);
  if(str == "black" || str == "b") {
    pla = P_BLACK;
    return true;
  }
  else if(str == "white" || str == "w") {
    pla = P_WHITE;
    return true;
  }
  return false;
}

static bool tryParseLoc(const string& s, const Board& b, Loc& loc) {
  return Location::tryOfString(s,b,loc);
}

static int numHandicapStones(const BoardHistory& hist) {
  const Board board = hist.initialBoard;
  int startBoardNumBlackStones = 0;
  int startBoardNumWhiteStones = 0;
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      if(board.colors[loc] == C_BLACK)
        startBoardNumBlackStones += 1;
      else if(board.colors[loc] == C_WHITE)
        startBoardNumWhiteStones += 1;
    }
  }
  //If we set up in a nontrivial position, then consider it a non-handicap game.
  if(startBoardNumWhiteStones == 0)
    return startBoardNumBlackStones;
  return 0;
}

static bool shouldResign(
  const AsyncBot* bot,
  Player pla,
  const vector<double>& recentWinLossValues,
  double expectedScore,
  const double resignThreshold,
  const int resignConsecTurns
) {
  const BoardHistory hist = bot->getRootHist();
  const Board initialBoard = hist.initialBoard;

  //Assume an advantage of 15 * number of black stones beyond the one black normally gets on the first move and komi
  int extraBlackStones = numHandicapStones(hist);
  if(hist.initialPla == P_WHITE && extraBlackStones > 0)
    extraBlackStones -= 1;
  double handicapBlackAdvantage = 15.0 * extraBlackStones + (7.5 - hist.rules.komi);

  int minTurnForResignation = 0;
  double noResignationWhenWhiteScoreAbove = initialBoard.x_size * initialBoard.y_size;
  if(handicapBlackAdvantage > 0.9 && pla == P_WHITE) {
    //Play at least some moves no matter what
    minTurnForResignation = 1 + initialBoard.x_size * initialBoard.y_size / 5;

    //In a handicap game, also only resign if the expected score difference is well behind schedule assuming
    //that we're supposed to catch up over many moves.
    double numTurnsToCatchUp = 0.60 * initialBoard.x_size * initialBoard.y_size - minTurnForResignation;
    double numTurnsSpent = (double)(hist.moveHistory.size()) - minTurnForResignation;
    if(numTurnsToCatchUp <= 1.0)
      numTurnsToCatchUp = 1.0;
    if(numTurnsSpent <= 0.0)
      numTurnsSpent = 0.0;
    if(numTurnsSpent > numTurnsToCatchUp)
      numTurnsSpent = numTurnsToCatchUp;

    double resignScore = -handicapBlackAdvantage * ((numTurnsToCatchUp - numTurnsSpent) / numTurnsToCatchUp);
    resignScore -= 5.0; //Always require at least a 5 point buffer
    resignScore -= handicapBlackAdvantage * 0.15; //And also require a 15% of the initial handicap

    noResignationWhenWhiteScoreAbove = resignScore;
  }

  if(hist.moveHistory.size() < minTurnForResignation)
    return false;
  if(pla == P_WHITE && expectedScore > noResignationWhenWhiteScoreAbove)
    return false;
  if(resignConsecTurns > recentWinLossValues.size())
    return false;

  for(int i = 0; i<resignConsecTurns; i++) {
    double winLossValue = recentWinLossValues[recentWinLossValues.size()-1-i];
    Player resignPlayerThisTurn = C_EMPTY;
    if(winLossValue < resignThreshold)
      resignPlayerThisTurn = P_WHITE;
    else if(winLossValue > -resignThreshold)
      resignPlayerThisTurn = P_BLACK;

    if(resignPlayerThisTurn != pla)
      return false;
  }

  return true;
}

static void printGenmoveLog(ostream& out, const AsyncBot* bot, const NNEvaluator* nnEval, Loc moveLoc, double timeTaken, Player perspective) {
  const Search* search = bot->getSearch();
  Board::printBoard(out, bot->getRootBoard(), moveLoc, &(bot->getRootHist().moveHistory));
  out << bot->getRootHist().rules << "\n";
  out << "Time taken: " << timeTaken << "\n";
  out << "Root visits: " << search->numRootVisits() << "\n";
  out << "NN rows: " << nnEval->numRowsProcessed() << endl;
  out << "NN batches: " << nnEval->numBatchesProcessed() << endl;
  out << "NN avg batch size: " << nnEval->averageProcessedBatchSize() << endl;
  out << "PV: ";
  search->printPV(out, search->rootNode, 25);
  out << "\n";
  out << "Tree:\n";
  search->printTree(out, search->rootNode, PrintTreeOptions().maxDepth(1).maxChildrenToShow(10),perspective);
}

struct GTPEngine {
  GTPEngine(const GTPEngine&) = delete;
  GTPEngine& operator=(const GTPEngine&) = delete;

  const string nnModelFile;
  const double whiteBonusPerHandicapStone;

  NNEvaluator* nnEval;
  AsyncBot* bot;

  Rules baseRules; //Not including komi, which is always overridden with unhackedKomi + hacks
  SearchParams params;
  float unhackedKomi;
  TimeControls bTimeControls;
  TimeControls wTimeControls;

  //This move history doesn't get cleared upon consecutive moves by the same side, and is used
  //for undo, whereas the one in search does.
  Board initialBoard;
  Player initialPla;
  vector<Move> moveHistory;

  vector<double> recentWinLossValues;
  double lastSearchFactor;

  Player perspective;

  GTPEngine(const string& modelFile, SearchParams initialParams, Rules initialRules, double wBonusPerHandicapStone, Player persp)
    :nnModelFile(modelFile),
     whiteBonusPerHandicapStone(wBonusPerHandicapStone),
     nnEval(NULL),
     bot(NULL),
     baseRules(initialRules),
     params(initialParams),
     unhackedKomi(initialRules.komi),
     bTimeControls(),
     wTimeControls(),
     initialBoard(),
     initialPla(P_BLACK),
     moveHistory(),
     recentWinLossValues(),
     lastSearchFactor(1.0),
     perspective(persp)
  {
  }

  ~GTPEngine() {
    stopAndWait();
    delete bot;
    delete nnEval;
  }

  void stopAndWait() {
    bot->stopAndWait();
  }

  void setOrResetBoardSize(ConfigParser& cfg, Logger& logger, Rand& seedRand, int boardXSize, int boardYSize) {
    if(nnEval != NULL && boardXSize == nnEval->getNNXLen() && boardYSize == nnEval->getNNYLen())
      return;
    if(nnEval != NULL) {
      assert(bot != NULL);
      bot->stopAndWait();
      delete bot;
      delete nnEval;
      bot = NULL;
      nnEval = NULL;
      logger.write("Cleaned up old neural net and bot");
    }

    int maxConcurrentEvals = params.numThreads * 2 + 16; // * 2 + 16 just to give plenty of headroom
    vector<NNEvaluator*> nnEvals = Setup::initializeNNEvaluators(
      {nnModelFile},{nnModelFile},cfg,logger,seedRand,maxConcurrentEvals,false,false,boardXSize,boardYSize,-1
    );
    assert(nnEvals.size() == 1);
    nnEval = nnEvals[0];
    logger.write("Loaded neural net with nnXLen " + Global::intToString(nnEval->getNNXLen()) + " nnYLen " + Global::intToString(nnEval->getNNYLen()));

    string searchRandSeed;
    if(cfg.contains("searchRandSeed"))
      searchRandSeed = cfg.getString("searchRandSeed");
    else
      searchRandSeed = Global::uint64ToString(seedRand.nextUInt64());

    bot = new AsyncBot(params, nnEval, &logger, searchRandSeed);

    Board board(boardXSize,boardYSize);
    Player pla = P_BLACK;
    BoardHistory hist(board,pla,baseRules,0);
    vector<Move> newMoveHistory;
    setPosition(pla,board,hist,board,pla,newMoveHistory);
  }

  void setPosition(Player pla, const Board& board, const BoardHistory& hist, const Board& newInitialBoard, Player newInitialPla, const vector<Move> newMoveHistory) {
    bot->setPosition(pla,board,hist);
    updateKomiIfNew(unhackedKomi);
    recentWinLossValues.clear();
    initialBoard = newInitialBoard;
    initialPla = newInitialPla;
    moveHistory = newMoveHistory;
  }

  void clearBoard() {
    int newXSize = bot->getRootBoard().x_size;
    int newYSize = bot->getRootBoard().y_size;
    Board board(newXSize,newYSize);
    Player pla = P_BLACK;
    BoardHistory hist(board,pla,bot->getRootHist().rules,0);
    vector<Move> newMoveHistory;
    setPosition(pla,board,hist,board,pla,newMoveHistory);
  }

  void updateKomiIfNew(double newUnhackedKomi) {
    //Komi without whiteBonusPerHandicapStone hack
    unhackedKomi = newUnhackedKomi;

    float newKomi = unhackedKomi;
    newKomi += numHandicapStones(bot->getRootHist()) * whiteBonusPerHandicapStone;
    if(newKomi != bot->getRootHist().rules.komi)
      recentWinLossValues.clear();
    bot->setKomiIfNew(newKomi);
  }

  bool play(Loc loc, Player pla) {
    bool suc = bot->makeMove(loc,pla);
    if(suc)
      moveHistory.push_back(Move(loc,pla));
    return suc;
  }

  bool undo() {
    if(moveHistory.size() <= 0)
      return false;

    vector<Move> moveHistoryCopy = moveHistory;

    Board undoneBoard = initialBoard;
    BoardHistory undoneHist(undoneBoard,initialPla,bot->getRootHist().rules,0);
    vector<Move> emptyMoveHistory;
    setPosition(initialPla,undoneBoard,undoneHist,initialBoard,initialPla,emptyMoveHistory);

    for(int i = 0; i<moveHistoryCopy.size()-1; i++) {
      Loc moveLoc = moveHistoryCopy[i].loc;
      Loc movePla = moveHistoryCopy[i].pla;
      bool suc = play(moveLoc,movePla);
      assert(suc);
      (void)suc; //Avoid warning when asserts are off
    }
    return true;
  }

  void ponder() {
    bot->ponder(lastSearchFactor);
  }

  void genMove(
    Player pla,
    Logger& logger, double searchFactorWhenWinningThreshold, double searchFactorWhenWinning,
    bool cleanupBeforePass, bool ogsChatToStderr,
    bool allowResignation, double resignThreshold, int resignConsecTurns,
    bool logSearchInfo, bool debug, bool playChosenMove,
    string& response, bool& responseIsError, bool& maybeStartPondering
  ) {
    response = "";
    responseIsError = false;
    maybeStartPondering = false;

    ClockTimer timer;
    nnEval->clearStats();
    TimeControls tc = pla == P_BLACK ? bTimeControls : wTimeControls;

    //Play faster when winning
    double searchFactor = Play::getSearchFactor(searchFactorWhenWinningThreshold,searchFactorWhenWinning,params,recentWinLossValues,pla);
    lastSearchFactor = searchFactor;

    Loc moveLoc = bot->genMoveSynchronous(pla,tc,searchFactor);
    bool isLegal = bot->isLegal(moveLoc,pla);
    if(moveLoc == Board::NULL_LOC || !isLegal) {
      responseIsError = true;
      response = "genmove returned null location or illegal move";
      ostringstream sout;
      sout << "genmove null location or illegal move!?!" << "\n";
      sout << bot->getRootBoard() << "\n";
      sout << "Pla: " << playerToString(pla) << "\n";
      sout << "MoveLoc: " << Location::toString(moveLoc,bot->getRootBoard()) << "\n";
      logger.write(sout.str());
      return;
    }

    //Implement cleanupBeforePass hack - the bot wants to pass, so instead cleanup if there is something to clean
    if(cleanupBeforePass && moveLoc == Board::PASS_LOC) {
      Board board = bot->getRootBoard();
      BoardHistory hist = bot->getRootHist();
      Color* safeArea = bot->getSearch()->rootSafeArea;
      assert(safeArea != NULL);
      //Scan the board for any spot that is adjacent to an opponent group that is part of our pass-alive territory.
      for(int y = 0; y<board.y_size; y++) {
        for(int x = 0; x<board.x_size; x++) {
          Loc otherLoc = Location::getLoc(x,y,board.x_size);
          if(moveLoc == Board::PASS_LOC &&
             board.colors[otherLoc] == C_EMPTY &&
             safeArea[otherLoc] == pla &&
             board.isAdjacentToPla(otherLoc,getOpp(pla)) &&
             hist.isLegal(board,otherLoc,pla)
          ) {
            moveLoc = otherLoc;
          }
        }
      }
    }

    ReportedSearchValues values;
    double winLossValue;
    double expectedScore;
    {
      values = bot->getSearch()->getRootValuesAssertSuccess();
      winLossValue = values.winLossValue;
      expectedScore = values.expectedScore;
    }

    double timeTaken = timer.getSeconds();

    //-------------------------------

    //Chat
    if(ogsChatToStderr) {
      int64_t visits = bot->getSearch()->getRootVisits();
      double winrate = 0.5 * (1.0 + (values.winValue - values.lossValue));
      //Print winrate from desired perspective
      if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
        winrate = 1.0 - winrate;
        expectedScore = -expectedScore;
      }
      cerr << "CHAT:"
           << "Visits " << visits
           << " Winrate " << Global::strprintf("%.2f%%", winrate * 100.0)
           << " ScoreMean " << Global::strprintf("%.1f", expectedScore)
           << " ScoreStdev " << Global::strprintf("%.1f", values.expectedScoreStdev);
      cerr << " PV ";
      bot->getSearch()->printPVForMove(cerr,bot->getSearch()->rootNode, moveLoc, 6);
      cerr << endl;
    }

    recentWinLossValues.push_back(winLossValue);

    bool resigned = allowResignation && shouldResign(bot,pla,recentWinLossValues,expectedScore,resignThreshold,resignConsecTurns);

    if(resigned)
      response = "resign";
    else
      response = Location::toString(moveLoc,bot->getRootBoard());

    if(logSearchInfo) {
      ostringstream sout;
      printGenmoveLog(sout,bot,nnEval,moveLoc,timeTaken,perspective);
      logger.write(sout.str());
    }
    if(debug) {
      printGenmoveLog(cerr,bot,nnEval,moveLoc,timeTaken,perspective);
    }

    if(!resigned && moveLoc != Board::NULL_LOC && isLegal && playChosenMove) {
      bool suc = bot->makeMove(moveLoc,pla);
      if(suc)
        moveHistory.push_back(Move(moveLoc,pla));
      assert(suc);
      (void)suc; //Avoid warning when asserts are off
      maybeStartPondering = true;
    }
    return;
  }

  void clearCache() {
    bot->clearSearch();
    nnEval->clearCache();
  }

  void placeFreeHandicap(int n, Logger& logger, string& response, bool& responseIsError) {
    //If asked to place more, we just go ahead and only place up to 30, or a quarter of the board
    int xSize = bot->getRootBoard().x_size;
    int ySize = bot->getRootBoard().y_size;
    int maxHandicap = xSize*ySize / 4;
    if(maxHandicap > 30)
      maxHandicap = 30;
    if(n > maxHandicap)
      n = maxHandicap;

    Board board(xSize,ySize);
    Player pla = P_BLACK;
    BoardHistory hist(board,pla,bot->getRootHist().rules,0);
    double extraBlackTemperature = 0.25;
    bool adjustKomi = false;
    int numVisitsForKomi = 0;
    Rand rand;
    ExtraBlackAndKomi extraBlackAndKomi(n,hist.rules.komi,hist.rules.komi);
    Play::playExtraBlack(bot->getSearch(), logger, extraBlackAndKomi, board, hist, extraBlackTemperature, rand, adjustKomi, numVisitsForKomi);

    //Also switch the initial player, expecting white should be next.
    {
      Rules rules = hist.rules;
      hist.clear(board,P_WHITE,rules,0);
      pla = P_WHITE;
    }

    response = "";
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        if(board.colors[loc] != C_EMPTY) {
          response += " " + Location::toString(loc,board);
        }
      }
    }
    response = Global::trim(response);
    (void)responseIsError;

    vector<Move> newMoveHistory;
    setPosition(pla,board,hist,board,pla,newMoveHistory);
  }


  void analyze(Player pla, bool kata, double secondsPerReport, int minMoves, bool showOwnership) {

    static const int analysisPVLen = 9;
    std::function<void(Search* search)> callback;

    //lz-analyze
    if(!kata) {
      callback = [minMoves,pla,this](Search* search) {
        vector<AnalysisData> buf;
        search->getAnalysisData(buf,minMoves,false,analysisPVLen);
        if(buf.size() <= 0)
          return;

        const Board board = search->getRootBoard();
        for(int i = 0; i<buf.size(); i++) {
          if(i > 0)
            cout << " ";
          const AnalysisData& data = buf[i];
          double winrate = 0.5 * (1.0 + data.winLossValue);
          if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
            winrate = 1.0-winrate;
          }
          cout << "info";
          cout << " move " << Location::toString(data.move,board);
          cout << " visits " << data.numVisits;
          cout << " winrate " << round(winrate * 10000.0);
          cout << " prior " << round(data.policyPrior * 10000.0);
          cout << " order " << data.order;
          cout << " pv";
          for(int j = 0; j<data.pv.size(); j++)
            cout << " " << Location::toString(data.pv[j],board);
        }
        cout << endl;
      };
    }
    //kata-analyze
    else {
      callback = [minMoves,pla,showOwnership,this](Search* search) {
        vector<AnalysisData> buf;
        search->getAnalysisData(buf,minMoves,false,analysisPVLen);
        if(buf.size() <= 0)
          return;

        vector<double> ownership;
        if(showOwnership) {
          static constexpr int ownershipMinVisits = 3;
          ownership = search->getAverageTreeOwnership(ownershipMinVisits);
        }

        const Board board = search->getRootBoard();
        for(int i = 0; i<buf.size(); i++) {
          if(i > 0)
            cout << " ";
          const AnalysisData& data = buf[i];
          double winrate = 0.5 * (1.0 + data.winLossValue);
          double scoreMean = data.scoreMean;
          //Analysis displays winrate from bot's perspective
          if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
            winrate = 1.0-winrate;
            scoreMean = -scoreMean;
          }
          cout << "info";
          cout << " move " << Location::toString(data.move,board);
          cout << " visits " << data.numVisits;
          cout << " utility " << data.utility;
          cout << " winrate " << winrate;
          cout << " scoreMean " << scoreMean;
          cout << " scoreStdev " << data.scoreStdev;
          cout << " prior " << data.policyPrior;
          cout << " order " << data.order;
          cout << " pv";
          for(int j = 0; j<data.pv.size(); j++)
            cout << " " << Location::toString(data.pv[j],board);
        }

        if(showOwnership) {
          cout << " ";

          cout << "ownership";
          int nnXLen = search->nnXLen;
          for(int y = 0; y<board.y_size; y++) {
            for(int x = 0; x<board.x_size; x++) {
              int pos = NNPos::xyToPos(x,y,nnXLen);
              if(pla == P_BLACK)
                cout << " " << -ownership[pos];
              else
                cout << " " << ownership[pos];
            }
          }
        }

        cout << endl;
      };
    }

    if(showOwnership)
      bot->setAlwaysIncludeOwnerMap(true);
    else
      bot->setAlwaysIncludeOwnerMap(false);

    double searchFactor = 1e40; //go basically forever
    bot->analyze(pla, searchFactor, secondsPerReport, callback);
  }

};


int MainCmds::gtp(int argc, const char* const* argv) {
  Board::initHash();
  ScoreValue::initTables();
  Rand seedRand;

  string configFile;
  string nnModelFile;
  string overrideVersion;
  try {
    TCLAP::CmdLine cmd("Run GTP engine", ' ', Version::getKataGoVersionForHelp(),true);
    TCLAP::ValueArg<string> configFileArg("","config","Config file to use (see configs/gtp_example.cfg)",true,string(),"FILE");
    TCLAP::ValueArg<string> nnModelFileArg("","model","Neural net model file",true,string(),"FILE");
    TCLAP::ValueArg<string> overrideVersionArg("","override-version","Force KataGo to say a certain value in response to gtp version command",false,string(),"VERSION");
    cmd.add(configFileArg);
    cmd.add(nnModelFileArg);
    cmd.add(overrideVersionArg);
    cmd.parse(argc,argv);
    configFile = configFileArg.getValue();
    nnModelFile = nnModelFileArg.getValue();
    overrideVersion = overrideVersionArg.getValue();
  }
  catch (TCLAP::ArgException &e) {
    cerr << "Error: " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  ConfigParser cfg(configFile);

  Logger logger;
  logger.addFile(cfg.getString("logFile"));
  bool logAllGTPCommunication = cfg.getBool("logAllGTPCommunication");
  bool logSearchInfo = cfg.getBool("logSearchInfo");
  bool loggingToStderr = false;

  if(cfg.contains("logToStderr") && cfg.getBool("logToStderr")) {
    loggingToStderr = true;
    logger.setLogToStderr(true);
  }

  logger.write("GTP Engine starting...");

  Rules initialRules;
  {
    string koRule = cfg.getString("koRule", Rules::koRuleStrings());
    string scoringRule = cfg.getString("scoringRule", Rules::scoringRuleStrings());
    bool multiStoneSuicideLegal = cfg.getBool("multiStoneSuicideLegal");
    float komi = 7.5f; //Default komi, gtp will generally override this

    initialRules.koRule = Rules::parseKoRule(koRule);
    initialRules.scoringRule = Rules::parseScoringRule(scoringRule);
    initialRules.multiStoneSuicideLegal = multiStoneSuicideLegal;
    initialRules.komi = komi;
  }

  SearchParams params;
  {
    vector<SearchParams> paramss = Setup::loadParams(cfg);
    if(paramss.size() != 1)
      throw StringError("Can only specify examply one search bot in gtp mode");
    params = paramss[0];
  }

  const bool ponderingEnabled = cfg.getBool("ponderingEnabled");
  const bool cleanupBeforePass = cfg.contains("cleanupBeforePass") ? cfg.getBool("cleanupBeforePass") : false;
  const bool allowResignation = cfg.contains("allowResignation") ? cfg.getBool("allowResignation") : false;
  const double resignThreshold = cfg.contains("allowResignation") ? cfg.getDouble("resignThreshold",-1.0,0.0) : -1.0; //Threshold on [-1,1], regardless of winLossUtilityFactor
  const int resignConsecTurns = cfg.contains("resignConsecTurns") ? cfg.getInt("resignConsecTurns",1,100) : 3;
  const int whiteBonusPerHandicapStone = cfg.contains("whiteBonusPerHandicapStone") ? cfg.getInt("whiteBonusPerHandicapStone",0,1) : 0;

  Setup::initializeSession(cfg);

  const double searchFactorWhenWinning = cfg.contains("searchFactorWhenWinning") ? cfg.getDouble("searchFactorWhenWinning",0.01,1.0) : 1.0;
  const double searchFactorWhenWinningThreshold = cfg.contains("searchFactorWhenWinningThreshold") ? cfg.getDouble("searchFactorWhenWinningThreshold",0.0,1.0) : 1.0;
  const bool ogsChatToStderr = cfg.contains("ogsChatToStderr") ? cfg.getBool("ogsChatToStderr") : false;

  bool startupPrintMessageToStderr = true;
  if(cfg.contains("startupPrintMessageToStderr"))
    startupPrintMessageToStderr = cfg.getBool("startupPrintMessageToStderr");

  Player perspective = Setup::parseReportAnalysisWinrates(cfg,C_EMPTY);

  GTPEngine* engine = new GTPEngine(nnModelFile,params,initialRules,whiteBonusPerHandicapStone,perspective);
  engine->setOrResetBoardSize(cfg,logger,seedRand,19,19);

  //Check for unused config keys
  cfg.warnUnusedKeys(cerr,&logger);

  logger.write(Version::getKataGoVersionForHelp());
  logger.write("Loaded model "+ nnModelFile);
  logger.write("GTP ready, beginning main protocol loop");
  //Also check loggingToStderr so that we don't duplicate the message from the log file
  if(startupPrintMessageToStderr && !loggingToStderr) {
    cerr << Version::getKataGoVersionForHelp() << endl;
    cerr << "Loaded model " << nnModelFile << endl;
    cerr << "GTP ready, beginning main protocol loop" << endl;
  }

  bool currentlyAnalyzing = false;
  string line;
  while(cin) {
    getline(cin,line);

    //Parse command, extracting out the command itself, the arguments, and any GTP id number for the command.
    string command;
    vector<string> pieces;
    bool hasId = false;
    int id = 0;
    {
      //Filter down to only "normal" ascii characters. Also excludes carrage returns.
      //Newlines are already handled by getline
      size_t newLen = 0;
      for(size_t i = 0; i < line.length(); i++)
        if(((int)line[i] >= 32 && (int)line[i] <= 126) || line[i] == '\t')
          line[newLen++] = line[i];

      line.erase(line.begin()+newLen, line.end());

      //Remove comments
      size_t commentPos = line.find("#");
      if(commentPos != string::npos)
        line = line.substr(0, commentPos);

      //Convert tabs to spaces
      for(size_t i = 0; i < line.length(); i++)
        if(line[i] == '\t')
          line[i] = ' ';

      line = Global::trim(line);
      if(line.length() == 0)
        continue;

      assert(line.length() > 0);

      if(logAllGTPCommunication)
        logger.write("Controller: " + line);

      //Parse id number of command, if present
      size_t digitPrefixLen = 0;
      while(digitPrefixLen < line.length() && Global::isDigit(line[digitPrefixLen]))
        digitPrefixLen++;
      if(digitPrefixLen > 0) {
        hasId = true;
        try {
          id = Global::parseDigits(line,0,digitPrefixLen);
        }
        catch(const IOError& e) {
          cout << "? GTP id '" << id << "' could not be parsed: " << e.what() << endl;
          continue;
        }
        line = line.substr(digitPrefixLen);
      }

      line = Global::trim(line);
      if(line.length() <= 0) {
        cout << "? empty command" << endl;
        continue;
      }

      pieces = Global::split(line,' ');
      for(size_t i = 0; i<pieces.size(); i++)
        pieces[i] = Global::trim(pieces[i]);
      assert(pieces.size() > 0);

      command = pieces[0];
      pieces.erase(pieces.begin());
    }

    //Upon any command, stop any analysis and output a newline
    if(currentlyAnalyzing) {
      engine->stopAndWait();
      cout << endl;
    }

    bool responseIsError = false;
    bool shouldQuitAfterResponse = false;
    bool maybeStartPondering = false;
    string response;

    if(command == "protocol_version") {
      response = "2";
    }

    else if(command == "name") {
      response = "KataGo";
    }

    else if(command == "version") {
      if(overrideVersion.size() > 0)
        response = overrideVersion;
      else
        response = Version::getKataGoVersion();
    }

    else if(command == "known_command") {
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected single argument for known_command but got '" + Global::concat(pieces," ") + "'";
      }
      else {
        if(std::find(knownCommands.begin(), knownCommands.end(), pieces[0]) != knownCommands.end())
          response = "true";
        else
          response = "false";
      }
    }

    else if(command == "list_commands") {
      for(size_t i = 0; i<knownCommands.size(); i++)
        response += knownCommands[i] + "\n";
    }

    else if(command == "quit") {
      shouldQuitAfterResponse = true;
      logger.write("Quit requested by controller");
    }

    else if(command == "boardsize" || command == "rectangular_boardsize") {
      int newXSize = 0;
      int newYSize = 0;
      bool suc = false;

      if(pieces.size() == 1) {
        if(contains(pieces[0],':')) {
          vector<string> subpieces = Global::split(pieces[0],':');
          if(subpieces.size() == 2 && Global::tryStringToInt(subpieces[0], newXSize) && Global::tryStringToInt(subpieces[1], newYSize))
            suc = true;
        }
        else {
          if(Global::tryStringToInt(pieces[0], newXSize)) {
            suc = true;
            newYSize = newXSize;
          }
        }
      }
      else if(pieces.size() == 2) {
        if(Global::tryStringToInt(pieces[0], newXSize) && Global::tryStringToInt(pieces[1], newYSize))
          suc = true;
      }

      if(!suc) {
        responseIsError = true;
        response = "Expected int argument for boardsize or pair of ints but got '" + Global::concat(pieces," ") + "'";
      }
      else if(newXSize < 2 || newYSize < 2) {
        responseIsError = true;
        response = "unacceptable size";
      }
      else if(newXSize > Board::MAX_LEN || newYSize > Board::MAX_LEN) {
        responseIsError = true;
        response = Global::strprintf("unacceptable size (Board::MAX_LEN is %d, consider increasing and recompiling)",(int)Board::MAX_LEN);
      }
      else {
        engine->setOrResetBoardSize(cfg,logger,seedRand,newXSize,newYSize);
      }
    }

    else if(command == "clear_board") {
      engine->clearBoard();
    }

    else if(command == "komi") {
      float newKomi = 0;
      if(pieces.size() != 1 || !Global::tryStringToFloat(pieces[0],newKomi)) {
        responseIsError = true;
        response = "Expected single float argument for komi but got '" + Global::concat(pieces," ") + "'";
      }
      //GTP spec says that we should accept any komi, but we're going to ignore that.
      else if(isnan(newKomi) || newKomi < -100.0 || newKomi > 100.0) {
        responseIsError = true;
        response = "unacceptable komi";
      }
      else if(!Rules::komiIsIntOrHalfInt(newKomi)) {
        responseIsError = true;
        response = "komi must be an integer or half-integer";
      }
      else {
        engine->updateKomiIfNew(newKomi);
        //In case the controller tells us komi every move, restart pondering afterward.
        maybeStartPondering = engine->bot->getRootHist().moveHistory.size() > 0;
      }
    }

    else if(command == "time_settings") {
      double mainTime;
      double byoYomiTime;
      int byoYomiStones;
      if(pieces.size() != 3
         || !Global::tryStringToDouble(pieces[0],mainTime)
         || !Global::tryStringToDouble(pieces[1],byoYomiTime)
         || !Global::tryStringToInt(pieces[2],byoYomiStones)
         ) {
        responseIsError = true;
        response = "Expected 2 floats and an int for time_settings but got '" + Global::concat(pieces," ") + "'";
      }
      else if(isnan(mainTime) || mainTime < 0.0 || mainTime > 1e50) {
        responseIsError = true;
        response = "invalid main_time";
      }
      else if(isnan(byoYomiTime) || byoYomiTime < 0.0 || byoYomiTime > 1e50) {
        responseIsError = true;
        response = "invalid byo_yomi_time";
      }
      else if(byoYomiStones < 0 || byoYomiStones > 100000) {
        responseIsError = true;
        response = "invalid byo_yomi_stones";
      }
      else {
        TimeControls tc;
        //This means no time limits, according to gtp spec
        if(byoYomiStones == 0 && byoYomiTime > 0.0) {
          //do nothing, tc already no limits by default
        }
        //Absolute time
        else if(byoYomiStones == 0) {
          tc.originalMainTime = mainTime;
          tc.increment = 0.0;
          tc.originalNumPeriods = 0;
          tc.numStonesPerPeriod = 0;
          tc.perPeriodTime = 0.0;
          tc.mainTimeLeft = mainTime;
          tc.inOvertime = false;
          tc.numPeriodsLeftIncludingCurrent = 0;
          tc.numStonesLeftInPeriod = 0;
          tc.timeLeftInPeriod = 0;
        }
        else {
          tc.originalMainTime = mainTime;
          tc.increment = 0.0;
          tc.originalNumPeriods = 1;
          tc.numStonesPerPeriod = byoYomiStones;
          tc.perPeriodTime = byoYomiTime;
          tc.mainTimeLeft = mainTime;
          tc.inOvertime = false;
          tc.numPeriodsLeftIncludingCurrent = 1;
          tc.numStonesLeftInPeriod = 0;
          tc.timeLeftInPeriod = 0;
        }

        engine->bTimeControls = tc;
        engine->wTimeControls = tc;
      }
    }

    else if(command == "time_left") {
      Player pla;
      double time;
      int stones;
      if(pieces.size() != 3
         || !tryParsePlayer(pieces[0],pla)
         || !Global::tryStringToDouble(pieces[1],time)
         || !Global::tryStringToInt(pieces[2],stones)
         ) {
        responseIsError = true;
        response = "Expected player and float time and int stones for time_left but got '" + Global::concat(pieces," ") + "'";
      }
      //Be slightly tolerant of negative time left
      else if(isnan(time) || time < -10.0 || time > 1e50) {
        responseIsError = true;
        response = "invalid time";
      }
      else if(stones < 0 || stones > 100000) {
        responseIsError = true;
        response = "invalid stones";
      }
      else {
        TimeControls tc = pla == P_BLACK ? engine->bTimeControls : engine->wTimeControls;
        //Main time
        if(stones == 0) {
          tc.mainTimeLeft = time;
          tc.inOvertime = false;
          tc.numPeriodsLeftIncludingCurrent = tc.originalNumPeriods;
          tc.numStonesLeftInPeriod = 0;
          tc.timeLeftInPeriod = 0;
        }
        else {
          tc.mainTimeLeft = 0.9;
          tc.inOvertime = true;
          tc.numPeriodsLeftIncludingCurrent = 1;
          tc.numStonesLeftInPeriod = stones;
          tc.timeLeftInPeriod = time;
        }
        if(pla == P_BLACK)
          engine->bTimeControls = tc;
        else
          engine->wTimeControls = tc;

        //In case the controller tells us komi every move, restart pondering afterward.
        maybeStartPondering = engine->bot->getRootHist().moveHistory.size() > 0;
      }
    }

    else if(command == "play") {
      Player pla;
      Loc loc;
      if(pieces.size() != 2) {
        responseIsError = true;
        response = "Expected two arguments for play but got '" + Global::concat(pieces," ") + "'";
      }
      else if(!tryParsePlayer(pieces[0],pla)) {
        responseIsError = true;
        response = "Could not parse color: '" + pieces[0] + "'";
      }
      else if(!tryParseLoc(pieces[1],engine->bot->getRootBoard(),loc)) {
        responseIsError = true;
        response = "Could not parse vertex: '" + pieces[1] + "'";
      }
      else {
        bool suc = engine->play(loc,pla);
        if(!suc) {
          responseIsError = true;
          response = "illegal move";
        }
        maybeStartPondering = true;
      }
    }

    else if(command == "undo") {
      bool suc = engine->undo();
      if(!suc) {
        responseIsError = true;
        response = "cannot undo";
      }
    }

    else if(command == "genmove" || command == "genmove-debug" || command == "search-debug") {
      Player pla;
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for genmove but got '" + Global::concat(pieces," ") + "'";
      }
      else if(!tryParsePlayer(pieces[0],pla)) {
        responseIsError = true;
        response = "Could not parse color: '" + pieces[0] + "'";
      }
      else {
        bool debug = command == "genmove-debug" || command == "search-debug";
        bool playChosenMove = command != "search-debug";

        engine->genMove(
          pla,
          logger,searchFactorWhenWinningThreshold,searchFactorWhenWinning,
          cleanupBeforePass,ogsChatToStderr,
          allowResignation,resignThreshold,resignConsecTurns,
          logSearchInfo,debug,playChosenMove,
          response,responseIsError,maybeStartPondering
        );
      }
    }

    else if(command == "clear-cache") {
      engine->clearCache();
    }
    else if(command == "showboard") {
      ostringstream sout;
      Board::printBoard(sout, engine->bot->getRootBoard(), Board::NULL_LOC, &(engine->bot->getRootHist().moveHistory));
      response = Global::trim(sout.str());
    }

    else if(command == "place_free_handicap") {
      int n;
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for place_free_handicap but got '" + Global::concat(pieces," ") + "'";
      }
      else if(!Global::tryStringToInt(pieces[0],n)) {
        responseIsError = true;
        response = "Could not parse number of handicap stones: '" + pieces[0] + "'";
      }
      else if(n < 2) {
        responseIsError = true;
        response = "Number of handicap stones less than 2: '" + pieces[0] + "'";
      }
      else if(!engine->bot->getRootBoard().isEmpty()) {
        responseIsError = true;
        response = "Board is not empty";
      }
      else {
        engine->placeFreeHandicap(n,logger,response,responseIsError);
      }
    }

    else if(command == "set_free_handicap") {
      if(!engine->bot->getRootBoard().isEmpty()) {
        responseIsError = true;
        response = "Board is not empty";
      }
      else {
        vector<Loc> locs;
        int xSize = engine->bot->getRootBoard().x_size;
        int ySize = engine->bot->getRootBoard().y_size;
        Board board(xSize,ySize);
        for(int i = 0; i<pieces.size(); i++) {
          Loc loc;
          bool suc = tryParseLoc(pieces[i],board,loc);
          if(!suc || loc == Board::PASS_LOC) {
            responseIsError = true;
            response = "Invalid handicap location: " + pieces[i];
          }
          locs.push_back(loc);
        }
        for(int i = 0; i<locs.size(); i++)
          board.setStone(locs[i],P_BLACK);

        Player pla = P_WHITE;
        BoardHistory hist(board,pla,engine->bot->getRootHist().rules,0);
        vector<Move> newMoveHistory;
        engine->setPosition(pla,board,hist,board,pla,newMoveHistory);
      }
    }

    else if(command == "final_score") {
      //Returns the resulting score if this position were scored AS-IS (players repeatedly passing until the game ends),
      //rather than attempting to estimate what the score would be with further playouts
      Board board = engine->bot->getRootBoard();
      BoardHistory hist = engine->bot->getRootHist();

      //For GTP purposes, we treat noResult as a draw since there is no provision for anything else.
      if(!hist.isGameFinished)
        hist.endAndScoreGameNow(board);

      if(hist.winner == C_EMPTY)
        response = "0";
      else if(hist.winner == C_BLACK)
        response = "B+" + Global::strprintf("%.1f",-hist.finalWhiteMinusBlackScore);
      else if(hist.winner == C_WHITE)
        response = "W+" + Global::strprintf("%.1f",hist.finalWhiteMinusBlackScore);
      else
        ASSERT_UNREACHABLE;
    }

    else if(command == "final_status_list") {
      int statusMode = 0;
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for final_status_list but got '" + Global::concat(pieces," ") + "'";
      }
      else {
        if(pieces[0] == "alive")
          statusMode = 0;
        else if(pieces[0] == "seki")
          statusMode = 1;
        else if(pieces[0] == "dead")
          statusMode = 2;
        else {
          responseIsError = true;
          response = "Argument to final_status_list must be 'alive' or 'seki' or 'dead'";
          statusMode = 3;
        }

        if(statusMode < 3) {
          vector<Loc> locsToReport;
          Board board = engine->bot->getRootBoard();
          BoardHistory hist = engine->bot->getRootHist();

          if(hist.isGameFinished && hist.isNoResult) {
            //Treat all stones as alive under a no result
            if(statusMode == 0) {
              for(int y = 0; y<board.y_size; y++) {
                for(int x = 0; x<board.x_size; x++) {
                  Loc loc = Location::getLoc(x,y,board.x_size);
                  if(board.colors[loc] != C_EMPTY)
                    locsToReport.push_back(loc);
                }
              }
            }
          }
          else {
            Color area[Board::MAX_ARR_SIZE];
            hist.endAndScoreGameNow(board,area);
            for(int y = 0; y<board.y_size; y++) {
              for(int x = 0; x<board.x_size; x++) {
                Loc loc = Location::getLoc(x,y,board.x_size);
                if(board.colors[loc] != C_EMPTY) {
                  if(statusMode == 0 && board.colors[loc] == area[loc])
                    locsToReport.push_back(loc);
                  else if(statusMode == 2 && board.colors[loc] != area[loc])
                    locsToReport.push_back(loc);
                }
              }
            }
          }

          response = "";
          for(int i = 0; i<locsToReport.size(); i++) {
            Loc loc = locsToReport[i];
            if(i > 0)
              response += " ";
            response += Location::toString(loc,board);
          }
        }
      }
    }

    else if(command == "lz-analyze" || command == "kata-analyze") {
      int numArgsParsed = 0;

      Player pla = engine->bot->getRootPla();
      double lzAnalyzeInterval = 1e30;
      int minMoves = 0;
      bool showOwnership = false;
      bool parseFailed = false;

      //Format:
      //lz-analyze [optional player] [optional interval float] <keys and values>
      //Keys and values consists of zero or more of:

      //interval <float interval in centiseconds>
      //avoid <player> <comma-separated moves> <until movenum>
      //minmoves <int min number of moves to show>
      //ownership <bool whether to show ownership or not>

      //Parse optional player
      if(pieces.size() > numArgsParsed && tryParsePlayer(pieces[numArgsParsed],pla))
        numArgsParsed += 1;

      //Parse optional interval float
      if(pieces.size() > numArgsParsed &&
         Global::tryStringToDouble(pieces[numArgsParsed],lzAnalyzeInterval) &&
         !isnan(lzAnalyzeInterval) && lzAnalyzeInterval >= 0 && lzAnalyzeInterval < 1e20)
        numArgsParsed += 1;

      //Now loop and handle all key value pairs
      while(pieces.size() > numArgsParsed) {
        const string& key = pieces[numArgsParsed];
        numArgsParsed += 1;
        //Make sure we have a value. If not, then we fail.
        if(pieces.size() <= numArgsParsed) {
          parseFailed = true;
          break;
        }

        const string& value = pieces[numArgsParsed];
        numArgsParsed += 1;

        if(key == "interval" && Global::tryStringToDouble(value,lzAnalyzeInterval) &&
           !isnan(lzAnalyzeInterval) && lzAnalyzeInterval >= 0 && lzAnalyzeInterval < 1e20) {
          continue;
        }
        //Parse it but ignore it since we don't support excluding moves right now
        else if(key == "avoid" || key == "allow") {
          //Parse two more arguments, and ignore them
          if(pieces.size() <= numArgsParsed+1) {
            parseFailed = true;
            break;
          }
          const string& moves = pieces[numArgsParsed];
          (void)moves;
          numArgsParsed += 1;
          const string& untilMove = pieces[numArgsParsed];
          (void)untilMove;
          numArgsParsed += 1;
          continue;
        }
        else if(key == "minmoves" && Global::tryStringToInt(value,minMoves) &&
                minMoves >= 0 && minMoves < 1000000000) {
          continue;
        }

        else if(command == "kata-analyze" && key == "ownership" && Global::tryStringToBool(value,showOwnership)) {
          continue;
        }

        parseFailed = true;
        break;
      }

      if(parseFailed) {
        responseIsError = true;
        response = "Could not parse analyze arguments or arguments out of range: '" + Global::concat(pieces," ") + "'";
      }
      else {
        double secondsPerReport = lzAnalyzeInterval * 0.01; //Convert from centiseconds to seconds

        bool kata = command == "kata-analyze";
        engine->analyze(pla, kata, secondsPerReport, minMoves, showOwnership);
        currentlyAnalyzing = true;
      }
    }

    else if(command == "stop") {
      //Stop any ongoing ponder or analysis
      engine->stopAndWait();
    }

    else {
      responseIsError = true;
      response = "unknown command";
    }


    //Postprocessing of response
    if(hasId)
      response = Global::intToString(id) + " " + response;
    else
      response = " " + response;

    if(responseIsError)
      response = "?" + response;
    else
      response = "=" + response;

    cout << response << endl;

    //GTP needs extra newline, except if currently analyzing, defer the newline until we actually stop analysis
    if(!currentlyAnalyzing)
      cout << endl;

    if(logAllGTPCommunication)
      logger.write(response);

    if(shouldQuitAfterResponse)
      break;

    if(maybeStartPondering && ponderingEnabled)
      engine->ponder();

  } //Close read loop

  delete engine;
  engine = NULL;
  NeuralNet::globalCleanup();
  ScoreValue::freeTables();

  logger.write("All cleaned up, quitting");
  return 0;
}
