#!/usr/bin/python3
import sys
import os
import argparse
import traceback
import random
import math
import time
import re
import logging
import colorsys
import json
import tensorflow as tf
import numpy as np

from board import Board
from model import Model
import common

description = """
Play go with a trained neural net!
Implements a basic GTP engine that uses the neural net directly to play moves.
"""

parser = argparse.ArgumentParser(description=description)
common.add_model_load_args(parser)
parser.add_argument('-name-scope', help='Name scope for model variables', required=False)
args = vars(parser.parse_args())

(model_variables_prefix, model_config_json) = common.load_model_paths(args)
name_scope = args["name_scope"]

#Hardcoded max board size
pos_len = 19

# Model ----------------------------------------------------------------

two_over_pi = 0.63661977236758134308

with open(model_config_json) as f:
  model_config = json.load(f)

if name_scope is not None:
  with tf.name_scope(name_scope):
    model = Model(model_config,pos_len,{})
else:
  model = Model(model_config,pos_len,{})
policy0_output = tf.nn.softmax(model.policy_output[:,:,0])
policy1_output = tf.nn.softmax(model.policy_output[:,:,1])
value_output = tf.nn.softmax(model.value_output)
scoremean_output = 20.0 * model.miscvalues_output[:,0]
scorestdev_output = 20.0 * tf.math.softplus(model.miscvalues_output[:,1])
ownership_output = tf.tanh(model.ownership_output)
scorebelief_output = tf.nn.softmax(model.scorebelief_output)
bonusbelief_output = tf.nn.softmax(model.bonusbelief_output)
sbscale = model.sbscale3_layer
# bbscale = model.bbscale3_layer
# sbscale = tf.zeros([1],dtype=tf.float32)
bbscale = tf.zeros([1],dtype=tf.float32)

# Moves ----------------------------------------------------------------

def fetch_output(session, board, boards, moves, use_history_prop, rules, fetches):
  bin_input_data = np.zeros(shape=[1]+model.bin_input_shape, dtype=np.float32)
  global_input_data = np.zeros(shape=[1]+model.global_input_shape, dtype=np.float32)
  pla = board.pla
  opp = Board.get_opp(pla)
  move_idx = len(moves)
  model.fill_row_features(board,pla,opp,boards,moves,move_idx,rules,bin_input_data,global_input_data,use_history_prop=use_history_prop,idx=0)
  outputs = session.run(fetches, feed_dict={
    model.bin_inputs: bin_input_data,
    model.global_inputs: global_input_data,
    model.symmetries: [False,False,False],
    model.is_training: False,
    model.include_history: [[1.0,1.0,1.0,1.0,1.0]]
  })
  return [output[0] for output in outputs]

def get_policy_output(session, board, boards, moves, use_history_prop, rules):
  return fetch_output(session,board,boards,moves,use_history_prop,rules,[policy0_output])
def get_policy1_output(session, board, boards, moves, use_history_prop, rules):
  return fetch_output(session,board,boards,moves,use_history_prop,rules,[policy1_output])

def get_scorebelief_output(session, board, boards, moves, use_history_prop, rules):
  return fetch_output(session,board,boards,moves,use_history_prop,rules,[scorebelief_output,scoremean_output,scorestdev_output,sbscale])
def get_bonusbelief_output(session, board, boards, moves, use_history_prop, rules):
  return fetch_output(session,board,boards,moves,use_history_prop,rules,[bonusbelief_output,bbscale])

def get_policy_and_value_output(session, board, boards, moves, use_history_prop, rules):
  (policy,value,scoremean) = fetch_output(session,board,boards,moves,use_history_prop,rules,[policy0_output,value_output,scoremean_output])
  value = list(value)
  value.append(scoremean)
  return (policy,value)

def get_policy1_and_value_output(session, board, boards, moves, use_history_prop, rules):
  (policy,value,scoremean) = fetch_output(session,board,boards,moves,use_history_prop,rules,[policy1_output,value_output,scoremean_output])
  value = list(value)
  value.append(scoremean)
  return (policy,value)

def get_ownership_values(session, board, boards, moves, use_history_prop, rules):
  [ownership] = fetch_output(session,board,boards,moves,use_history_prop,rules,[ownership_output])
  ownership = ownership.reshape([model.pos_len * model.pos_len])
  locs_and_values = []
  for y in range(board.size):
    for x in range(board.size):
      loc = board.loc(x,y)
      pos = model.loc_to_tensor_pos(loc,board)
      if board.pla == Board.WHITE:
        locs_and_values.append((loc,ownership[pos]))
      else:
        locs_and_values.append((loc,-ownership[pos]))
  return locs_and_values

def get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop, rules):
  pla = board.pla
  [policy,value] = get_policy_and_value_output(session, board, boards, moves, use_history_prop, rules)
  moves_and_probs = []
  for i in range(len(policy)):
    move = model.tensor_pos_to_loc(i,board)
    if i == len(policy)-1:
      moves_and_probs.append((Board.PASS_LOC,policy[i]))
    elif board.would_be_legal(pla,move):
      moves_and_probs.append((move,policy[i]))
  return (moves_and_probs,value)

def get_moves_and_probs1_and_value(session, board, boards, moves, use_history_prop, rules):
  pla = board.pla
  [policy,value] = get_policy1_and_value_output(session, board, boards, moves, use_history_prop, rules)
  moves_and_probs = []
  for i in range(len(policy)):
    move = model.tensor_pos_to_loc(i,board)
    if i == len(policy)-1:
      moves_and_probs.append((Board.PASS_LOC,policy[i]))
    else:
      moves_and_probs.append((move,policy[i]))
  return (moves_and_probs,value)

def genmove_and_value(session, board, boards, moves, use_history_prop, rules):
  (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop, rules)
  moves_and_probs = sorted(moves_and_probs, key=lambda moveandprob: moveandprob[1], reverse=True)

  if len(moves_and_probs) <= 0:
    return (Board.PASS_LOC,value)

  #Generate a random number biased small and then find the appropriate move to make
  #Interpolate from moving uniformly to choosing from the triangular distribution
  alpha = 1
  beta = 1 + math.sqrt(max(0,len(moves)-20))
  r = np.random.beta(alpha,beta)
  probsum = 0.0
  i = 0
  while True:
    (move,prob) = moves_and_probs[i]
    probsum += prob
    if i >= len(moves_and_probs)-1 or probsum > r:
      return (move,value)
    i += 1

def get_layer_values(session, board, boards, moves, rules, layer, channel):
  [layer] = fetch_output(session,board,boards,moves,use_history_prop=1.0,rules=rules,fetches=[layer])
  layer = layer.reshape([model.pos_len * model.pos_len,-1])
  locs_and_values = []
  for y in range(board.size):
    for x in range(board.size):
      loc = board.loc(x,y)
      pos = model.loc_to_tensor_pos(loc,board)
      locs_and_values.append((loc,layer[pos,channel]))
  return locs_and_values

def get_input_feature(board, boards, moves, rules, feature_idx):
  bin_input_data = np.zeros(shape=[1]+model.bin_input_shape, dtype=np.float32)
  global_input_data = np.zeros(shape=[1]+model.global_input_shape, dtype=np.float32)
  pla = board.pla
  opp = Board.get_opp(pla)
  move_idx = len(moves)
  model.fill_row_features(board,pla,opp,boards,moves,move_idx,rules,bin_input_data,global_input_data,use_history_prop=1.0,idx=0)

  locs_and_values = []
  for y in range(board.size):
    for x in range(board.size):
      loc = board.loc(x,y)
      pos = model.loc_to_tensor_pos(loc,board)
      locs_and_values.append((loc,bin_input_data[0,pos,feature_idx]))
  return locs_and_values

def get_pass_alive(board, rules):
  pla = board.pla
  opp = Board.get_opp(pla)
  area = [-1 for i in range(board.arrsize)]
  nonPassAliveStones = False
  safeBigTerritories = True
  unsafeBigTerritories = False
  board.calculateArea(area,nonPassAliveStones,safeBigTerritories,unsafeBigTerritories,rules["multiStoneSuicideLegal"])

  locs_and_values = []
  for y in range(board.size):
    for x in range(board.size):
      loc = board.loc(x,y)
      locs_and_values.append((loc,area[loc]))
  return locs_and_values


def fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div, is_percent, value_and_score=None):
  divisor = 1.0
  if normalization_div == "max":
    max_abs_value = max(abs(value) for (loc,value) in locs_and_values)
    divisor = max(0.0000000001,max_abs_value) #avoid divide by zero
  elif normalization_div is not None:
    divisor = normalization_div

  #Caps value at 1.0, using an asymptotic curve
  def loose_cap(x):
    def transformed_softplus(x):
      return -math.log(math.exp(-(x-1.0)*8.0)+1.0)/8.0+1.0
    base = transformed_softplus(0.0)
    return (transformed_softplus(x) - base) / (1.0 - base)

  #Softly curves a value so that it ramps up faster than linear in that range
  def soft_curve(x,x0,x1):
    p = (x-x0)/(x1-x0)
    def curve(p):
      return math.sqrt(p+0.16)-0.4
    p = curve(p) / curve(1.0)
    return x0 + p * (x1-x0)

  for (loc,value) in locs_and_values:
    if loc != Board.PASS_LOC:
      value = value / divisor
      if value < 0:
        value = -value
        huestart = 0.50
        huestop = 0.86
      else:
        huestart = -0.02
        huestop = 0.38

      value = loose_cap(value)

      def lerp(p,x0,x1,y0,y1):
        return y0 + (y1-y0) * (p-x0)/(x1-x0)

      if value <= 0.04:
        hue = huestart
        lightness = 0.5
        saturation = value / 0.04
        (r,g,b) = colorsys.hls_to_rgb((hue+1)%1, lightness, saturation)
      elif value <= 0.70:
        # value = soft_curve(value,0.04,0.70)
        hue = lerp(value,0.04,0.70,huestart,huestop)
        val = 1.0
        saturation = 1.0
        (r,g,b) = colorsys.hsv_to_rgb((hue+1)%1, val, saturation)
      else:
        hue = huestop
        lightness = lerp(value,0.70,1.00,0.5,0.95)
        saturation = 1.0
        (r,g,b) = colorsys.hls_to_rgb((hue+1)%1, lightness, saturation)

      r = ("%02x" % int(r*255))
      g = ("%02x" % int(g*255))
      b = ("%02x" % int(b*255))
      gfx_commands.append("COLOR #%s%s%s %s" % (r,g,b,str_coord(loc,board)))

  locs_and_values = sorted(locs_and_values, key=lambda loc_and_value: loc_and_value[1])
  locs_and_values_rev = sorted(locs_and_values, key=lambda loc_and_value: loc_and_value[1], reverse=True)
  texts = []
  texts_rev = []
  texts_value = []
  maxlen_per_side = 10
  if len(locs_and_values) > 0 and locs_and_values[0][1] < 0:
    maxlen_per_side = 5

    for i in range(min(len(locs_and_values),maxlen_per_side)):
      (loc,value) = locs_and_values[i]
      if is_percent:
        texts.append("%s %4.1f%%" % (str_coord(loc,board),value*100))
      else:
        texts.append("%s %.3f" % (str_coord(loc,board),value))
    texts.reverse()

  for i in range(min(len(locs_and_values_rev),maxlen_per_side)):
    (loc,value) = locs_and_values_rev[i]
    if is_percent:
      texts_rev.append("%s %4.1f%%" % (str_coord(loc,board),value*100))
    else:
      texts_rev.append("%s %.3f" % (str_coord(loc,board),value))

  if value_and_score is not None:
    texts_value.append("wv %.2fc nr %.2f%% ws %.1f" % (
      100*(value_and_score[0]-value_and_score[1] if board.pla == Board.WHITE else value_and_score[1] - value_and_score[0]),
      100*value_and_score[2],
      (value_and_score[3] if board.pla == Board.WHITE else -value_and_score[3])
    ))

  gfx_commands.append("TEXT " + ", ".join(texts_value + texts_rev + texts))

def print_scorebelief(board,scorebelief,scoremean,scorestdev,sbscale):
  scorebelief = list(scorebelief)
  if board.pla != Board.WHITE:
    scorebelief.reverse()
    scoremean = -scoremean

  scoredistrmid = pos_len * pos_len + Model.EXTRA_SCORE_DISTR_RADIUS
  ret = ""
  ret += "TEXT "
  ret += "SBScale: " + str(sbscale) + "\n"
  ret += "ScoreBelief: \n"
  for i in range(17,-1,-1):
    ret += "TEXT "
    ret += "%+6.1f" %(-(i*20+0.5))
    for j in range(20):
      idx = scoredistrmid-(i*20+j)-1
      ret += " %4.0f" % (scorebelief[idx] * 10000)
    ret += "\n"
  for i in range(18):
    ret += "TEXT "
    ret += "%+6.1f" %((i*20+0.5))
    for j in range(20):
      idx = scoredistrmid+(i*20+j)
      ret += " %4.0f" % (scorebelief[idx] * 10000)
    ret += "\n"

  beliefscore = 0
  beliefscoresq = 0
  beliefwin = 0
  belieftotal = 0
  for idx in range(scoredistrmid*2):
    score = idx-scoredistrmid+0.5
    if score > 0:
      beliefwin += scorebelief[idx]
    else:
      beliefwin -= scorebelief[idx]
    belieftotal += scorebelief[idx]
    beliefscore += score*scorebelief[idx]
    beliefscoresq += score*score*scorebelief[idx]

  beliefscoremean = beliefscore/belieftotal
  beliefscoremeansq = beliefscoresq/belieftotal
  beliefscorevar = max(0,beliefscoremeansq-beliefscoremean*beliefscoremean)
  beliefscorestdev = math.sqrt(beliefscorevar)

  ret += "TEXT BeliefWin: %.2fc\n" % (100*beliefwin/belieftotal)
  ret += "TEXT BeliefScoreMean: %.1f\n" % (beliefscoremean)
  ret += "TEXT BeliefScoreStdev: %.1f\n" % (beliefscorestdev)
  ret += "TEXT ScoreMean: %.1f\n" % (scoremean)
  ret += "TEXT ScoreStdev: %.1f\n" % (scorestdev)
  return ret

def print_bonusbelief(board,bonusbelief,bbscale):
  bonusbelief = list(bonusbelief)
  if board.pla != Board.WHITE:
    bonusbelief.reverse()
  bonusdistrmid = Model.BONUS_SCORE_RADIUS
  ret = ""
  ret += "TEXT "
  ret += "BBScale: " + str(bbscale) + "\n"
  ret += "BonusBelief: \n"
  for i in range(5,-1,-1):
    ret += "TEXT "
    ret += "%+6.1f" %(-(i*5)-1)
    for j in range(5):
      idx = bonusdistrmid-(i*5+j)-1
      ret += " %4.0f" % (bonusbelief[idx] * 10000)
    ret += "\n"

  ret += "TEXT "
  ret += "%+6.1f" %(0)
  ret += " %4.0f" % (bonusbelief[bonusdistrmid] * 10000)
  ret += "\n"

  for i in range(6):
    ret += "TEXT "
    ret += "%+6.1f" %((i*5+1))
    for j in range(5):
      idx = bonusdistrmid+(i*5+j)+1
      ret += " %4.0f" % (bonusbelief[idx] * 10000)
    ret += "\n"

  return ret


# Basic parsing --------------------------------------------------------
colstr = 'ABCDEFGHJKLMNOPQRST'
def parse_coord(s,board):
  if s == 'pass':
    return Board.PASS_LOC
  return board.loc(colstr.index(s[0].upper()), board.size - int(s[1:]))

def str_coord(loc,board):
  if loc == Board.PASS_LOC:
    return 'pass'
  x = board.loc_x(loc)
  y = board.loc_y(loc)
  return '%c%d' % (colstr[x], board.size - y)


# GTP Implementation -----------------------------------------------------

#Adapted from https://github.com/pasky/michi/blob/master/michi.py, which is distributed under MIT license
#https://opensource.org/licenses/MIT
def run_gtp(session):
  known_commands = [
    'boardsize',
    'clear_board',
    'showboard',
    'komi',
    'play',
    'genmove',
    'quit',
    'name',
    'version',
    'known_command',
    'list_commands',
    'protocol_version',
    'gogui-analyze_commands',
    'policy',
    'logpolicy',
    'policy1',
    'policy-japanese',
    'policy-encore1',
    'policy-encore2',
    'policy-no-history',
    'ownership',
    'ownership-japanese',
    'scorebelief',
    'scorebelief-japanese',
    'bonusbelief',
    'bonusbelief-japanese',
    'passalive',
  ]
  known_analyze_commands = [
    'gfx/Policy/policy',
    'gfx/LogPolicy/logpolicy',
    'gfx/Policy1/policy1',
    'gfx/PolicyJP/policy-japanese',
    'gfx/PolicyE1/policy-encore1',
    'gfx/PolicyE2/policy-encore2',
    'gfx/PolicyNoHistory/policy-no-history',
    'gfx/Ownership/ownership',
    'gfx/OwnershipJP/ownership-japanese',
    'gfx/ScoreBelief/scorebelief',
    'gfx/ScoreBeliefJP/scorebelief-japanese',
    'gfx/BonusBelief/bonusbelief',
    'gfx/BonusBeliefJP/bonusbelief-japanese',
    'gfx/PassAlive/passalive',
  ]

  board_size = 19
  board = Board(size=board_size)
  moves = []
  boards = [board.copy()]

  layerdict = dict(model.outputs_by_layer)
  weightdict = dict()
  for v in tf.trainable_variables():
    weightdict[v.name] = v

  layer_command_lookup = dict()


  def add_extra_board_size_visualizations(layer_name, layer, normalization_div):
    assert(layer.shape[1].value == board_size)
    assert(layer.shape[2].value == board_size)
    num_channels = layer.shape[3].value
    for i in range(num_channels):
      command_name = layer_name + "-" + str(i)
      command_name = command_name.replace("/",":")
      known_commands.append(command_name)
      known_analyze_commands.append("gfx/" + command_name + "/" + command_name)
      layer_command_lookup[command_name.lower()] = (layer,i,normalization_div)

  def add_layer_visualizations(layer_name, normalization_div):
    if layer_name in layerdict:
      layer = layerdict[layer_name]
      add_extra_board_size_visualizations(layer_name, layer, normalization_div)

  add_layer_visualizations("conv1",normalization_div=6)
  add_layer_visualizations("rconv1",normalization_div=14)
  add_layer_visualizations("rconv2",normalization_div=20)
  add_layer_visualizations("rconv3",normalization_div=26)
  add_layer_visualizations("rconv4",normalization_div=36)
  add_layer_visualizations("rconv5",normalization_div=40)
  add_layer_visualizations("rconv6",normalization_div=40)
  add_layer_visualizations("rconv7",normalization_div=44)
  add_layer_visualizations("rconv7/conv1a",normalization_div=12)
  add_layer_visualizations("rconv7/conv1b",normalization_div=12)
  add_layer_visualizations("rconv8",normalization_div=48)
  add_layer_visualizations("rconv9",normalization_div=52)
  add_layer_visualizations("rconv10",normalization_div=55)
  add_layer_visualizations("rconv11",normalization_div=58)
  add_layer_visualizations("rconv11/conv1a",normalization_div=12)
  add_layer_visualizations("rconv11/conv1b",normalization_div=12)
  add_layer_visualizations("rconv12",normalization_div=58)
  add_layer_visualizations("rconv13",normalization_div=64)
  add_layer_visualizations("rconv14",normalization_div=66)
  add_layer_visualizations("g1",normalization_div=6)
  add_layer_visualizations("p1",normalization_div=2)
  add_layer_visualizations("v1",normalization_div=4)

  input_feature_command_lookup = dict()
  def add_input_feature_visualizations(layer_name, feature_idx, normalization_div):
    command_name = layer_name
    command_name = command_name.replace("/",":")
    known_commands.append(command_name)
    known_analyze_commands.append("gfx/" + command_name + "/" + command_name)
    input_feature_command_lookup[command_name] = (feature_idx,normalization_div)

  for i in range(model.bin_input_shape[1]):
    add_input_feature_visualizations("input-" + str(i),i, normalization_div=1)


  linear = tf.cumsum(tf.ones([19],dtype=tf.float32),axis=0,exclusive=True) / 18.0
  color_calibration = tf.stack(axis=0,values=[
    linear,
    linear*0.5,
    linear*0.2,
    linear*0.1,
    linear*0.05,
    linear*0.02,
    linear*0.01,
    -linear,
    -linear*0.5,
    -linear*0.2,
    -linear*0.1,
    -linear*0.05,
    -linear*0.02,
    -linear*0.01,
    linear*2-1,
    tf.zeros([19],dtype=tf.float32),
    tf.zeros([19],dtype=tf.float32),
    tf.zeros([19],dtype=tf.float32),
    tf.zeros([19],dtype=tf.float32),
  ])
  add_extra_board_size_visualizations("colorcalibration", tf.reshape(color_calibration,[1,19,19,1]),normalization_div=None)

  while True:
    try:
      line = input().strip()
    except EOFError:
      break
    if line == '':
      continue
    command = [s.lower() for s in line.split()]
    if re.match('\d+', command[0]):
      cmdid = command[0]
      command = command[1:]
    else:
      cmdid = ''

    ret = ''
    if command[0] == "boardsize":
      if int(command[1]) > model.pos_len:
        print("Warning: Trying to set incompatible boardsize %s (!= %d)" % (command[1], N), file=sys.stderr)
        ret = None
      board_size = int(command[1])
      board = Board(size=board_size)
      moves = []
      boards = [board.copy()]
    elif command[0] == "clear_board":
      board = Board(size=board_size)
      moves = []
      boards = [board.copy()]
    elif command[0] == "showboard":
      ret = "\n" + board.to_string().strip()
    elif command[0] == "komi":
      pass
    elif command[0] == "play":
      pla = (Board.BLACK if command[1] == "B" or command[1] == "b" else Board.WHITE)
      loc = parse_coord(command[2],board)
      board.play(pla,loc)
      moves.append((pla,loc))
      boards.append(board.copy())
    elif command[0] == "genmove":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (loc,value) = genmove_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      pla = board.pla
      if len(command) > 1:
        pla = (Board.BLACK if command[1] == "B" or command[1] == "b" else Board.WHITE)
      board.play(pla,loc)
      moves.append((pla,loc))
      boards.append(board.copy())
      ret = str_coord(loc,board)

    # elif command[0] == "final_score":
    #   ret = '0'
    elif command[0] == "name":
      ret = 'simplenn'
    elif command[0] == "version":
      ret = '0.1'
    elif command[0] == "list_commands":
      ret = '\n'.join(known_commands)
    elif command[0] == "known_command":
      ret = 'true' if command[1] in known_commands else 'false'
    elif command[0] == "gogui-analyze_commands":
      ret = '\n'.join(known_analyze_commands)
    elif command[0] == "policy":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "logpolicy":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      moves_and_logprobs = [(move,max(0.0,4.9+math.log10(prob))) for (move,prob) in moves_and_probs]
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_logprobs, board, normalization_div=6, is_percent=False, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "policy1":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs1_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "policy-japanese":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "policy-encore1":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 1,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "policy-encore2":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 2,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "policy-no-history":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      (moves_and_probs,value) = get_moves_and_probs_and_value(session, board, boards, moves, use_history_prop=0.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, moves_and_probs, board, normalization_div=None, is_percent=True, value_and_score=value)
      ret = "\n".join(gfx_commands)
    elif command[0] == "ownership":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      locs_and_values = get_ownership_values(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div=None, is_percent=True, value_and_score=None)
      ret = "\n".join(gfx_commands)
    elif command[0] == "ownership-japanese":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      locs_and_values = get_ownership_values(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div=None, is_percent=True, value_and_score=None)
      ret = "\n".join(gfx_commands)
    elif command[0] in layer_command_lookup:
      (layer,channel,normalization_div) = layer_command_lookup[command[0]]
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      locs_and_values = get_layer_values(session, board, boards, moves, rules, layer, channel)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div, is_percent=False)
      ret = "\n".join(gfx_commands)

    elif command[0] in input_feature_command_lookup:
      (feature_idx,normalization_div) = input_feature_command_lookup[command[0]]
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      locs_and_values = get_input_feature(board, boards, moves, rules, feature_idx)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div, is_percent=False)
      ret = "\n".join(gfx_commands)

    elif command[0] == "passalive":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      locs_and_values = get_pass_alive(board, rules)
      gfx_commands = []
      fill_gfx_commands_for_heatmap(gfx_commands, locs_and_values, board, normalization_div=None, is_percent=False)
      ret = "\n".join(gfx_commands)

    elif command[0] == "scorebelief":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      [scorebelief,scoremean,scorestdev,sbscale] = get_scorebelief_output(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      ret = print_scorebelief(board,scorebelief,scoremean,scorestdev,sbscale)

    elif command[0] == "scorebelief-japanese":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      [scorebelief,scoremean,scorestdev,sbscale] = get_scorebelief_output(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      ret = print_scorebelief(board,scorebelief,scoremean,scorestdev,sbscale)

    elif command[0] == "bonusbelief":
      rules = {
        "koRule": "KO_POSITIONAL",
        "scoringRule": "SCORING_AREA",
        "multiStoneSuicideLegal": True,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      [bonusbelief,bbscale] = get_bonusbelief_output(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      ret = print_bonusbelief(board,bonusbelief,bbscale)

    elif command[0] == "bonusbelief-japanese":
      rules = {
        "koRule": "KO_SIMPLE",
        "scoringRule": "SCORING_TERRITORY",
        "multiStoneSuicideLegal": False,
        "encorePhase": 0,
        "passWouldEndPhase": False,
        "whiteKomi": 7.5
      }
      [bonusbelief,bbscale] = get_bonusbelief_output(session, board, boards, moves, use_history_prop=1.0, rules=rules)
      ret = print_bonusbelief(board,bonusbelief,bbscale)

    elif command[0] == "protocol_version":
      ret = '2'
    elif command[0] == "quit":
      print('=%s \n\n' % (cmdid,), end='')
      break
    else:
      print('Warning: Ignoring unknown command - %s' % (line,), file=sys.stderr)
      ret = None

    if ret is not None:
      print('=%s %s\n\n' % (cmdid, ret,), end='')
    else:
      print('?%s ???\n\n' % (cmdid,), end='')
    sys.stdout.flush()

saver = tf.train.Saver(
  max_to_keep = 10000,
  save_relative_paths = True,
)

with tf.Session() as session:
  saver.restore(session, model_variables_prefix)
  run_gtp(session)



