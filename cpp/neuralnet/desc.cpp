#include "../neuralnet/desc.h"

#include <cmath>
#include <fstream>
#include <zstr/src/zstr.hpp>

#include "../core/global.h"
#include "../neuralnet/modelversion.h"
#include "../neuralnet/nninterface.h"

using namespace std;

static void checkWeightFinite(float f, const string& name) {
  if(!isfinite(f))
    throw StringError(name + ": Nan or infinite neural net weight or parameter");
}
#define CHECKFINITE(x, name) \
  { checkWeightFinite((x), name); }

ConvLayerDesc::ConvLayerDesc()
  : convYSize(0), convXSize(0), inChannels(0), outChannels(0), dilationY(1), dilationX(1) {}

ConvLayerDesc::ConvLayerDesc(istream& in) {
  in >> name;
  in >> convYSize;
  in >> convXSize;
  in >> inChannels;
  in >> outChannels;
  in >> dilationY;
  in >> dilationX;

  if(in.fail())
    throw StringError(name + ": convlayer failed to parse sizes and channels and dilations");

  if(convXSize <= 0 || convYSize <= 0)
    throw StringError(name + ": convolution filter sizes must be positive");
  if(inChannels <= 0 || outChannels <= 0)
    throw StringError(name + ": number of in and out channels must be positive");
  if(dilationX <= 0 || dilationY <= 0)
    throw StringError(name + ": dilation factors must be positive");
  if(convXSize % 2 != 1 || convYSize % 2 != 1)
    throw StringError(name + ": convolution filter sizes must be odd, found even sizes");

  // Model file order is y,x,ic,oc
  // Cuda's order is oc,ic,y,x
  int numWeights = convYSize * convXSize * inChannels * outChannels;
  weights.resize(numWeights);
  int ocStride = convYSize * convXSize * inChannels;
  int icStride = convYSize * convXSize;
  int yStride = convXSize;
  int xStride = 1;

  for(int y = 0; y < convYSize; y++) {
    for(int x = 0; x < convXSize; x++) {
      for(int ic = 0; ic < inChannels; ic++) {
        for(int oc = 0; oc < outChannels; oc++) {
          float w;
          in >> w;
          CHECKFINITE(w, name);
          weights[oc * ocStride + ic * icStride + y * yStride + x * xStride] = w;
        }
      }
    }
  }
  if(in.fail())
    throw StringError(name + ": convlayer failed to expected number of float weights");
}

ConvLayerDesc::ConvLayerDesc(ConvLayerDesc&& other) {
  *this = std::move(other);
}

ConvLayerDesc& ConvLayerDesc::operator=(ConvLayerDesc&& other) {
  name = std::move(other.name);
  convYSize = other.convYSize;
  convXSize = other.convXSize;
  inChannels = other.inChannels;
  outChannels = other.outChannels;
  dilationY = other.dilationY;
  dilationX = other.dilationX;
  weights = std::move(other.weights);
  return *this;
}

//-----------------------------------------------------------------------------

BatchNormLayerDesc::BatchNormLayerDesc() : numChannels(0), epsilon(0.001), hasScale(false), hasBias(false) {}

BatchNormLayerDesc::BatchNormLayerDesc(istream& in) {
  in >> name;
  in >> numChannels;
  in >> epsilon;
  in >> hasScale;
  in >> hasBias;

  if(in.fail())
    throw StringError(name + ": bnlayer failed to parse num channels and epsilon and hasScale and hasBias");

  if(numChannels < 1)
    throw StringError(name + ": numChannels (" + Global::intToString(numChannels) + ") < 1");
  if(epsilon <= 0)
    throw StringError(name + ": epsilon (" + Global::floatToString(epsilon) + ") <= 0");

  float w;
  mean.resize(numChannels);
  for(int c = 0; c < numChannels; c++) {
    in >> w;
    CHECKFINITE(w, name);
    mean[c] = w;
  }
  variance.resize(numChannels);
  for(int c = 0; c < numChannels; c++) {
    in >> w;
    CHECKFINITE(w, name);
    variance[c] = w;
  }
  scale.resize(numChannels);
  for(int c = 0; c < numChannels; c++) {
    if(hasScale)
      in >> w;
    else
      w = 1.0;
    CHECKFINITE(w, name);
    scale[c] = w;
  }
  bias.resize(numChannels);
  for(int c = 0; c < numChannels; c++) {
    if(hasBias)
      in >> w;
    else
      w = 1.0;
    CHECKFINITE(w, name);
    bias[c] = w;
  }

  if(in.fail())
    throw StringError(
      name + ": bnlayer failed to parse expected number of batch norm mean, variance, bias, scale values");
}

BatchNormLayerDesc::BatchNormLayerDesc(BatchNormLayerDesc&& other) {
  *this = std::move(other);
}

BatchNormLayerDesc& BatchNormLayerDesc::operator=(BatchNormLayerDesc&& other) {
  name = std::move(other.name);
  numChannels = other.numChannels;
  epsilon = other.epsilon;
  hasScale = other.hasScale;
  hasBias = other.hasBias;
  mean = std::move(other.mean);
  variance = std::move(other.variance);
  scale = std::move(other.scale);
  bias = std::move(other.bias);
  return *this;
}

//-----------------------------------------------------------------------------

ActivationLayerDesc::ActivationLayerDesc() {}

ActivationLayerDesc::ActivationLayerDesc(istream& in) {
  in >> name;
}

ActivationLayerDesc::ActivationLayerDesc(ActivationLayerDesc&& other) {
  *this = std::move(other);
}

ActivationLayerDesc& ActivationLayerDesc::operator=(ActivationLayerDesc&& other) {
  name = std::move(other.name);
  return *this;
}

//-----------------------------------------------------------------------------

MatMulLayerDesc::MatMulLayerDesc() : inChannels(0), outChannels(0) {}

MatMulLayerDesc::MatMulLayerDesc(istream& in) {
  in >> name;
  in >> inChannels;
  in >> outChannels;

  if(in.fail())
    throw StringError(name + ": matmullayer failed to parse num channels");
  if(inChannels <= 0 || outChannels <= 0)
    throw StringError(name + ": number of in and out channels must be positive");

  // Model file order is ic,oc
  // Cublas order used is also ic,oc since we transpose
  int numWeights = inChannels * outChannels;
  weights.resize(numWeights);
  int icStride = outChannels;
  int ocStride = 1;

  for(int ic = 0; ic < inChannels; ic++) {
    for(int oc = 0; oc < outChannels; oc++) {
      float w;
      in >> w;
      CHECKFINITE(w, name);
      weights[oc * ocStride + ic * icStride] = w;
    }
  }
  if(in.fail())
    throw StringError(name + ": matmullayer failed to parse expected number of matmul weights");
}

MatMulLayerDesc::MatMulLayerDesc(MatMulLayerDesc&& other) {
  *this = std::move(other);
}

MatMulLayerDesc& MatMulLayerDesc::operator=(MatMulLayerDesc&& other) {
  name = std::move(other.name);
  inChannels = other.inChannels;
  outChannels = other.outChannels;
  weights = std::move(other.weights);
  return *this;
}

//-----------------------------------------------------------------------------

MatBiasLayerDesc::MatBiasLayerDesc() : numChannels(0) {}

MatBiasLayerDesc::MatBiasLayerDesc(istream& in) {
  in >> name;
  in >> numChannels;

  if(in.fail())
    throw StringError(name + ": matbiaslayer failed to parse num channels");
  if(numChannels <= 0)
    throw StringError(name + ": number of channels must be positive");

  weights.resize(numChannels);

  for(int c = 0; c < numChannels; c++) {
    float w;
    in >> w;
    CHECKFINITE(w, name);
    weights[c] = w;
  }
  if(in.fail())
    throw StringError(name + ": matbiaslayer failed to parse expected number of matbias weights");
}

MatBiasLayerDesc::MatBiasLayerDesc(MatBiasLayerDesc&& other) {
  *this = std::move(other);
}

MatBiasLayerDesc& MatBiasLayerDesc::operator=(MatBiasLayerDesc&& other) {
  name = std::move(other.name);
  numChannels = other.numChannels;
  weights = std::move(other.weights);
  return *this;
}

//-----------------------------------------------------------------------------

ResidualBlockDesc::ResidualBlockDesc() {}

ResidualBlockDesc::ResidualBlockDesc(istream& in) {
  in >> name;
  if(in.fail())
    throw StringError(name + ": res block failed to parse name");

  preBN = BatchNormLayerDesc(in);
  preActivation = ActivationLayerDesc(in);
  regularConv = ConvLayerDesc(in);
  midBN = BatchNormLayerDesc(in);
  midActivation = ActivationLayerDesc(in);
  finalConv = ConvLayerDesc(in);

  if(preBN.numChannels != regularConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": preBN.numChannels (%d) != regularConv.inChannels (%d)", preBN.numChannels, regularConv.inChannels));
  if(midBN.numChannels != regularConv.outChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != regularConv.outChannels (%d)", midBN.numChannels, regularConv.outChannels));
  if(midBN.numChannels != finalConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != finalConv.inChannels (%d)", midBN.numChannels, finalConv.inChannels));

  if(in.fail())
    throw StringError(name + ": res block parse failure (istream fail() return true)");
}

ResidualBlockDesc::ResidualBlockDesc(ResidualBlockDesc&& other) {
  *this = std::move(other);
}

ResidualBlockDesc& ResidualBlockDesc::operator=(ResidualBlockDesc&& other) {
  name = std::move(other.name);
  preBN = std::move(other.preBN);
  preActivation = std::move(other.preActivation);
  regularConv = std::move(other.regularConv);
  midBN = std::move(other.midBN);
  midActivation = std::move(other.midActivation);
  finalConv = std::move(other.finalConv);
  return *this;
}

//-----------------------------------------------------------------------------

DilatedResidualBlockDesc::DilatedResidualBlockDesc() {}

DilatedResidualBlockDesc::DilatedResidualBlockDesc(istream& in) {
  in >> name;
  if(in.fail())
    throw StringError(name + ": dilated res block failed to parse name");

  preBN = BatchNormLayerDesc(in);
  preActivation = ActivationLayerDesc(in);
  regularConv = ConvLayerDesc(in);
  dilatedConv = ConvLayerDesc(in);
  midBN = BatchNormLayerDesc(in);
  midActivation = ActivationLayerDesc(in);
  finalConv = ConvLayerDesc(in);

  if(preBN.numChannels != regularConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": preBN.numChannels (%d) != regularConv.inChannels (%d)", preBN.numChannels, regularConv.inChannels));
  if(preBN.numChannels != dilatedConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": preBN.numChannels (%d) != dilatedConv.inChannels (%d)", preBN.numChannels, dilatedConv.inChannels));
  if(midBN.numChannels != regularConv.outChannels + dilatedConv.outChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != regularConv.outChannels (%d) + dilatedConv.outChannels (%d)",
               midBN.numChannels,
               regularConv.outChannels,
               dilatedConv.outChannels));
  if(midBN.numChannels != finalConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != finalConv.inChannels (%d)", midBN.numChannels, finalConv.inChannels));

  if(in.fail())
    throw StringError(name + ": dilated res block parse failure (istream fail() return true)");
}

DilatedResidualBlockDesc::DilatedResidualBlockDesc(DilatedResidualBlockDesc&& other) {
  *this = std::move(other);
}

DilatedResidualBlockDesc& DilatedResidualBlockDesc::operator=(DilatedResidualBlockDesc&& other) {
  name = std::move(other.name);
  preBN = std::move(other.preBN);
  preActivation = std::move(other.preActivation);
  regularConv = std::move(other.regularConv);
  dilatedConv = std::move(other.dilatedConv);
  midBN = std::move(other.midBN);
  midActivation = std::move(other.midActivation);
  finalConv = std::move(other.finalConv);
  return *this;
}

//-----------------------------------------------------------------------------

GlobalPoolingResidualBlockDesc::GlobalPoolingResidualBlockDesc() {}

GlobalPoolingResidualBlockDesc::GlobalPoolingResidualBlockDesc(istream& in, int vrsn) {
  in >> name;
  if(in.fail())
    throw StringError(name + ": gpool res block failed to parse name");
  version = vrsn;
  preBN = BatchNormLayerDesc(in);
  preActivation = ActivationLayerDesc(in);
  regularConv = ConvLayerDesc(in);
  gpoolConv = ConvLayerDesc(in);
  gpoolBN = BatchNormLayerDesc(in);
  gpoolActivation = ActivationLayerDesc(in);
  gpoolToBiasMul = MatMulLayerDesc(in);
  midBN = BatchNormLayerDesc(in);
  midActivation = ActivationLayerDesc(in);
  finalConv = ConvLayerDesc(in);

  if(preBN.numChannels != regularConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": preBN.numChannels (%d) != regularConv.inChannels (%d)", preBN.numChannels, regularConv.inChannels));
  if(preBN.numChannels != gpoolConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": preBN.numChannels (%d) != gpoolConv.inChannels (%d)", preBN.numChannels, gpoolConv.inChannels));
  if(gpoolBN.numChannels != gpoolConv.outChannels)
    throw StringError(
      name + Global::strprintf(
               ": gpoolBN.numChannels (%d) != gpoolConv.outChannels (%d)", gpoolBN.numChannels, gpoolConv.outChannels));
  if(version >= 3) {
    if(gpoolBN.numChannels * 3 != gpoolToBiasMul.inChannels)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolBN.numChannels * 3 (%d) != gpoolToBiasMul.inChannels (%d)",
                 gpoolBN.numChannels * 3,
                 gpoolToBiasMul.inChannels));
  } else {
    if(gpoolBN.numChannels * 2 != gpoolToBiasMul.inChannels)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolBN.numChannels * 2 (%d) != gpoolToBiasMul.inChannels (%d)",
                 gpoolBN.numChannels * 2,
                 gpoolToBiasMul.inChannels));
  }
  if(midBN.numChannels != regularConv.outChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != regularConv.outChannels (%d)", midBN.numChannels, regularConv.outChannels));
  if(midBN.numChannels != gpoolToBiasMul.outChannels)
    throw StringError(
      name +
      Global::strprintf(
        ": midBN.numChannels (%d) != gpoolToBiasMul.outChannels (%d)", midBN.numChannels, gpoolToBiasMul.outChannels));
  if(midBN.numChannels != finalConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": midBN.numChannels (%d) != finalConv.inChannels (%d)", midBN.numChannels, finalConv.inChannels));

  if(in.fail())
    throw StringError(name + ": gpool res block parse failure (istream fail() return true)");
}

GlobalPoolingResidualBlockDesc::GlobalPoolingResidualBlockDesc(GlobalPoolingResidualBlockDesc&& other) {
  *this = std::move(other);
}

GlobalPoolingResidualBlockDesc& GlobalPoolingResidualBlockDesc::operator=(GlobalPoolingResidualBlockDesc&& other) {
  name = std::move(other.name);
  preBN = std::move(other.preBN);
  preActivation = std::move(other.preActivation);
  regularConv = std::move(other.regularConv);
  gpoolConv = std::move(other.gpoolConv);
  gpoolBN = std::move(other.gpoolBN);
  gpoolActivation = std::move(other.gpoolActivation);
  gpoolToBiasMul = std::move(other.gpoolToBiasMul);
  midBN = std::move(other.midBN);
  midActivation = std::move(other.midActivation);
  finalConv = std::move(other.finalConv);
  return *this;
}

//-----------------------------------------------------------------------------

TrunkDesc::TrunkDesc()
  : version(-1),
    numBlocks(0),
    trunkNumChannels(0),
    midNumChannels(0),
    regularNumChannels(0),
    dilatedNumChannels(0),
    gpoolNumChannels(0) {}

TrunkDesc::TrunkDesc(istream& in, int vrsn) {
  in >> name;
  version = vrsn;
  in >> numBlocks;
  in >> trunkNumChannels;
  in >> midNumChannels;
  in >> regularNumChannels;
  in >> dilatedNumChannels;
  in >> gpoolNumChannels;

  if(in.fail())
    throw StringError(name + ": trunk failed to parse num blocks or various channel parameters");
  if(numBlocks < 1)
    throw StringError(name + ": trunk num blocks must be positive");
  if(
    trunkNumChannels <= 0 || midNumChannels <= 0 || regularNumChannels <= 0 || dilatedNumChannels <= 0 ||
    gpoolNumChannels <= 0)
    throw StringError(name + ": all numbers of channels must be positive");
  if(midNumChannels != regularNumChannels + dilatedNumChannels)
    throw StringError(name + ": midNumChannels != regularNumChannels + dilatedNumChannels");

  initialConv = ConvLayerDesc(in);
  if(initialConv.outChannels != trunkNumChannels)
    throw StringError(
      name + Global::strprintf(
               ": %s initialConv.outChannels (%d) != trunkNumChannels (%d)",
               initialConv.name.c_str(),
               initialConv.outChannels,
               trunkNumChannels));

  if(version >= 3) {
    initialMatMul = MatMulLayerDesc(in);
    if(initialMatMul.outChannels != trunkNumChannels)
      throw StringError(
        name + Global::strprintf(
                 ": %s initialMatMul.outChannels (%d) != trunkNumChannels (%d)",
                 initialMatMul.name.c_str(),
                 initialMatMul.outChannels,
                 trunkNumChannels));
  }

  string kind;
  for(int i = 0; i < numBlocks; i++) {
    in >> kind;
    if(in.fail())
      throw StringError(name + ": failed to parse block kind");
    if(kind == "ordinary_block") {
      ResidualBlockDesc* desc = new ResidualBlockDesc(in);

      if(desc->preBN.numChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s preBN.numChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->preBN.numChannels,
                   trunkNumChannels));
      if(desc->regularConv.outChannels != midNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s regularConv.outChannels (%d) != regularNumChannels+dilatedNumChannels (%d)",
                   desc->name.c_str(),
                   desc->regularConv.outChannels,
                   regularNumChannels + dilatedNumChannels));
      if(desc->regularConv.outChannels != midNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s regularConv.outChannels (%d) != midNumChannels (%d)",
                   desc->name.c_str(),
                   desc->regularConv.outChannels,
                   midNumChannels));
      if(desc->finalConv.outChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s finalConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->finalConv.outChannels,
                   trunkNumChannels));

      blocks.push_back(make_pair(ORDINARY_BLOCK_KIND, (void*)desc));
    } else if(kind == "dilated_block") {
      DilatedResidualBlockDesc* desc = new DilatedResidualBlockDesc(in);

      if(desc->preBN.numChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s preBN.numChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->preBN.numChannels,
                   trunkNumChannels));
      if(desc->regularConv.outChannels != regularNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s regularConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->regularConv.outChannels,
                   regularNumChannels));
      if(desc->dilatedConv.outChannels != dilatedNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s dilatedConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->dilatedConv.outChannels,
                   dilatedNumChannels));
      if(desc->finalConv.outChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s finalConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->finalConv.outChannels,
                   trunkNumChannels));

      blocks.push_back(make_pair(DILATED_BLOCK_KIND, (void*)desc));
    } else if(kind == "gpool_block") {
      GlobalPoolingResidualBlockDesc* desc = new GlobalPoolingResidualBlockDesc(in, version);

      if(desc->preBN.numChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s preBN.numChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->preBN.numChannels,
                   trunkNumChannels));
      if(desc->regularConv.outChannels != regularNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s regularConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->regularConv.outChannels,
                   regularNumChannels));
      if(desc->gpoolConv.outChannels != gpoolNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s gpoolConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->gpoolConv.outChannels,
                   gpoolNumChannels));
      if(desc->finalConv.outChannels != trunkNumChannels)
        throw StringError(
          name + Global::strprintf(
                   ": %s finalConv.outChannels (%d) != trunkNumChannels (%d)",
                   desc->name.c_str(),
                   desc->finalConv.outChannels,
                   trunkNumChannels));

      blocks.push_back(make_pair(GLOBAL_POOLING_BLOCK_KIND, (void*)desc));
    } else
      throw StringError(name + ": found unknown block kind: " + kind);

    if(in.fail())
      throw StringError(name + ": trunk istream fail after parsing block");
  }

  trunkTipBN = BatchNormLayerDesc(in);
  trunkTipActivation = ActivationLayerDesc(in);

  if(trunkTipBN.numChannels != trunkNumChannels)
    throw StringError(
      name + Global::strprintf(
               ": trunkTipBN.numChannels (%d) != trunkNumChannels (%d)", trunkTipBN.numChannels, trunkNumChannels));

  if(in.fail())
    throw StringError(name + ": trunk istream fail after parsing tip");
}

TrunkDesc::~TrunkDesc() {
  for(int i = 0; i < blocks.size(); i++) {
    if(blocks[i].first == ORDINARY_BLOCK_KIND) {
      ResidualBlockDesc* desc = (ResidualBlockDesc*)blocks[i].second;
      delete desc;
    } else if(blocks[i].first == DILATED_BLOCK_KIND) {
      DilatedResidualBlockDesc* desc = (DilatedResidualBlockDesc*)blocks[i].second;
      delete desc;
    } else if(blocks[i].first == GLOBAL_POOLING_BLOCK_KIND) {
      GlobalPoolingResidualBlockDesc* desc = (GlobalPoolingResidualBlockDesc*)blocks[i].second;
      delete desc;
    }
  }
}

TrunkDesc::TrunkDesc(TrunkDesc&& other) {
  name = std::move(other.name);
  version = other.version;
  numBlocks = other.numBlocks;
  trunkNumChannels = other.trunkNumChannels;
  midNumChannels = other.midNumChannels;
  regularNumChannels = other.regularNumChannels;
  dilatedNumChannels = other.dilatedNumChannels;
  gpoolNumChannels = other.gpoolNumChannels;
  initialConv = std::move(other.initialConv);
  initialMatMul = std::move(other.initialMatMul);
  blocks = std::move(other.blocks);
  trunkTipBN = std::move(other.trunkTipBN);
  trunkTipActivation = std::move(other.trunkTipActivation);
}

TrunkDesc& TrunkDesc::operator=(TrunkDesc&& other) {
  name = std::move(other.name);
  version = other.version;
  numBlocks = other.numBlocks;
  trunkNumChannels = other.trunkNumChannels;
  midNumChannels = other.midNumChannels;
  regularNumChannels = other.regularNumChannels;
  dilatedNumChannels = other.dilatedNumChannels;
  gpoolNumChannels = other.gpoolNumChannels;
  initialConv = std::move(other.initialConv);
  initialMatMul = std::move(other.initialMatMul);
  blocks = std::move(other.blocks);
  trunkTipBN = std::move(other.trunkTipBN);
  trunkTipActivation = std::move(other.trunkTipActivation);
  return *this;
}

//-----------------------------------------------------------------------------

PolicyHeadDesc::PolicyHeadDesc() : version(-1) {}

PolicyHeadDesc::PolicyHeadDesc(istream& in, int vrsn) {
  in >> name;
  version = vrsn;

  if(in.fail())
    throw StringError(name + ": policy head failed to parse name");

  p1Conv = ConvLayerDesc(in);
  g1Conv = ConvLayerDesc(in);
  g1BN = BatchNormLayerDesc(in);
  g1Activation = ActivationLayerDesc(in);
  gpoolToBiasMul = MatMulLayerDesc(in);
  p1BN = BatchNormLayerDesc(in);
  p1Activation = ActivationLayerDesc(in);
  p2Conv = ConvLayerDesc(in);
  gpoolToPassMul = MatMulLayerDesc(in);

  if(in.fail())
    throw StringError(name + ": policy head istream fail after parsing layers");

  if(p1Conv.outChannels != p1BN.numChannels)
    throw StringError(
      name +
      Global::strprintf(": p1Conv.outChannels (%d) != p1BN.numChannels (%d)", p1Conv.outChannels, p1BN.numChannels));
  if(g1Conv.outChannels != g1BN.numChannels)
    throw StringError(
      name +
      Global::strprintf(": g1Conv.outChannels (%d) != g1BN.numChannels (%d)", g1Conv.outChannels, g1BN.numChannels));
  if(version >= 3) {
    if(gpoolToBiasMul.inChannels != g1BN.numChannels * 3)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolToBiasMul.inChannels (%d) != g1BN.numChannels*3 (%d)",
                 gpoolToBiasMul.inChannels,
                 g1BN.numChannels * 3));
  } else {
    if(gpoolToBiasMul.inChannels != g1BN.numChannels * 2)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolToBiasMul.inChannels (%d) != g1BN.numChannels*2 (%d)",
                 gpoolToBiasMul.inChannels,
                 g1BN.numChannels * 2));
  }
  if(gpoolToBiasMul.outChannels != p1BN.numChannels)
    throw StringError(
      name +
      Global::strprintf(
        ": gpoolToBiasMul.outChannels (%d) != p1BN.numChannels (%d)", gpoolToBiasMul.outChannels, p1BN.numChannels));
  if(version >= 1) {
    if(p2Conv.inChannels != p1BN.numChannels)
      throw StringError(
        name +
        Global::strprintf(": p2Conv.inChannels (%d) != p1BN.numChannels (%d)", p2Conv.inChannels, p1BN.numChannels));
  } else {
    if(p2Conv.inChannels != p1BN.numChannels * 2)
      throw StringError(
        name + Global::strprintf(
                 ": p2Conv.inChannels (%d) != p1BN.numChannels*2 (%d)", p2Conv.inChannels, p1BN.numChannels * 2));
  }
  if(p2Conv.outChannels != 1)
    throw StringError(name + Global::strprintf(": p2Conv.outChannels (%d) != 1", p2Conv.outChannels));
  if(version >= 3) {
    if(gpoolToPassMul.inChannels != g1BN.numChannels * 3)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolToPassMul.inChannels (%d) != g1BN.numChannels*3 (%d)",
                 gpoolToPassMul.inChannels,
                 g1BN.numChannels * 3));
  } else {
    if(gpoolToPassMul.inChannels != g1BN.numChannels * 2)
      throw StringError(
        name + Global::strprintf(
                 ": gpoolToPassMul.inChannels (%d) != g1BN.numChannels*2 (%d)",
                 gpoolToPassMul.inChannels,
                 g1BN.numChannels * 2));
  }
  if(gpoolToPassMul.outChannels != 1)
    throw StringError(name + Global::strprintf(": gpoolToPassMul.outChannels (%d) != 1", gpoolToPassMul.outChannels));
}

PolicyHeadDesc::~PolicyHeadDesc() {}

PolicyHeadDesc::PolicyHeadDesc(PolicyHeadDesc&& other) {
  *this = std::move(other);
}

PolicyHeadDesc& PolicyHeadDesc::operator=(PolicyHeadDesc&& other) {
  name = std::move(other.name);
  version = other.version;
  p1Conv = std::move(other.p1Conv);
  g1Conv = std::move(other.g1Conv);
  g1BN = std::move(other.g1BN);
  g1Activation = std::move(other.g1Activation);
  gpoolToBiasMul = std::move(other.gpoolToBiasMul);
  p1BN = std::move(other.p1BN);
  p1Activation = std::move(other.p1Activation);
  p2Conv = std::move(other.p2Conv);
  gpoolToPassMul = std::move(other.gpoolToPassMul);
  return *this;
}

//-----------------------------------------------------------------------------

ValueHeadDesc::ValueHeadDesc() : version(-1) {}

ValueHeadDesc::ValueHeadDesc(istream& in, int vrsn) {
  in >> name;
  version = vrsn;

  if(in.fail())
    throw StringError(name + ": value head failed to parse name");

  v1Conv = ConvLayerDesc(in);
  v1BN = BatchNormLayerDesc(in);
  v1Activation = ActivationLayerDesc(in);
  v2Mul = MatMulLayerDesc(in);
  v2Bias = MatBiasLayerDesc(in);
  v2Activation = ActivationLayerDesc(in);
  v3Mul = MatMulLayerDesc(in);
  v3Bias = MatBiasLayerDesc(in);

  if(version >= 3) {
    sv3Mul = MatMulLayerDesc(in);
    sv3Bias = MatBiasLayerDesc(in);
    vOwnershipConv = ConvLayerDesc(in);
  }

  if(in.fail())
    throw StringError(name + ": value head istream fail after parsing layers");

  if(v1Conv.outChannels != v1BN.numChannels)
    throw StringError(
      name +
      Global::strprintf(": v1Conv.outChannels (%d) != v1BN.numChannels (%d)", v1Conv.outChannels, v1BN.numChannels));

  if(version >= 3) {
    if(v2Mul.inChannels != v1BN.numChannels * 3)
      throw StringError(
        name + Global::strprintf(
                 ": v2Mul.inChannels (%d) != v1BN.numChannels*3 (%d)", v2Mul.inChannels, v1BN.numChannels * 3));
  } else {
    if(v2Mul.inChannels != v1BN.numChannels)
      throw StringError(
        name +
        Global::strprintf(": v2Mul.inChannels (%d) != v1BN.numChannels (%d)", v2Mul.inChannels, v1BN.numChannels));
  }

  if(v2Mul.outChannels != v2Bias.numChannels)
    throw StringError(
      name +
      Global::strprintf(": v2Mul.outChannels (%d) != v2Bias.numChannels (%d)", v2Mul.outChannels, v2Bias.numChannels));
  if(version >= 1) {
    if(v2Mul.outChannels != v3Mul.inChannels)
      throw StringError(
        name +
        Global::strprintf(": v2Mul.outChannels (%d) != v3Mul.inChannels (%d)", v2Mul.outChannels, v3Mul.inChannels));
  } else {
    if(v2Mul.outChannels * 2 != v3Mul.inChannels)
      throw StringError(
        name + Global::strprintf(
                 ": v2Mul.outChannels*2 (%d) != v3Mul.inChannels (%d)", v2Mul.outChannels * 2, v3Mul.inChannels));
  }
  if(version >= 3) {
    if(v3Mul.outChannels != 3)
      throw StringError(name + Global::strprintf(": v3Mul.outChannels (%d) != 3", v3Mul.outChannels));
    if(v3Bias.numChannels != 3)
      throw StringError(name + Global::strprintf(": v3Bias.numChannels (%d) != 3", v3Bias.numChannels));
  } else {
    if(v3Mul.outChannels != 1)
      throw StringError(name + Global::strprintf(": v3Mul.outChannels (%d) != 1", v3Mul.outChannels));
    if(v3Bias.numChannels != 1)
      throw StringError(name + Global::strprintf(": v3Bias.numChannels (%d) != 1", v3Bias.numChannels));
  }

  if(version >= 3) {
    if(sv3Mul.inChannels != v2Mul.outChannels)
      throw StringError(
        name +
        Global::strprintf(": sv3Mul.inChannels (%d) != v2Mul.outChannels (%d)", sv3Mul.inChannels, v2Mul.outChannels));

    if(version >= 4) {
      if(sv3Mul.outChannels != 2)
        throw StringError(name + Global::strprintf(": sv3Mul.outChannels (%d) != 2", sv3Mul.outChannels));
      if(sv3Bias.numChannels != 2)
        throw StringError(name + Global::strprintf(": sv3Bias.numChannels (%d) != 2", sv3Bias.numChannels));
    } else {
      if(sv3Mul.outChannels != 1)
        throw StringError(name + Global::strprintf(": sv3Mul.outChannels (%d) != 1", sv3Mul.outChannels));
      if(sv3Bias.numChannels != 1)
        throw StringError(name + Global::strprintf(": sv3Bias.numChannels (%d) != 1", sv3Bias.numChannels));
    }

    if(vOwnershipConv.inChannels != v1Conv.outChannels)
      throw StringError(
        name + Global::strprintf(
                 ": vOwnershipConv.outChannels (%d) != v1Conv.outChannels (%d)",
                 vOwnershipConv.inChannels,
                 v1Conv.outChannels));
    if(vOwnershipConv.outChannels != 1)
      throw StringError(name + Global::strprintf(": vOwnershipConv.outChannels (%d) != 1", vOwnershipConv.outChannels));
  }
}

ValueHeadDesc::~ValueHeadDesc() {}

ValueHeadDesc::ValueHeadDesc(ValueHeadDesc&& other) {
  *this = std::move(other);
}

ValueHeadDesc& ValueHeadDesc::operator=(ValueHeadDesc&& other) {
  name = std::move(other.name);
  version = other.version;
  v1Conv = std::move(other.v1Conv);
  v1BN = std::move(other.v1BN);
  v1Activation = std::move(other.v1Activation);
  v2Mul = std::move(other.v2Mul);
  v2Bias = std::move(other.v2Bias);
  v2Activation = std::move(other.v2Activation);
  v3Mul = std::move(other.v3Mul);
  v3Bias = std::move(other.v3Bias);
  sv3Mul = std::move(other.sv3Mul);
  sv3Bias = std::move(other.sv3Bias);
  vOwnershipConv = std::move(other.vOwnershipConv);
  return *this;
}

//-----------------------------------------------------------------------------

ModelDesc::ModelDesc()
  : version(-1),
    xSizePreV3(0),
    ySizePreV3(0),
    numInputChannels(0),
    numInputGlobalChannels(0),
    numValueChannels(0),
    numScoreValueChannels(0),
    numOwnershipChannels(0) {}

ModelDesc::ModelDesc(istream& in) {
  in >> name;
  in >> version;
  if(in.fail())
    throw StringError(name + ": model failed to parse name or version");

  if(version < 0 || version > NNModelVersion::latestModelVersionImplemented)
    throw StringError(name + ": model found unsupported version " + Global::intToString(version));
  if(version < 1)
    throw StringError("Version 0 neural nets no longer supported in cuda backend");

  if(version >= 3) {
    xSizePreV3 = 0;  // Unused, V3 uses posLen instead
    ySizePreV3 = 0;  // Unused, V3 uses posLen instead
  } else {
    in >> xSizePreV3;
    in >> ySizePreV3;
    if(in.fail())
      throw StringError(name + ": model failed to parse xSize or ySize");
    if(xSizePreV3 <= 0 || ySizePreV3 <= 0)
      throw StringError(name + ": model xSize and ySize must be positive");
  }

  in >> numInputChannels;
  if(in.fail())
    throw StringError(name + ": model failed to parse numInputChannels");
  if(numInputChannels <= 0)
    throw StringError(name + ": model numInputChannels must be positive");

  if(version >= 3) {
    in >> numInputGlobalChannels;
    if(in.fail())
      throw StringError(name + ": model failed to parse numInputGlobalChannels");
    if(numInputGlobalChannels <= 0)
      throw StringError(name + ": model numInputGlobalChannels must be positive");
  } else
    numInputGlobalChannels = 0;

  trunk = TrunkDesc(in, version);
  policyHead = PolicyHeadDesc(in, version);
  valueHead = ValueHeadDesc(in, version);

  numValueChannels = valueHead.v3Mul.outChannels;
  numScoreValueChannels = valueHead.sv3Mul.outChannels;
  numOwnershipChannels = valueHead.vOwnershipConv.outChannels;

  if(in.fail())
    throw StringError(name + ": model desc istream fail after parsing model");

  if(numInputChannels != trunk.initialConv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": numInputChannels (%d) != trunk.initialConv.inChannels (%d)",
               numInputChannels,
               trunk.initialConv.inChannels));
  if(version >= 3) {
    if(numInputGlobalChannels != trunk.initialMatMul.inChannels)
      throw StringError(
        name + Global::strprintf(
                 ": numInputChannels (%d) != trunk.initialMatMul.inChannels (%d)",
                 numInputGlobalChannels,
                 trunk.initialMatMul.inChannels));
  }

  if(trunk.trunkNumChannels != policyHead.p1Conv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": trunk.trunkNumChannels (%d) != policyHead.p1Conv.inChannels (%d)",
               trunk.trunkNumChannels,
               policyHead.p1Conv.inChannels));
  if(trunk.trunkNumChannels != policyHead.g1Conv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": trunk.trunkNumChannels (%d) != policyHead.g1Conv.inChannels (%d)",
               trunk.trunkNumChannels,
               policyHead.g1Conv.inChannels));
  if(trunk.trunkNumChannels != valueHead.v1Conv.inChannels)
    throw StringError(
      name + Global::strprintf(
               ": trunk.trunkNumChannels (%d) != valueHead.v1Conv.inChannels (%d)",
               trunk.trunkNumChannels,
               valueHead.v1Conv.inChannels));
}

ModelDesc::~ModelDesc() {}

ModelDesc::ModelDesc(ModelDesc&& other) {
  *this = std::move(other);
}

ModelDesc& ModelDesc::operator=(ModelDesc&& other) {
  name = std::move(other.name);
  version = other.version;
  xSizePreV3 = other.xSizePreV3;
  ySizePreV3 = other.ySizePreV3;
  numInputChannels = other.numInputChannels;
  numInputGlobalChannels = other.numInputGlobalChannels;
  numValueChannels = other.numValueChannels;
  numScoreValueChannels = other.numScoreValueChannels;
  numOwnershipChannels = other.numOwnershipChannels;
  trunk = std::move(other.trunk);
  policyHead = std::move(other.policyHead);
  valueHead = std::move(other.valueHead);
  return *this;
}

void ModelDesc::loadFromFileMaybeGZipped(const string& fileName, ModelDesc& descBuf) {
  try {
    //zstr has a bad property of simply aborting the program if the file doesn't exist
    //So we try to catch this common error by explicitly testing first if the file exists by trying to open it normally
    //to turn it into a regular C++ exception.
    {
      ifstream testIn(fileName);
      if(!testIn.good())
        throw StringError("File does not exist or could not be opened: " + fileName);
    }
    zstr::ifstream in(fileName);
    descBuf = std::move(ModelDesc(in));
  }
  catch(const StringError& e) {
    throw StringError("Error parsing model file " + fileName + ": " + e.what());
  }
}


Rules ModelDesc::getSupportedRules(const Rules& desiredRules, bool& supported) const {
  static_assert(NNModelVersion::latestModelVersionImplemented == 6, "");
  Rules rules = desiredRules;
  supported = true;
  if(version <= 6) {
    if(rules.koRule == Rules::KO_SIMPLE || rules.koRule == Rules::KO_SPIGHT) {
      rules.koRule = Rules::KO_SITUATIONAL;
      supported = false;
    }
    if(rules.scoringRule == Rules::SCORING_TERRITORY) {
      rules.scoringRule = Rules::SCORING_AREA;
      supported = false;
    }
  }
  else {
    ASSERT_UNREACHABLE;
  }

  return rules;
}
