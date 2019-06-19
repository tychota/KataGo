#include "../tests/tests.h"

using namespace std;
using namespace TestCommon;

static void checkKoHashConsistency(BoardHistory& hist, Board& board, Player nextPla) {
  testAssert(hist.koHashHistory.size() > 0);
  Hash128 expected = board.pos_hash;
  if(hist.encorePhase > 0) {
    expected ^= Board::ZOBRIST_PLAYER_HASH[nextPla];
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        if(hist.blackKoProhibited[loc])
          expected ^= Board::ZOBRIST_KO_MARK_HASH[loc][P_BLACK];
        if(hist.whiteKoProhibited[loc])
          expected ^= Board::ZOBRIST_KO_MARK_HASH[loc][P_WHITE];
      }
    }
  }
  else if(hist.rules.koRule == Rules::KO_SITUATIONAL) {
    expected ^= Board::ZOBRIST_PLAYER_HASH[nextPla];
  }
  testAssert(expected == hist.koHashHistory[hist.koHashHistory.size()-1]);
}

static void makeMoveAssertLegal(BoardHistory& hist, Board& board, Loc loc, Player pla, int line) {
  bool phaseWouldEnd = hist.passWouldEndPhase(board,pla);
  int oldPhase = hist.encorePhase;

  if(!hist.isLegal(board, loc, pla))
    throw StringError("Illegal move on line " + Global::intToString(line));
  hist.makeBoardMoveAssumeLegal(board, loc, pla, NULL);
  checkKoHashConsistency(hist,board,getOpp(pla));

  if(loc == Board::PASS_LOC) {
    int newPhase = hist.encorePhase;
    if(phaseWouldEnd != (newPhase != oldPhase || hist.isGameFinished))
      throw StringError("hist.passWouldEndPhase returned different answer than what actually happened after a pass");
  }
}

static double finalScoreIfGameEndedNow(const BoardHistory& baseHist, const Board& baseBoard) {
  Player pla = P_BLACK;
  Board board(baseBoard);
  BoardHistory hist(baseHist);
  if(hist.moveHistory.size() > 0)
    pla = getOpp(hist.moveHistory[hist.moveHistory.size()-1].pla);
  while(!hist.isGameFinished) {
    hist.makeBoardMoveAssumeLegal(board, Board::PASS_LOC, pla, NULL);
    pla = getOpp(pla);
  }

  double score = hist.finalWhiteMinusBlackScore;

  hist.endAndScoreGameNow(board);
  testAssert(hist.finalWhiteMinusBlackScore == score);

  BoardHistory hist2(baseHist);
  hist2.endAndScoreGameNow(baseBoard);
  testAssert(hist2.finalWhiteMinusBlackScore == score);

  return score;
}

void Tests::runRulesTests() {
  cout << "Running rules tests" << endl;
  ostringstream out;

  //Some helpers
  auto printIllegalMoves = [](ostream& o, const Board& board, const BoardHistory& hist, Player pla) {
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        if(board.colors[loc] == C_EMPTY && !board.isIllegalSuicide(loc,pla,hist.rules.multiStoneSuicideLegal) && !hist.isLegal(board,loc,pla)) {
          o << "Illegal: " << Location::toStringMach(loc,board.x_size) << " " << colorToChar(pla) << endl;
        }
        if((pla == P_BLACK && hist.blackKoProhibited[loc]) || (pla == P_WHITE && hist.whiteKoProhibited[loc])) {
          o << "Ko-prohibited: " << Location::toStringMach(loc,board.x_size) << " " << colorToChar(pla) << endl;
        }
      }
    }
  };

  auto printEncoreKoProhibition = [](ostream& o, const Board& board, const BoardHistory& hist) {
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        if(hist.blackKoProhibited[loc])
          o << "Ko prohibited black at " << Location::toString(loc,board) << endl;
        if(hist.whiteKoProhibited[loc])
          o << "Ko prohibited white at " << Location::toString(loc,board) << endl;
      }
    }
  };

  auto printGameResult = [](ostream& o, const BoardHistory& hist) {
    if(!hist.isGameFinished)
      o << "Game is not over";
    else {
      o << "Winner: " << playerToString(hist.winner) << endl;
      o << "W-B Score: " << hist.finalWhiteMinusBlackScore << endl;
      o << "isNoResult: " << hist.isNoResult << endl;
      o << "isResignation: " << hist.isResignation << endl;
    }
  };

  {
    const char* name = "Area rules";
    Board board = Board::parseBoard(4,4,R"%%(
....
....
....
....
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = true;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,1,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,3,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,3,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_WHITE);
    testAssert(hist.finalWhiteMinusBlackScore == 0.5f);
    //Resurrecting the board after game over with another pass
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_WHITE);
    testAssert(hist.finalWhiteMinusBlackScore == 0.5f);
    //And then some real moves followed by more passes
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_WHITE);
    testAssert(hist.finalWhiteMinusBlackScore == 0.5f);
    out << board << endl;
    string expected = R"%%(
HASH: 5FA73DC4EC4D5C8EF52ECF27BFF1754C
   A B C D
 4 . X O .
 3 . X O .
 2 . X O O
 1 . X O .
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Territory rules";
    Board board = Board::parseBoard(4,4,R"%%(
....
....
....
....
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = true;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,1,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,3,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,3,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.encorePhase == 0);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.encorePhase == 1);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.encorePhase == 1);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.encorePhase == 2);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.encorePhase == 2);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.encorePhase == 2);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_WHITE);
    testAssert(hist.finalWhiteMinusBlackScore == 3.5f);
    out << board << endl;

    //Resurrecting the board after pass to have black throw in a dead stone, since second encore, should make no difference
    makeMoveAssertLegal(hist, board, Location::getLoc(3,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.encorePhase == 2);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_WHITE);
    testAssert(hist.finalWhiteMinusBlackScore == 3.5f);
    out << board << endl;

    //Resurrecting again to have black solidfy his group and prove it pass-alive
    makeMoveAssertLegal(hist, board, Location::getLoc(3,0,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    //White claimed 3 points pre-second-encore, while black waited until second encore, so black gets 4 points and wins by 0.5.
    testAssert(hist.encorePhase == 2);
    testAssert(hist.isGameFinished == true);
    testAssert(hist.winner == P_BLACK);
    testAssert(hist.finalWhiteMinusBlackScore == -0.5f);
    out << board << endl;

    string expected = R"%%(
HASH: 5FA73DC4EC4D5C8EF52ECF27BFF1754C
   A B C D
 4 . X O .
 3 . X O .
 2 . X O O
 1 . X O .


HASH: D7D56E29425FCBAE79353E413C56BE86
   A B C D
 4 . X O .
 3 . X O X
 2 . X O O
 1 . X O .


HASH: ED1BFE08358E833305424823D2511E60
   A B C D
 4 . X O O
 3 X X O .
 2 . X O O
 1 . X O .

)%%";
    expect(name,out,expected);
  }


  //Ko rule testing with a regular ko and a sending two returning 1
  {
    Board baseBoard = Board::parseBoard(6,5,R"%%(
.o.xxo
oxxxo.
o.x.oo
xxxoo.
oooo.o
)%%");

    Rules baseRules;
    baseRules.koRule = Rules::KO_POSITIONAL;
    baseRules.scoringRule = Rules::SCORING_TERRITORY;
    baseRules.komi = 0.5f;
    baseRules.multiStoneSuicideLegal = false;

    {
      const char* name = "Simple ko rules";
      Board board(baseBoard);
      Rules rules(baseRules);
      rules.koRule = Rules::KO_SIMPLE;
      BoardHistory hist(board,P_BLACK,rules,0);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
      out << "After black ko capture:" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After black ko capture and one pass:" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(hist.encorePhase == 0);
      testAssert(hist.isGameFinished == false);
      out << "After black ko capture and two passes:" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
      out << "White recapture:" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);

      out << "Beginning sending two returning one cycle" << endl;
      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      printIllegalMoves(out,board,hist,P_BLACK);
      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      printIllegalMoves(out,board,hist,P_WHITE);
      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      printIllegalMoves(out,board,hist,P_BLACK);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      printIllegalMoves(out,board,hist,P_WHITE);
      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      printIllegalMoves(out,board,hist,P_BLACK);
      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      printIllegalMoves(out,board,hist,P_WHITE);
      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      printIllegalMoves(out,board,hist,P_BLACK);
      testAssert(hist.encorePhase == 0);
      testAssert(hist.isGameFinished == false);
      //Spight ending condition cuts this cycle a bit shorter
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      printIllegalMoves(out,board,hist,P_WHITE);
      testAssert(hist.encorePhase == 1);
      testAssert(hist.isGameFinished == false);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(hist.encorePhase == 2);
      printGameResult(out,hist);

      string expected = R"%%(
After black ko capture:
Illegal: (5,0) O
After black ko capture and one pass:
After black ko capture and two passes:
White recapture:
Illegal: (5,1) X
Beginning sending two returning one cycle
Winner: White
W-B Score: 0.5
isNoResult: 0
isResignation: 0
)%%";
      expect(name,out,expected);
    }

    {
      const char* name = "Positional ko rules";
      Board board(baseBoard);
      Rules rules(baseRules);
      rules.koRule = Rules::KO_POSITIONAL;
      BoardHistory hist(board,P_BLACK,rules,0);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
      out << "After black ko capture:" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After black ko capture and one pass:" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      //On tmp board and hist, verify that the main phase ends if black passes now
      Board tmpboard(board);
      BoardHistory tmphist(hist);
      makeMoveAssertLegal(tmphist, tmpboard, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(tmphist.encorePhase == 1);
      testAssert(tmphist.isGameFinished == false);

      makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
      out << "Beginning sending two returning one cycle" << endl;

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends two?" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      out << "Can white recapture?" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
      out << "After white recaptures the other ko instead" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white recaptures the other ko instead and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      out << "After white now returns 1" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white now returns 1 and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends 2 again" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);
      testAssert(hist.encorePhase == 0);
      testAssert(hist.isGameFinished == false);

      string expected = R"%%(
After black ko capture:
Illegal: (5,0) O
After black ko capture and one pass:
Beginning sending two returning one cycle
After white sends two?
Can white recapture?
Illegal: (1,0) O
After white recaptures the other ko instead
Illegal: (5,1) X
After white recaptures the other ko instead and black passes
After white now returns 1
Illegal: (5,1) X
After white now returns 1 and black passes
After white sends 2 again
Illegal: (0,0) X
Illegal: (5,1) X
)%%";
      expect(name,out,expected);
    }

    {
      const char* name = "Situational ko rules";
      Board board(baseBoard);
      Rules rules(baseRules);
      rules.koRule = Rules::KO_SITUATIONAL;
      BoardHistory hist(board,P_BLACK,rules,0);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
      out << "After black ko capture:" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After black ko capture and one pass:" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      //On tmp board and hist, verify that the main phase ends if black passes now
      Board tmpboard(board);
      BoardHistory tmphist(hist);
      makeMoveAssertLegal(tmphist, tmpboard, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(tmphist.encorePhase == 1);
      testAssert(tmphist.isGameFinished == false);

      makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
      out << "Beginning sending two returning one cycle" << endl;

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends two?" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      out << "Can white recapture?" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
      out << "After white recaptures the other ko instead" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white recaptures the other ko instead and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      out << "After white now returns 1" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white now returns 1 and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends 2 again" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);
      testAssert(hist.encorePhase == 0);
      testAssert(hist.isGameFinished == false);

      string expected = R"%%(
After black ko capture:
Illegal: (5,0) O
After black ko capture and one pass:
Beginning sending two returning one cycle
After white sends two?
Can white recapture?
After white recaptures the other ko instead
Illegal: (5,1) X
After white recaptures the other ko instead and black passes
After white now returns 1
Illegal: (5,1) X
After white now returns 1 and black passes
After white sends 2 again
Illegal: (0,0) X
)%%";
      expect(name,out,expected);
    }

    {
      const char* name = "Spight ko rules";
      Board board(baseBoard);
      Rules rules(baseRules);
      rules.koRule = Rules::KO_SPIGHT;
      BoardHistory hist(board,P_BLACK,rules,0);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
      out << "After black ko capture:" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After black ko capture and one pass:" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      //On tmp board and hist, verify that the main phase does not end if black passes now
      Board tmpboard(board);
      BoardHistory tmphist(hist);
      makeMoveAssertLegal(tmphist, tmpboard, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(tmphist.encorePhase == 0);
      testAssert(tmphist.isGameFinished == false);
      out << "If black were to pass as well??" << endl;
      printIllegalMoves(out,tmpboard,tmphist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
      out << "Beginning sending two returning one cycle" << endl;

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends two?" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      out << "Can white recapture?" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
      out << "After white recaptures the other ko instead" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white recaptures the other ko instead and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      out << "After white now returns 1" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After white now returns 1 and black passes" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_WHITE, __LINE__);
      out << "After white sends 2 again" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
      out << "Can white recapture?" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After pass" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);
      testAssert(hist.encorePhase == 0);
      testAssert(hist.isGameFinished == false);

      //This is actually black's second pass in this position!
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After pass" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);
      testAssert(hist.encorePhase == 1);
      testAssert(hist.isGameFinished == false);

      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      testAssert(hist.encorePhase == 2);
      printGameResult(out,hist);

      string expected = R"%%(
After black ko capture:
Illegal: (5,0) O
After black ko capture and one pass:
If black were to pass as well??
Beginning sending two returning one cycle
After white sends two?
Can white recapture?
Illegal: (1,0) O
After white recaptures the other ko instead
Illegal: (5,1) X
After white recaptures the other ko instead and black passes
After white now returns 1
After white now returns 1 and black passes
After white sends 2 again
Can white recapture?
Illegal: (1,0) O
After pass
After pass
Winner: Black
W-B Score: -0.5
isNoResult: 0
isResignation: 0
)%%";
      expect(name,out,expected);
    }
  }

  //Testing superko with suicide
  {
    Board baseBoard = Board::parseBoard(6,5,R"%%(
.oxo.x
oxxooo
xx....
......
......
)%%");

    Rules baseRules;
    baseRules.koRule = Rules::KO_POSITIONAL;
    baseRules.scoringRule = Rules::SCORING_AREA;
    baseRules.komi = 0.5f;
    baseRules.multiStoneSuicideLegal = true;

    int koRulesToTest[3] = { Rules::KO_POSITIONAL, Rules::KO_SITUATIONAL, Rules::KO_SPIGHT };
    const char* name = "Suicide ko testing";
    for(int i = 0; i<3; i++)
    {
      out << "------------------------------" << endl;
      Board board(baseBoard);
      Rules rules(baseRules);
      rules.koRule = koRulesToTest[i];
      BoardHistory hist(board,P_BLACK,rules,0);

      makeMoveAssertLegal(hist, board, Location::getLoc(4,0,board.x_size), P_BLACK, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
      out << "After black suicide and white pass" << endl;
      printIllegalMoves(out,board,hist,P_BLACK);

      makeMoveAssertLegal(hist, board, Location::getLoc(4,0,board.x_size), P_BLACK, __LINE__);
      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_BLACK, __LINE__);
      makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
      out << "After a little looping" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_WHITE, __LINE__);
      makeMoveAssertLegal(hist, board, Location::getLoc(4,0,board.x_size), P_BLACK, __LINE__);
      out << "Filling in a bit more" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);

      //Illegal under non-spight superkos, but still should be handled gracefully
      hist.makeBoardMoveAssumeLegal(board, Location::getLoc(0,1,board.x_size), P_WHITE, NULL);
      hist.makeBoardMoveAssumeLegal(board, Location::getLoc(5,0,board.x_size), P_BLACK, NULL);
      hist.makeBoardMoveAssumeLegal(board, Location::getLoc(1,0,board.x_size), P_WHITE, NULL);
      hist.makeBoardMoveAssumeLegal(board, Location::getLoc(4,0,board.x_size), P_BLACK, NULL);
      out << "Looped some more" << endl;
      printIllegalMoves(out,board,hist,P_WHITE);
      out << board << endl;

    }
    string expected = R"%%(
------------------------------
After black suicide and white pass
Illegal: (5,0) X
After a little looping
Illegal: (0,1) O
Filling in a bit more
Illegal: (0,1) O
Looped some more
Illegal: (0,0) O
Illegal: (0,1) O
HASH: D9EA171850FEC7E00195801AB2AC1575
   A B C D E F
 5 . O X O X .
 4 . X X O O O
 3 X X . . . .
 2 . . . . . .
 1 . . . . . .


------------------------------
After black suicide and white pass
After a little looping
Illegal: (0,1) O
Filling in a bit more
Illegal: (0,1) O
Looped some more
HASH: D9EA171850FEC7E00195801AB2AC1575
   A B C D E F
 5 . O X O X .
 4 . X X O O O
 3 X X . . . .
 2 . . . . . .
 1 . . . . . .


------------------------------
After black suicide and white pass
After a little looping
Filling in a bit more
Looped some more
Illegal: (0,0) O
HASH: D9EA171850FEC7E00195801AB2AC1575
   A B C D E F
 5 . O X O X .
 4 . X X O O O
 3 X X . . . .
 2 . . . . . .
 1 . . . . . .

)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Eternal life";
    Board board = Board::parseBoard(8,5,R"%%(
........
oooooo..
xxxxxo..
xoooxxoo
.o.x.ox.
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Location::getLoc(2,4,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,4,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,4,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,4,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,4,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,4,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,4,board.x_size), P_BLACK, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,4,board.x_size), P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    printGameResult(out,hist);

    string expected = R"%%(
Winner: Empty
W-B Score: 0
isNoResult: 1
isResignation: 0
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Triple ko simple";
    Board board = Board::parseBoard(7,6,R"%%(
ooooooo
oxo.o.o
x.xoxox
xxxxxxx
ooooooo
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Location::getLoc(3,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    testAssert(hist.isGameFinished == false);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,2,board.x_size), P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    printGameResult(out,hist);

    string expected = R"%%(
Winner: Empty
W-B Score: 0
isNoResult: 1
isResignation: 0
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Triple ko superko";
    Board board = Board::parseBoard(7,6,R"%%(
ooooooo
oxo.o.o
x.xoxox
xxxxxxx
ooooooo
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Location::getLoc(3,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    printIllegalMoves(out,board,hist,P_WHITE);
    string expected = R"%%(
Illegal: (1,2) O
Illegal: (5,2) O
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Triple ko encore";
    Board board = Board::parseBoard(7,6,R"%%(
ooooooo
oxo.o.o
x.xoxox
xxxxxxx
ooooooo
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,1,board.x_size), P_BLACK, __LINE__);
    //Pass for ko
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_WHITE, __LINE__);
    //Should be a complete capture
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    //There should be no ko marks on the board at this point.
    printEncoreKoProhibition(out,board,hist);

    string expected = R"%%(
HASH: 2FA527ADE62EF25B530B64733AFFDBF6
   A B C D E F G
 6 . . . . . . .
 5 . X . X . X .
 4 X . X . X . X
 3 X X X X X X X
 2 O O O O O O O
 1 . . . . . . .
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Encore - own throwin that temporarily breaks the ko shape should not clear the ko prohibition";
    Board board = Board::parseBoard(7,6,R"%%(
..o....
...o...
.xoxo..
..x.x..
...x...
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,3,board.x_size), P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,1,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);

    string expected = R"%%(
HASH: 7232311C746B8B9CD09B4B5E78F36FDB
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
HASH: 51A42639B1FD03594FC9F5DCAF16D642
   A B C D E F G
 6 . . O . . . .
 5 . . X O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
HASH: C28F759972CFA74DCA869C1EE08828C2
   A B C D E F G
 6 . . O . . . .
 5 . O . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Encore - ko prohibition clears if opponent moves without restoring the ko shape";
    Board board = Board::parseBoard(7,6,R"%%(
..o....
...o...
.xoxo..
..x.x..
...x...
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,3,board.x_size), P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,1,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);

    string expected = R"%%(
HASH: 7232311C746B8B9CD09B4B5E78F36FDB
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
HASH: 51A42639B1FD03594FC9F5DCAF16D642
   A B C D E F G
 6 . . O . . . .
 5 . . X O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
HASH: 3BA8E71777E554D6E368DCEC26777E08
   A B C D E F G
 6 O . O . . . .
 5 . . X O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


HASH: FEA42DE99C790CB13056CF1C1DE10E7C
   A B C D E F G
 6 O . O . . . .
 5 . . X O . . .
 4 . X . X O . .
 3 . . X . X . .
 2 . . . X . . .
 1 . . . . . . .

)%%";
    expect(name,out,expected);
  }


  {
    const char* name = "Encore - once only rule doesn't prevent the opponent moving there (filling ko)";
    Board board = Board::parseBoard(7,6,R"%%(
..o....
...o...
.xoxo..
..x.x..
...x...
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(3,3,board.x_size), P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    //Pass for ko
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    //Pass
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    //Take ko
    makeMoveAssertLegal(hist, board, Location::getLoc(3,2,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    //Pass
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);
    //Fill ko
    makeMoveAssertLegal(hist, board, Location::getLoc(3,3,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printEncoreKoProhibition(out,board,hist);

    string expected = R"%%(
HASH: 7232311C746B8B9CD09B4B5E78F36FDB
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited black at D4
HASH: 7232311C746B8B9CD09B4B5E78F36FDB
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


HASH: 7232311C746B8B9CD09B4B5E78F36FDB
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O . O . .
 3 . . X O X . .
 2 . . . X . . .
 1 . . . . . . .


HASH: A191A543B756FCD6B78EF314F5CEBE65
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O X O . .
 3 . . X . X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited white at D3
HASH: A191A543B756FCD6B78EF314F5CEBE65
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O X O . .
 3 . . X . X . .
 2 . . . X . . .
 1 . . . . . . .


Ko prohibited white at D3
HASH: 83A43A9FDE43E4E9601FF8E2CB94D35A
   A B C D E F G
 6 . . O . . . .
 5 . . . O . . .
 4 . X O X O . .
 3 . . X X X . .
 2 . . . X . . .
 1 . . . . . . .
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Territory scoring in the main phase";
    Board board = Board::parseBoard(7,7,R"%%(
ox.ooo.
oxxxxxx
ooooooo
.xoxx..
ooox...
x.oxxxx
.xox...
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(5,3,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(5,4,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(4,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(0,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,6,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    string expected = R"%%(
Score: 0.5
Score: 0.5
Score: 0.5
Score: -4.5
Score: -5.5
Score: -4.5
Score: -3.5
Score: -2.5
)%%";
    expect(name,out,expected);
  }
  {
    const char* name = "Territory scoring in encore 1";
    Board board = Board::parseBoard(7,7,R"%%(
ox.ooo.
oxxxxxx
ooooooo
.xoxx..
ooox...
x.oxxxx
.xox...
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(5,3,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,4,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(4,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(0,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,6,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    string expected = R"%%(
Score: 0.5
Score: 0.5
Score: 0.5
Score: -4.5
Score: -5.5
Score: -4.5
Score: -3.5
Score: -2.5
)%%";
    expect(name,out,expected);
  }
  {
    const char* name = "Territory scoring in encore 2";
    Board board = Board::parseBoard(7,7,R"%%(
ox.ooo.
oxxxxxx
ooooooo
.xoxx..
ooox...
x.oxxxx
.xox...
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_WHITE,rules,0);

    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(5,3,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,4,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(4,4,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(0,3,board.x_size), P_WHITE, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,6,board.x_size), P_BLACK, __LINE__);
    out << "Score: " << finalScoreIfGameEndedNow(hist,board) << endl;
    string expected = R"%%(
Score: 0.5
Score: 0.5
Score: 0.5
Score: -4.5
Score: -4.5
Score: -4.5
Score: -3.5
Score: -3.5
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Pass for ko";
    Board board = Board::parseBoard(7,7,R"%%(
..ox.oo
..oxxxo
...oox.
....oxx
..o.oo.
.......
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);
    Hash128 hasha;
    Hash128 hashb;
    Hash128 hashc;
    Hash128 hashd;
    Hash128 hashe;
    Hash128 hashf;

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.encorePhase == 1);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,2,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,1,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    out << "Black can't retake" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,2,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,2,board.x_size), P_WHITE, __LINE__);
    out << "Ko threat shouldn't work in the encore" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,6,board.x_size), P_WHITE, __LINE__);
    out << "Regular pass shouldn't work in the encore" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    out << "Pass for ko! (Should not affect the board stones)" << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(0,5,board.x_size), P_WHITE, __LINE__);
    hashd = hist.koHashHistory[hist.koHashHistory.size()-1];
    out << "Now black can retake, and white's retake isn't legal" << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    printIllegalMoves(out,board,hist,P_WHITE);
    hasha = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    hashb = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    hashc = hist.koHashHistory[hist.koHashHistory.size()-1];
    testAssert(hasha != hashb);
    testAssert(hasha != hashc);
    testAssert(hashb != hashc);
    out << "White's retake is legal after passing for ko" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    out << "Black's retake is illegal again" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hashd == hist.koHashHistory[hist.koHashHistory.size()-1]);
    out << "And is still illegal due to only-once" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,1,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,3,board.x_size), P_WHITE, __LINE__);
    out << "But a ko threat fixes that" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    out << "White illegal now" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    testAssert(hist.encorePhase == 1);
    hasha = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    hashc = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    hashc = hist.koHashHistory[hist.koHashHistory.size()-1];
    testAssert(hist.encorePhase == 2);
    testAssert(hasha != hashb);
    testAssert(hasha != hashc);
    testAssert(hashb != hashc);
    out << "Legal again in second encore" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    out << "Lastly, try black ko threat one more time" << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,2,board.x_size), P_WHITE, __LINE__);
    printIllegalMoves(out,board,hist,P_BLACK);
    out << "And a pass for ko" << endl;
    hashd = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    hashe = hist.koHashHistory[hist.koHashHistory.size()-1];
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    hashf = hist.koHashHistory[hist.koHashHistory.size()-1];
    printIllegalMoves(out,board,hist,P_BLACK);
    out << "And repeat with white" << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(5,0,board.x_size), P_WHITE, __LINE__);
    testAssert(hashd == hist.koHashHistory[hist.koHashHistory.size()-1]);
    makeMoveAssertLegal(hist, board, Location::getLoc(6,0,board.x_size), P_BLACK, __LINE__);
    testAssert(hashe == hist.koHashHistory[hist.koHashHistory.size()-1]);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hashf == hist.koHashHistory[hist.koHashHistory.size()-1]);
    out << "And see the only-once for black" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);

    string expected = R"%%(
Black can't retake
Ko-prohibited: (6,0) X
Ko threat shouldn't work in the encore
Ko-prohibited: (6,0) X
Regular pass shouldn't work in the encore
Ko-prohibited: (6,0) X
Pass for ko! (Should not affect the board stones)
HASH: 42FE4FEAAF27B840EA45877C528FEE84
   A B C D E F G
 7 . . O X X O .
 6 . . O X X X O
 5 . O X O O X .
 4 . . . . O X X
 3 . . O . O O .
 2 . . . . . . .
 1 O . . . . . .


Now black can retake, and white's retake isn't legal
Ko-prohibited: (5,0) O
White's retake is legal after passing for ko
Black's retake is illegal again
Ko-prohibited: (6,0) X
And is still illegal due to only-once
Illegal: (6,0) X
But a ko threat fixes that
White illegal now
Ko-prohibited: (5,0) O
Legal again in second encore
Lastly, try black ko threat one more time
Ko-prohibited: (6,0) X
And a pass for ko
And repeat with white
And see the only-once for black
Illegal: (6,0) X
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Two step ko mark clearing";
    Board board = Board::parseBoard(7,5,R"%%(
x.x....
.xx....
xox....
ooo....
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SITUATIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = true;
    BoardHistory hist(board,P_WHITE,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    testAssert(hist.encorePhase == 1);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,1,board.x_size), P_WHITE, __LINE__);
    out << "After first cap" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
    out << "After second cap" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
    out << "Just after black pass for ko" << endl;
    printIllegalMoves(out,board,hist,P_BLACK);
    out << board << endl;

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,0,board.x_size), P_BLACK, __LINE__);
    out <<"After first cap" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    out << board << endl;
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,2,board.x_size), P_BLACK, __LINE__);
    out << "After second cap" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    out << board << endl;
    makeMoveAssertLegal(hist, board, Location::getLoc(0,1,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    out << "After pass for ko" << endl;
    printIllegalMoves(out,board,hist,P_WHITE);
    out << board << endl;

    string expected = R"%%(
After first cap
Ko-prohibited: (0,2) X
After second cap
Ko-prohibited: (0,0) X
Just after black pass for ko
HASH: 3E2C923D4675E38712F67207D0B3D21B
   A B C D E F G
 5 . O X . . . .
 4 O X X . . . .
 3 . O X . . . .
 2 O O O . . . .
 1 . . . . . . .


After first cap
Ko-prohibited: (1,0) O
HASH: E51C9D5AE43BA59520B8877210F8CBED
   A B C D E F G
 5 X . X . . . .
 4 O X X . . . .
 3 . O X . . . .
 2 O O O . . . .
 1 . . . . . . .


After second cap
Ko-prohibited: (0,1) O
HASH: 8E15AD0AFD434346B3E4F2ED554621B7
   A B C D E F G
 5 X . X . . . .
 4 . X X . . . .
 3 X O X . . . .
 2 O O O . . . .
 1 . . . . . . .


After pass for ko
Illegal: (0,1) O
HASH: 8E15AD0AFD434346B3E4F2ED554621B7
   A B C D E F G
 5 X . X . . . .
 4 . X X . . . .
 3 X O X . . . .
 2 O O O . . . .
 1 . . . . . . .
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Throw in that destroys the ko momentarily does not clear ko prohibition";
    Board board = Board::parseBoard(7,5,R"%%(
x......
oxx....
.o.....
oo.....
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SITUATIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = true;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.encorePhase == 2);
    makeMoveAssertLegal(hist, board, Location::getLoc(0,2,board.x_size), P_BLACK, __LINE__);
    printIllegalMoves(out,board,hist,P_WHITE);
    makeMoveAssertLegal(hist, board, Location::getLoc(1,0,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(2,0,board.x_size), P_BLACK, __LINE__);
    out << board << endl;
    printIllegalMoves(out,board,hist,P_WHITE);

    string expected = R"%%(
Ko-prohibited: (0,1) O
HASH: 6CA50E111B93619273B4EEE5AC396990
   A B C D E F G
 5 X . X . . . .
 4 . X X . . . .
 3 X O . . . . .
 2 O O . . . . .
 1 . . . . . . .


Ko-prohibited: (0,1) O
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Various komis";
    Board board = Board::parseBoard(7,6,R"%%(
.......
.......
ooooooo
xxxxxxx
.......
.......
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    printGameResult(out,hist);

    hist.setKomi(0.0f);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    printGameResult(out,hist);

    hist.setKomi(-0.5f);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);
    testAssert(hist.isGameFinished == true);
    printGameResult(out,hist);

    string expected = R"%%(
Winner: White
W-B Score: 0.5
isNoResult: 0
isResignation: 0
Winner: Empty
W-B Score: 0
isNoResult: 0
isResignation: 0
Winner: Black
W-B Score: -0.5
isNoResult: 0
isResignation: 0
)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Stress test on tiny boards";

    Rand baseRand("Tiny board stress test");
    auto stressTest = [&](Board board, BoardHistory hist, Player nextPla, bool prolongGame) {
      Rand rand(baseRand.nextUInt64());
      for(int i = 0; i<1000; i++) {
        int numLegal = 0;
        static constexpr int MAX_LEGAL_MOVES = Board::MAX_PLAY_SIZE + 1;
        Loc legalMoves[MAX_LEGAL_MOVES];
        Loc move;

        for(int y = 0; y<board.y_size; y++) {
          for(int x = 0; x<board.x_size; x++) {
            move = Location::getLoc(x,y,board.x_size);
            if(hist.isLegal(board, move, nextPla)) legalMoves[numLegal++] = move;
          }
        }
        move = Board::PASS_LOC;
        if(hist.isLegal(board, move, nextPla)) legalMoves[numLegal++] = move;

        out << numLegal;
        out << " ";
        for(int y = 0; y<board.y_size; y++)
          for(int x = 0; x<board.x_size; x++)
            out << colorToChar(board.colors[Location::getLoc(x,y,board.x_size)]);
        out << " NP" << colorToChar(nextPla);
        out << " PS" << hist.consecutiveEndingPasses;
        out << " E" << hist.encorePhase;
        out << " ";
        for(int y = 0; y<board.y_size; y++)
          for(int x = 0; x<board.x_size; x++)
            out << (int)(hist.blackKoProhibited[Location::getLoc(x,y,board.x_size)]);
        out << " ";
        for(int y = 0; y<board.y_size; y++)
          for(int x = 0; x<board.x_size; x++)
            out << (int)(hist.whiteKoProhibited[Location::getLoc(x,y,board.x_size)]);
        out << " ";
        for(int y = 0; y<board.y_size; y++)
          for(int x = 0; x<board.x_size; x++)
            out << (int)(hist.secondEncoreStartColors[Location::getLoc(x,y,board.x_size)]);

        out << endl;

        if(hist.isGameFinished)
          break;

        testAssert(numLegal > 0);
        move = legalMoves[rand.nextUInt(numLegal)];
        if(prolongGame && move == Board::PASS_LOC)
          move = legalMoves[rand.nextUInt(numLegal)];
        makeMoveAssertLegal(hist, board, move, nextPla, __LINE__);
        nextPla = getOpp(nextPla);
      }
      printGameResult(out,hist);
    };

    Board emptyBoard22 = Board::parseBoard(2,2,R"%%(
..
..
)%%");

    Rules rules;
    string expected;

    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,true);
    rules.multiStoneSuicideLegal = true;
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,true);
    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
4 .X.. NPO PS0 E0 0000 0000 0000
3 .X.O NPX PS0 E0 0000 0000 0000
1 .XX. NPO PS0 E0 0000 0000 0000
3 .XX. NPX PS1 E0 0000 0000 0000
2 XXX. NPO PS0 E0 0000 0000 0000
4 ...O NPX PS0 E0 0000 0000 0000
4 ...O NPO PS1 E0 0000 0000 0000
1 O..O NPX PS0 E0 0000 0000 0000
3 O..O NPO PS1 E0 0000 0000 0000
2 O.OO NPX PS0 E0 0000 0000 0000
1 O.OO NPO PS1 E0 0000 0000 0000
2 O.OO NPX PS0 E1 0000 0000 0000
1 O.OO NPO PS1 E1 0000 0000 0000
2 O.OO NPX PS0 E2 0000 0000 2022
1 O.OO NPO PS1 E2 0000 0000 2022
2 O.OO NPX PS2 E2 0000 0000 2022
Winner: White
W-B Score: 2.5
isNoResult: 0
isResignation: 0
5 .... NPX PS0 E0 0000 0000 0000
4 ..X. NPO PS0 E0 0000 0000 0000
3 .OX. NPX PS0 E0 0000 0000 0000
2 XOX. NPO PS0 E0 0000 0000 0000
2 XOX. NPX PS1 E0 0000 0000 0000
2 X.XX NPO PS0 E0 0000 0000 0000
4 .O.. NPX PS0 E0 0000 0000 0000
3 XO.. NPO PS0 E0 0000 0000 0000
1 .OO. NPX PS0 E0 0000 0000 0000
3 .OO. NPO PS1 E0 0000 0000 0000
2 .OOO NPX PS0 E0 0000 0000 0000
4 X... NPO PS0 E0 0000 0000 0000
3 X..O NPX PS0 E0 0000 0000 0000
2 XX.O NPO PS0 E0 0000 0000 0000
3 ..OO NPX PS0 E0 0000 0000 0000
2 .XOO NPO PS0 E0 0000 0000 0000
2 .XOO NPX PS1 E0 0000 0000 0000
3 XX.. NPO PS0 E0 0000 0000 0000
2 XX.O NPX PS0 E0 0000 0000 0000
2 XXX. NPO PS0 E0 0000 0000 0000
4 ...O NPX PS0 E0 0000 0000 0000
3 .X.O NPO PS0 E0 0000 0000 0000
1 O..O NPX PS0 E0 0000 0000 0000
3 O..O NPO PS1 E0 0000 0000 0000
2 OO.O NPX PS0 E0 0000 0000 0000
4 ..X. NPO PS0 E0 0000 0000 0000
3 ..XO NPX PS0 E0 0000 0000 0000
3 ..XO NPO PS1 E0 0000 0000 0000
2 .OXO NPX PS0 E0 0000 0000 0000
2 .OXO NPO PS1 E0 0000 0000 0000
2 OO.O NPX PS0 E0 0000 0000 0000
4 ..X. NPO PS0 E0 0000 0000 0000
3 .OX. NPX PS0 E0 0000 0000 0000
2 .OXX NPO PS0 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
2 OOX. NPO PS0 E0 0000 0000 0000
2 OO.O NPX PS0 E0 0000 0000 0000
4 ..X. NPO PS0 E0 0000 0000 0000
3 O.X. NPX PS0 E0 0000 0000 0000
2 O.XX NPO PS0 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
2 OO.X NPO PS0 E0 0000 0000 0000
2 OOO. NPX PS0 E0 0000 0000 0000
4 ...X NPO PS0 E0 0000 0000 0000
3 O..X NPX PS0 E0 0000 0000 0000
2 O.XX NPO PS0 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
Winner: Empty
W-B Score: 0
isNoResult: 1
isResignation: 0
)%%";

    expect(name,out,expected);

    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,false);
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,false);
    rules.multiStoneSuicideLegal = true;
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,false);
    stressTest(emptyBoard22,BoardHistory(emptyBoard22,P_BLACK,rules,0),P_BLACK,false);
    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
5 .... NPO PS1 E0 0000 0000 0000
5 .... NPX PS2 E0 0000 0000 0000
Winner: White
W-B Score: 0.5
isNoResult: 0
isResignation: 0
5 .... NPX PS0 E0 0000 0000 0000
5 .... NPO PS1 E0 0000 0000 0000
4 O... NPX PS0 E0 0000 0000 0000
3 O.X. NPO PS0 E0 0000 0000 0000
2 OOX. NPX PS0 E0 0000 0000 0000
2 OOX. NPO PS1 E0 0000 0000 0000
2 OOX. NPX PS2 E0 0000 0000 0000
Winner: White
W-B Score: 1.5
isNoResult: 0
isResignation: 0
5 .... NPX PS0 E0 0000 0000 0000
4 .X.. NPO PS0 E0 0000 0000 0000
3 .XO. NPX PS0 E0 0000 0000 0000
2 .XOX NPO PS0 E0 0000 0000 0000
2 .XOX NPX PS1 E0 0000 0000 0000
2 .XOX NPO PS2 E0 0000 0000 0000
Winner: Black
W-B Score: -0.5
isNoResult: 0
isResignation: 0
5 .... NPX PS0 E0 0000 0000 0000
4 ...X NPO PS0 E0 0000 0000 0000
3 ..OX NPX PS0 E0 0000 0000 0000
3 ..OX NPO PS1 E0 0000 0000 0000
3 ..OX NPX PS2 E0 0000 0000 0000
Winner: White
W-B Score: 0.5
isNoResult: 0
isResignation: 0
)%%";

    expect(name,out,expected);

    Board koBoard71 = Board::parseBoard(7,1,R"%%(
.o.ox.o
)%%");
    Board koBoard41 = Board::parseBoard(4,1,R"%%(
....
)%%");

    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard71,BoardHistory(koBoard71,P_BLACK,rules,0),P_BLACK,true);

    expected = R"%%(
3 .O.OX.O NPX PS0 E0 0000000 0000000 0000000
1 .OX.X.O NPO PS0 E0 0000000 0000000 0000000
4 .OX.X.O NPX PS0 E0 0000000 0000000 0000000
2 .OXXX.O NPO PS0 E0 0000000 0000000 0000000
4 .O...OO NPX PS0 E0 0000000 0000000 0000000
6 .O..X.. NPO PS0 E0 0000000 0000000 0000000
4 .O..XO. NPX PS0 E0 0000000 0000000 0000000
3 .O.XXO. NPO PS0 E0 0000000 0000000 0000000
2 .O.XXO. NPX PS1 E0 0000000 0000000 0000000
3 .O.XX.X NPO PS0 E0 0000000 0000000 0000000
3 OO.XX.X NPX PS0 E0 0000000 0000000 0000000
4 ..XXX.X NPO PS0 E0 0000000 0000000 0000000
3 ..XXXO. NPX PS0 E0 0000000 0000000 0000000
2 .XXXXO. NPO PS0 E0 0000000 0000000 0000000
2 .XXXXO. NPX PS1 E0 0000000 0000000 0000000
1 .XXXX.X NPO PS0 E0 0000000 0000000 0000000
3 .XXXX.X NPX PS0 E0 0000000 0000000 0000000
2 .XXXXXX NPO PS0 E0 0000000 0000000 0000000
7 O...... NPX PS0 E0 0000000 0000000 0000000
5 O....X. NPO PS0 E0 0000000 0000000 0000000
5 O..O.X. NPX PS0 E0 0000000 0000000 0000000
3 .X.O.X. NPO PS0 E0 0000000 0000000 0000000
3 .X.OOX. NPX PS0 E0 0000000 0000000 0000000
3 XX.OOX. NPO PS0 E0 0000000 0000000 0000000
3 ..OOOX. NPX PS0 E0 0000000 0000000 0000000
3 X.OOOX. NPO PS0 E0 0000000 0000000 0000000
1 X.OOO.O NPX PS0 E0 0000000 0000000 0000000
3 X.OOO.O NPO PS0 E0 0000000 0000000 0000000
2 X.OOOOO NPX PS0 E0 0000000 0000000 0000000
2 X.OOOOO NPO PS1 E0 0000000 0000000 0000000
2 .OOOOOO NPX PS0 E0 0000000 0000000 0000000
1 .OOOOOO NPO PS1 E0 0000000 0000000 0000000
2 .OOOOOO NPX PS0 E1 0000000 0000000 0000000
7 X...... NPO PS0 E1 0000000 0000000 0000000
6 X..O... NPX PS0 E1 0000000 0000000 0000000
5 XX.O... NPO PS0 E1 0000000 0000000 0000000
1 XX.O.O. NPX PS0 E1 0000000 0000000 0000000
4 XX.O.O. NPO PS1 E1 0000000 0000000 0000000
3 ..OO.O. NPX PS0 E1 0000000 0000000 0000000
4 .XOO.O. NPO PS0 E1 0000000 0000000 0000000
2 O.OO.O. NPX PS0 E1 0100000 0000000 0000000
4 O.OO.O. NPO PS0 E1 0000000 0000000 0000000
2 OOOO.O. NPX PS0 E1 0000000 0000000 0000000
5 ....XO. NPO PS0 E1 0000000 0000000 0000000
4 ..O.XO. NPX PS0 E1 0000000 0000000 0000000
5 ..O.X.X NPO PS0 E1 0000000 0000010 0000000
4 O.O.X.X NPX PS0 E1 0000000 0000010 0000000
3 O.O.XXX NPO PS0 E1 0000000 0000000 0000000
2 O.O.XXX NPX PS1 E1 0000000 0000000 0000000
3 O.O.XXX NPO PS0 E2 0000000 0000000 2020111
2 OOO.XXX NPX PS0 E2 0000000 0000000 2020111
2 OOO.XXX NPO PS1 E2 0000000 0000000 2020111
4 OOOO... NPX PS0 E2 0000000 0000000 2020111
3 OOOO..X NPO PS0 E2 0000000 0000000 2020111
2 OOOO.O. NPX PS0 E2 0000000 0000000 2020111
5 ....XO. NPO PS0 E2 0000000 0000000 2020111
4 ...O.O. NPX PS0 E2 0000000 0000000 2020111
5 X..O.O. NPO PS0 E2 0000000 0000000 2020111
4 X..O.OO NPX PS0 E2 0000000 0000000 2020111
5 X..OX.. NPO PS0 E2 0000000 0000000 2020111
4 X..OX.O NPX PS0 E2 0000000 0000000 2020111
3 XX.OX.O NPO PS0 E2 0000000 0000000 2020111
4 ..OOX.O NPX PS0 E2 0000000 0000000 2020111
4 ..OOXX. NPO PS0 E2 0000000 0000000 2020111
5 ..OO..O NPX PS0 E2 0000000 0000000 2020111
4 ..OOX.O NPO PS0 E2 0000000 0000000 2020111
3 O.OOX.O NPX PS0 E2 0000000 0000000 2020111
2 O.OOXX. NPO PS0 E2 0000000 0000000 2020111
4 O.OO..O NPX PS0 E2 0000000 0000000 2020111
2 O.OOX.O NPO PS0 E2 0000000 0000000 2020111
3 O.OO.OO NPX PS0 E2 0000000 0000000 2020111
2 .XOO.OO NPO PS0 E2 0000000 1000000 2020111
2 .XOO.OO NPX PS0 E2 0000000 0000000 2020111
2 .XOO.OO NPO PS1 E2 0000000 0000000 2020111
3 O.OO.OO NPX PS0 E2 0100000 0000000 2020111
3 O.OO.OO NPO PS0 E2 0000000 0000000 2020111
2 OOOO.OO NPX PS0 E2 0000000 0000000 2020111
7 ....X.. NPO PS0 E2 0000000 0000000 2020111
6 ....X.O NPX PS0 E2 0000000 0000000 2020111
4 ...XX.O NPO PS0 E2 0000000 0000000 2020111
4 O..XX.O NPX PS0 E2 0000000 0000000 2020111
3 O..XXX. NPO PS0 E2 0000000 0000000 2020111
3 OO.XXX. NPX PS0 E2 0000000 0000000 2020111
2 OO.XXXX NPO PS0 E2 0000000 0000000 2020111
5 OOO.... NPX PS0 E2 0000000 0000000 2020111
3 OOO.X.. NPO PS0 E2 0000000 0000000 2020111
3 OOO.X.O NPX PS0 E2 0000000 0000000 2020111
1 OOO.XX. NPO PS0 E2 0000000 0000000 2020111
3 OOO.XX. NPX PS1 E2 0000000 0000000 2020111
2 OOO.XXX NPO PS0 E2 0000000 0000000 2020111
2 OOO.XXX NPX PS1 E2 0000000 0000000 2020111
4 ...XXXX NPO PS0 E2 0000000 0000000 2020111
1 .O.XXXX NPX PS0 E2 0000000 0000000 2020111
3 .O.XXXX NPO PS1 E2 0000000 0000000 2020111
5 .OO.... NPX PS0 E2 0000000 0000000 2020111
4 .OOX... NPO PS0 E2 0000000 0000000 2020111
3 .OO.O.. NPX PS0 E2 0000000 0000000 2020111
4 .OO.O.X NPO PS0 E2 0000000 0000000 2020111
1 .OOOO.X NPX PS0 E2 0000000 0000000 2020111
3 .OOOO.X NPO PS1 E2 0000000 0000000 2020111
1 .OOOOO. NPX PS0 E2 0000000 0000000 2020111
3 .OOOOO. NPO PS1 E2 0000000 0000000 2020111
2 OOOOOO. NPX PS0 E2 0000000 0000000 2020111
1 OOOOOO. NPO PS1 E2 0000000 0000000 2020111
2 OOOOOO. NPX PS2 E2 0000000 0000000 2020111
Winner: White
W-B Score: 1.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);

    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard41,BoardHistory(koBoard41,P_BLACK,rules,0),P_BLACK,true);
    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
4 X... NPO PS0 E0 0000 0000 0000
1 X.O. NPX PS0 E0 0000 0000 0000
3 X.O. NPO PS1 E0 0000 0000 0000
2 X.OO NPX PS0 E0 0000 0000 0000
3 XX.. NPO PS0 E0 0000 0000 0000
3 XX.. NPX PS1 E0 0000 0000 0000
3 XX.. NPO PS0 E1 0000 0000 0000
2 XX.O NPX PS0 E1 0000 0000 0000
2 XXX. NPO PS0 E1 0000 0000 0000
4 ...O NPX PS0 E1 0000 0000 0000
3 X..O NPO PS0 E1 0000 0000 0000
2 X.OO NPX PS0 E1 0000 0000 0000
3 XX.. NPO PS0 E1 0000 0000 0000
3 ..O. NPX PS0 E1 0000 0000 0000
2 .XO. NPO PS0 E1 0000 0000 0000
2 O.O. NPX PS0 E1 0100 0000 0000
3 O.O. NPO PS0 E1 0000 0000 0000
2 OOO. NPX PS0 E1 0000 0000 0000
4 ...X NPO PS0 E1 0000 0000 0000
3 ..O. NPX PS0 E1 0000 0000 0000
3 X.O. NPO PS0 E1 0000 0000 0000
1 .OO. NPX PS0 E1 0000 0000 0000
3 .OO. NPO PS1 E1 0000 0000 0000
2 OOO. NPX PS0 E1 0000 0000 0000
1 OOO. NPO PS1 E1 0000 0000 0000
2 OOO. NPX PS0 E2 0000 0000 2220
1 OOO. NPO PS1 E2 0000 0000 2220
2 OOO. NPX PS2 E2 0000 0000 2220
Winner: White
W-B Score: 0.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);

    rules.koRule = Rules::KO_SITUATIONAL;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard41,BoardHistory(koBoard41,P_BLACK,rules,0),P_BLACK,true);
    expected = R"%%(

5 .... NPX PS0 E0 0000 0000 0000
4 X... NPO PS0 E0 0000 0000 0000
3 .O.. NPX PS0 E0 0000 0000 0000
3 .O.X NPO PS0 E0 0000 0000 0000
2 OO.X NPX PS0 E0 0000 0000 0000
2 ..XX NPO PS0 E0 0000 0000 0000
2 O.XX NPX PS0 E0 0000 0000 0000
2 O.XX NPO PS1 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
3 ..X. NPO PS0 E0 0000 0000 0000
2 O.X. NPX PS0 E0 0000 0000 0000
1 .XX. NPO PS0 E0 0000 0000 0000
3 .XX. NPX PS1 E0 0000 0000 0000
2 .XXX NPO PS0 E0 0000 0000 0000
4 O... NPX PS0 E0 0000 0000 0000
2 O..X NPO PS0 E0 0000 0000 0000
2 O.O. NPX PS0 E0 0000 0000 0000
1 .XO. NPO PS0 E0 0000 0000 0000
2 .XO. NPX PS1 E0 0000 0000 0000
1 .X.X NPO PS0 E0 0000 0000 0000
2 .X.X NPX PS1 E0 0000 0000 0000
2 .X.X NPO PS0 E1 0000 0000 0000
2 .XO. NPX PS0 E1 0001 0000 0000
2 .XO. NPO PS0 E1 0000 0000 0000
2 O.O. NPX PS0 E1 0100 0000 0000
3 O.O. NPO PS1 E1 0100 0000 0000
2 O.OO NPX PS0 E1 0000 0000 0000
3 .X.. NPO PS0 E1 0000 0000 0000
3 .X.O NPX PS0 E1 0000 0000 0000
2 XX.O NPO PS0 E1 0000 0000 0000
3 ..OO NPX PS0 E1 0000 0000 0000
2 X.OO NPO PS0 E1 0000 0000 0000
2 .OOO NPX PS0 E1 0000 0000 0000
4 X... NPO PS0 E1 0000 0000 0000
1 X.O. NPX PS0 E1 0000 0000 0000
3 X.O. NPO PS1 E1 0000 0000 0000
1 .OO. NPX PS0 E1 0000 0000 0000
3 .OO. NPO PS1 E1 0000 0000 0000
2 .OOO NPX PS0 E1 0000 0000 0000
4 X... NPO PS0 E1 0000 0000 0000
3 X..O NPX PS0 E1 0000 0000 0000
3 X..O NPO PS1 E1 0000 0000 0000
2 X.OO NPX PS0 E1 0000 0000 0000
3 XX.. NPO PS0 E1 0000 0000 0000
3 ..O. NPX PS0 E1 0000 0000 0000
1 .XO. NPO PS0 E1 0000 0000 0000
2 .XO. NPX PS1 E1 0000 0000 0000
2 .X.X NPO PS0 E1 0000 0010 0000
3 .X.X NPX PS0 E1 0000 0000 0000
2 XX.X NPO PS0 E1 0000 0000 0000
1 XX.X NPX PS1 E1 0000 0000 0000
2 XX.X NPO PS0 E2 0000 0000 1101
1 XX.X NPX PS1 E2 0000 0000 1101
2 XX.X NPO PS2 E2 0000 0000 1101
Winner: Black
W-B Score: -3.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);


    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard41,BoardHistory(koBoard41,P_BLACK,rules,0),P_BLACK,true);

    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
4 ...X NPO PS0 E0 0000 0000 0000
3 ..O. NPX PS0 E0 0000 0000 0000
2 .XO. NPO PS0 E0 0000 0000 0000
1 O.O. NPX PS0 E0 0000 0000 0000
3 O.O. NPO PS0 E0 0000 0000 0000
2 O.OO NPX PS0 E0 0000 0000 0000
3 .X.. NPO PS0 E0 0000 0000 0000
2 .XO. NPX PS0 E0 0000 0000 0000
1 .X.X NPO PS0 E0 0000 0000 0000
3 .X.X NPX PS0 E0 0000 0000 0000
2 .XXX NPO PS0 E0 0000 0000 0000
4 O... NPX PS0 E0 0000 0000 0000
4 O... NPO PS1 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
3 ..X. NPO PS0 E0 0000 0000 0000
2 .OX. NPX PS0 E0 0000 0000 0000
1 X.X. NPO PS0 E0 0000 0000 0000
3 X.X. NPX PS0 E0 0000 0000 0000
2 X.XX NPO PS0 E0 0000 0000 0000
3 .O.. NPX PS0 E0 0000 0000 0000
3 .O.X NPO PS0 E0 0000 0000 0000
1 .OO. NPX PS0 E0 0000 0000 0000
3 .OO. NPO PS1 E0 0000 0000 0000
2 OOO. NPX PS0 E0 0000 0000 0000
4 ...X NPO PS0 E0 0000 0000 0000
3 O..X NPX PS0 E0 0000 0000 0000
2 O.XX NPO PS0 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
2 OO.X NPO PS0 E0 0000 0000 0000
2 OO.X NPX PS1 E0 0000 0000 0000
3 ..XX NPO PS0 E0 0000 0000 0000
2 O.XX NPX PS0 E0 0000 0000 0000
2 O.XX NPO PS1 E0 0000 0000 0000
3 OO.. NPX PS0 E0 0000 0000 0000
2 OO.X NPO PS0 E0 0000 0000 0000
2 OO.X NPX PS1 E0 0000 0000 0000
Winner: White
W-B Score: 1.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);

    rules.koRule = Rules::KO_POSITIONAL;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard41,BoardHistory(koBoard41,P_BLACK,rules,0),P_BLACK,true);
    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
4 ...X NPO PS0 E0 0000 0000 0000
1 .O.X NPX PS0 E0 0000 0000 0000
3 .O.X NPO PS1 E0 0000 0000 0000
2 OO.X NPX PS0 E0 0000 0000 0000
3 ..XX NPO PS0 E0 0000 0000 0000
2 .O.. NPX PS0 E0 0000 0000 0000
4 .O.. NPO PS1 E0 0000 0000 0000
2 OO.. NPX PS0 E0 0000 0000 0000
3 OO.. NPO PS1 E0 0000 0000 0000
1 OOO. NPX PS0 E0 0000 0000 0000
1 OOO. NPO PS1 E0 0000 0000 0000
1 OOO. NPX PS2 E0 0000 0000 0000
Winner: White
W-B Score: 4.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);

    rules.koRule = Rules::KO_SITUATIONAL;
    rules.scoringRule = Rules::SCORING_AREA;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    stressTest(koBoard41,BoardHistory(koBoard41,P_BLACK,rules,0),P_BLACK,true);
    expected = R"%%(
5 .... NPX PS0 E0 0000 0000 0000
4 X... NPO PS0 E0 0000 0000 0000
3 .O.. NPX PS0 E0 0000 0000 0000
3 .O.X NPO PS0 E0 0000 0000 0000
1 .OO. NPX PS0 E0 0000 0000 0000
3 .OO. NPO PS1 E0 0000 0000 0000
1 .OOO NPX PS0 E0 0000 0000 0000
1 .OOO NPO PS1 E0 0000 0000 0000
1 .OOO NPX PS2 E0 0000 0000 0000
Winner: White
W-B Score: 4.5
isNoResult: 0
isResignation: 0

)%%";

    expect(name,out,expected);


  }


  {
    const char* name = "Board history clearing directly to the encore";
    Board board = Board::parseBoard(4,4,R"%%(
..o.
.o.o
.xox
..xx
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = true;
    BoardHistory hist(board,P_BLACK,rules,0);
    BoardHistory hist2(board,P_BLACK,rules,0);

    auto compareHists = [&]() {
      out << hist.moveHistory.size() << " " << hist2.moveHistory.size() << endl;
      out << hist.koHashHistory.size() << " " << hist2.koHashHistory.size() << endl;
      out << hist.koHashHistory[0] << " " << hist2.koHashHistory[0] << endl;
      out << hist.koHistoryLastClearedBeginningMoveIdx << " " << hist2.koHistoryLastClearedBeginningMoveIdx << endl;
      out << hist.getRecentBoard(0).pos_hash <<  " " << hist2.getRecentBoard(0).pos_hash << endl;
      out << hist.getRecentBoard(1).pos_hash <<  " " << hist2.getRecentBoard(1).pos_hash << endl;
      out << hist.getRecentBoard(2).pos_hash <<  " " << hist2.getRecentBoard(2).pos_hash << endl;
      out << hist.getRecentBoard(3).pos_hash <<  " " << hist2.getRecentBoard(3).pos_hash << endl;
      out << hist.getRecentBoard(4).pos_hash <<  " " << hist2.getRecentBoard(4).pos_hash << endl;
      out << hist.getRecentBoard(5).pos_hash <<  " " << hist2.getRecentBoard(5).pos_hash << endl;

      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist.wasEverOccupiedOrPlayed[i] == hist2.wasEverOccupiedOrPlayed[i]);
      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist.superKoBanned[i] == false);
      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist2.superKoBanned[i] == false);

      out << hist.consecutiveEndingPasses << " " << hist2.consecutiveEndingPasses << endl;
      out << hist.hashesAfterBlackPass.size() << " " << hist2.hashesAfterBlackPass.size() << endl;
      out << hist.hashesAfterWhitePass.size() << " " << hist2.hashesAfterWhitePass.size() << endl;
      out << hist.encorePhase << " " << hist2.encorePhase << endl;

      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist.blackKoProhibited[i] == false);
      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist2.blackKoProhibited[i] == false);
      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist.whiteKoProhibited[i] == false);
      for(int i = 0; i<Board::MAX_ARR_SIZE; i++)
        testAssert(hist2.whiteKoProhibited[i] == false);

      out << hist.koProhibitHash << " " << hist2.koProhibitHash << endl;
      out << hist.koCapturesInEncore.size() << " " << hist2.koCapturesInEncore.size() << endl;

      for(int y = 0; y<board.y_size; y++)
        for(int x = 0; x<board.x_size; x++)
          out << (int)(hist.secondEncoreStartColors[Location::getLoc(x,y,board.x_size)]);
      out << endl;
      for(int y = 0; y<board.y_size; y++)
        for(int x = 0; x<board.x_size; x++)
          out << (int)(hist2.secondEncoreStartColors[Location::getLoc(x,y,board.x_size)]);
      out << endl;

      out << hist.whiteBonusScore << " " << hist2.whiteBonusScore << endl;
      out << hist.isGameFinished << " " << hist2.isGameFinished << endl;
      out << (int)hist.winner << " " << (int)hist2.winner << endl;
      out << hist.finalWhiteMinusBlackScore << " " << hist2.finalWhiteMinusBlackScore << endl;
      out << hist.isNoResult << " " << hist2.isNoResult << endl;

    };

    Board copy = board;
    makeMoveAssertLegal(hist, copy, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, copy, Board::PASS_LOC, P_WHITE, __LINE__);

    hist2.clear(board, P_BLACK, hist2.rules, 1);

    compareHists();
    string expected = R"%%(

2 0
1 1
F43A55D89EAFC93CA62848648DA051CF F43A55D89EAFC93CA62848648DA051CF
2 0
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
0 0
0 0
0 0
1 1
00000000000000000000000000000000 00000000000000000000000000000000
0 0
0000000000000000
0000000000000000
0 0
0 0
0 0
0 0
0 0

)%%";
    expect(name,out,expected);

    makeMoveAssertLegal(hist, copy, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, copy, Board::PASS_LOC, P_WHITE, __LINE__);

    hist2.clear(board, P_BLACK, hist2.rules, 2);

    compareHists();
    expected = R"%%(

4 0
1 1
F43A55D89EAFC93CA62848648DA051CF F43A55D89EAFC93CA62848648DA051CF
4 0
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
D314459C37E7C630DCB23301AE1B492C D314459C37E7C630DCB23301AE1B492C
0 0
0 0
0 0
2 2
00000000000000000000000000000000 00000000000000000000000000000000
0 0
0020020201210011
0020020201210011
0 0
0 0
0 0
0 0
0 0

)%%";
    expect(name,out,expected);
  }

  {
    const char* name = "Test case failing in search before";
    Board board = Board::parseBoard(9,9,R"%%(
XXXXXXXXX
X.OXXXXXX
XXXXOXXXX
XXX.OOXX.
OXXXOOXXX
.OXXXXXXO
XXXX.XOOO
XXXOXOOOO
XXXOO.OOO
)%%");
    Rules rules;
    rules.koRule = Rules::KO_SIMPLE;
    rules.scoringRule = Rules::SCORING_TERRITORY;
    rules.komi = 0.5f;
    rules.multiStoneSuicideLegal = false;
    BoardHistory hist(board,P_BLACK,rules,0);

    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Board::PASS_LOC, P_WHITE, __LINE__);

    testAssert(hist.encorePhase == 1);
    makeMoveAssertLegal(hist, board, Location::getLoc(8,3,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,6,board.x_size), P_WHITE, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,7,board.x_size), P_BLACK, __LINE__);
    makeMoveAssertLegal(hist, board, Location::getLoc(4,7,board.x_size), P_WHITE, __LINE__);
    out << board << endl;

    string expected = R"%%(
HASH: C377EB251DBAB5E2F6C1BABE18EEE392
   A B C D E F G H J
 9 X X X X X X X X X
 8 X . O X X X X X X
 7 X X X X O X X X X
 6 X X X . O O X X X
 5 O X X X O O X X X
 4 . O X X X X X X O
 3 X X X X O X O O O
 2 X X X O O O O O O
 1 X X X O O . O O O
)%%";
    expect(name,out,expected);
  }


       
}
